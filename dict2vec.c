/* Copyright (c) 2017-present, All rights reserved.
 * Written by Julien Tissier <30314448+tca19@users.noreply.github.com>
 *
 * This file is part of Dict2vec.
 *
 * Dict2vec is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Dict2vec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License at the root of this repository for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Dict2vec.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>      /* strcat */
#include <math.h>
#include <pthread.h>

#define MAXLEN       100
#define MAXLINE      1000

#define SIGMOID_SIZE 512
#define MAX_SIGMOID  6

#define HASHSIZE     30000000
#define NEGSIZE      100000000

struct entry
{
	/* Words forming a strong pair with this entry are stored in the array
	 * sp[] (only the index of words are stored). Instead of calculating a
	 * new random index in sp[], the indexes are randomly shuffled at the
	 * beginning and a sliding cursor indicates the current word to draw
	 * (faster because no need to compute a lot of random indexes).
	 * Weak pairs follow the same implementation.
	 */
	int n_sp;       /* number of strong pairs of entry */
	int pos_sp;     /* current cursor position in sp[] */
	int *sp;

	int n_wp;       /* number of weak pairs of entry */
	int pos_wp;     /* current cursor position in wp[] */
	int *wp;

	long  count;    /* number of occurrences of entry in input file */
	char  *word;    /* string associated to the entry */
	float pdiscard; /* probability to discard entry when found in input */
};

struct parameters
{
	char input[MAXLEN];
	char output[MAXLEN];

	int dim;
	int window;
	int min_count;
	int negative;
	int strong_draws;
	int weak_draws;
	int num_threads;
	int epoch;
	int save_each_epoch;

	float alpha;
	float starting_alpha;
	float sample;
	float beta_strong;
	float beta_weak;
};

/* dynamic array containing 1 entry for each word in vocabulary */
struct entry *vocab;

struct parameters args = {
	"", "",
	100, 5, 5, 5, 0, 0, 1, 1, 0,
	0.025, 0.025, 1e-4, 1.0, 0.25
};

/* variables required for processing input file */
long vocab_max_size = 10000, vocab_size = 0, train_words = 0, file_size = 0,
	word_count_actual = 0;


int hashtab[HASHSIZE];          /* hash table to know index of a word */
int negtab[NEGSIZE];            /* array of indexes for negative sampling */
float sigmoid[SIGMOID_SIZE];    /* precomputed sigmoid values */

float *WI, *WO;    /* weight matrices */

/* other variables */
clock_t start;
int current_epoch = 0, neg_pos = 0;

/* contains: return 1 if value is inside array. 0 otherwise. */
int contains(int *array, int value, int size)
{
	int j;

	for (j = 0; j < size; ++j)
		if (array[j] == value)
			return 1;
	return 0;
}

/* shuffle: arrange the elements of array in random order. Swap two random cells
 * N times (N is the size of array).
 */
void shuffle(int *array, int size)
{
	int i, j, tmp;

	for (i = 0; i < size - 1; ++i)
	{
		j = i + rand() / (RAND_MAX / (size - i) + 1);
		tmp = array[j];
		array[j] = array[i];
		array[i] = tmp;
	}
}

/* init_negative_table: initialize the negative table used for negative
 * sampling. The table is composed of indexes of words, each one proportional
 * to the number of occurrence of this word.
 */
void init_negative_table()
{
	int i, n_cells, pos;
	float sum, d;

	/* compute the sum of count^0.75 for all words */
	for (i = 0, sum = 0.0; i < vocab_size; ++i)
		sum += pow(vocab[i].count, 0.75);

	/* multiply is faster than divide so precompute d = 1/sum */
	for (i = 0, pos = 0, d = 1.0/sum; i < vocab_size; ++i)
	{
		/* compute number of cells reserved for word[i] */
		n_cells = pow(vocab[i].count, 0.75) * NEGSIZE * d + 1;

		while (n_cells-- && pos < NEGSIZE)
			negtab[pos++] = i;
	}

	/* shuffle the array so we don't have to draw a new random index each
	 * time we want a negative sample, simply keep a position_index, get the
	 * negtab[position_index] value and increment position_index. This is the
	 * same as drawing a new random index i and get negtab[i]. */
	shuffle(negtab, NEGSIZE);
}

/* compute_discard_prob: compute the discard probabilty of each word. The
 * probability is defined as: p(w) = 1 - sqrt(t / f(w)) where t is the
 * threshold and f(w) is the frequency of word w. But we store
 * Y = sqrt(t / f(w)). Then we draw a random number X between 0 and 1 and
 * look if X > Y. X has a probability p(w) of being greater than Y.
 * If N is the total number of words, we have:
 *   Y = sqrt(t / ( count(w) / N )) = sqrt(t * N) / sqrt(count(w))
 */
void compute_discard_prob()
{
	int i;
	float w;

	/* precompute sqrt(t * n) */
	w = sqrt(args.sample * train_words);
	for (i = 0; i < vocab_size; ++i)
		vocab[i].pdiscard = w / sqrt(vocab[i].count);
}

/* hash: form hash value for string s */
unsigned int hash(char *s)
{
	unsigned int hashval;

	for (hashval = 0; *s != '\0'; ++s)
		hashval = hashval * 257 + *s;
	return hashval % HASHSIZE;
}

/* find: return the position of string s in hashtab. If word has never been
 * met, the cell at index hash(word) in hashtab will be -1. If the cell is
 * not -1, we start to compare word to each element with the same hash.
 */
unsigned int find(char *s)
{
	unsigned int h = hash(s);

	while (hashtab[h] != -1 && strcmp(s, vocab[hashtab[h]].word))
		h = (h + 1) % HASHSIZE;
	return h;
}

/* add word to the vocabulary. If word already exists, increment its count */
void add_word(char *word)
{
	unsigned int h = find(word);
	if (hashtab[h] == -1)
	{
		/* create new entry */
		struct entry e;
		e.word = malloc(sizeof(char) * (strlen(word)+1));
		strcpy(e.word, word);
		e.count    = 1;
		e.pdiscard = 1.0;
		e.n_sp     = 0;
		e.pos_sp   = 0;
		e.n_wp     = 0;
		e.pos_wp   = 0;
		e.sp       = NULL;
		e.wp       = NULL;

		/* add it to vocab and set its index in hashtab */
		vocab[vocab_size] = e;
		hashtab[h] = vocab_size++;

		/* reallocate more space if needed */
		if (vocab_size >= vocab_max_size)
		{
			vocab_max_size += 10000;
			vocab = realloc(vocab, vocab_max_size * sizeof(struct entry));
		}
	}
	else
	{
		vocab[hashtab[h]].count++;
	}
}

/* compare_words: used to sort two words */
int compare_words(const void *a, const void *b)
{
	return ((struct entry *)b)->count - ((struct entry *)a)->count;
}

/* destroy_vocab: free all memory used to create stong/weak pairs arrays, free
 * memory used to store words (char *) and free the entire array of entries.
 */
void destroy_vocab()
{
	int i;

	for (i = 0; i < vocab_size; ++i)
	{
		free(vocab[i].word);

		if (vocab[i].sp != NULL)
			free(vocab[i].sp);

		if (vocab[i].wp != NULL)
			free(vocab[i].wp);
	}

	free(vocab);
}

/* sort_and_reduce_vocab: sort the words in vocabulary by their number of
 * occurrences. Remove all words with less than min_count occurrences.
 */
void sort_and_reduce_vocab()
{
	int i, valid_words;

	/* sort vocab in descending order by number of word occurrence */
	qsort(vocab, vocab_size, sizeof(struct entry), compare_words);

	/* get the number of valid words (words with count >= min_count) */
	valid_words = 0;
	while (vocab[valid_words++].count >= args.min_count)
		continue;

	/* because valid_words has been incremented after the condition has
	 * been evaluated to false */
	valid_words--;

	/* remove words with less than min_count occurrences. Strong and weak
	 * pairs have not been added to vocab yet, so no need to free the
	 * allocated memory of strong/weak pairs arrays. */
	for (i = valid_words; i < vocab_size; ++i)
	{
		train_words -= vocab[i].count;
		free(vocab[i].word);
		vocab[i].word = NULL;
	}

	/* resize the vocab array with its new size */
	vocab_size = valid_words;
	vocab = realloc(vocab, vocab_size * sizeof(struct entry));

	/* sorting has changed the index of each word, so update the value
	 in hashtab. Start by reseting all values to -1.*/
	for (i = 0; i < HASHSIZE; ++i)
		hashtab[i] = -1;
	for (i = 0; i < vocab_size; ++i)
		hashtab[find(vocab[i].word)] = i;
}

/* read_strong_pairs; read the file containing the strong pairs. For each pair,
 * add it in the vocab for both words involved.
 */
int read_strong_pairs(char *filename)
{
	FILE *fi;
	char word1[MAXLEN], word2[MAXLEN];
	int i1, i2, len;

	if ((fi = fopen(filename, "r")) == NULL)
	{
		printf("WARNING: strong pairs data not found!\n"
		       "Not taken into account during learning.\n");
		return 1;
	}

	while ((fscanf(fi, "%s %s", word1, word2) != EOF))
	{
		i1 = find(word1);
		i2 = find(word2);

		/* nothing to do if one of the word is not in vocab */
		if (hashtab[i1] == -1 || hashtab[i2] == -1)
			continue;

		/* get the real indexes (not the index in the hash table) */
		i1 = hashtab[i1];
		i2 = hashtab[i2];

		/* look if we already have added one strong pair for i1. If not,
		 * create the array. Else, expand by one cell. */
		len = vocab[i1].n_sp;
		if (len == 0)
			vocab[i1].sp = calloc(1, sizeof(int));
		else
			vocab[i1].sp = realloc(vocab[i1].sp, (len+1) * sizeof(int));

		/* add i2 to the list of strong pairs indexes of word1 */
		vocab[i1].sp[len] = i2;
		vocab[i1].n_sp++;

		/*   ---------   */

		/* look if we already have added one pair for i2. If not, create
		 the array. Else, expand by one cell. */
		len = vocab[i2].n_sp;
		if (len == 0)
			vocab[i2].sp = calloc(1, sizeof(int));
		else
			vocab[i2].sp = realloc(vocab[i2].sp, (len+1) * sizeof(int));

		/* add i2 to the list of strong pairs indexes of word1 */
		vocab[i2].sp[len] = i1;
		vocab[i2].n_sp++;
	}

	fclose(fi);
	return 0;
}

/* read_weak_pairs: read the file containing the weak pairs. For each pair, add
 * it in the vocab for both words involved.
 */
int read_weak_pairs(char *filename)
{
	FILE *fi;
	char word1[MAXLEN], word2[MAXLEN];
	int i1, i2, len;

	if ((fi = fopen(filename, "r")) == NULL)
	{
		printf("WARNING: weak pairs data not found!\n"
		       "Not taken into account during learning.\n");
		return 1;
	}

	while ((fscanf(fi, "%s %s", word1, word2) != EOF))
	{
		i1 = find(word1);
		i2 = find(word2);

		/* nothing to do if one of the word is not in vocab */
		if (hashtab[i1] == -1 || hashtab[i2] == -1)
			continue;

		/* get the real indexes (not the index in the hash table) */
		i1 = hashtab[i1];
		i2 = hashtab[i2];

		/* look if we already have added one pair for i1. If not, create
		the array. Else, expand by one cell. */
		len = vocab[i1].n_wp;
		if (len == 0)
			vocab[i1].wp = calloc(1, sizeof(int));
		else
			vocab[i1].wp = realloc(vocab[i1].wp, (len+1) * sizeof(int));

		/* add i2 to the list of weak pairs indexes of word1 */
		vocab[i1].wp[len] = i2;
		vocab[i1].n_wp++;

		/*   ---------   */

		/* look if we already have added one pair for i2. If not, create
		 the array. Else, expand by one cell. */
		len = vocab[i2].n_wp;
		if (len == 0)
			vocab[i2].wp = calloc(1, sizeof(int));
		else
			vocab[i2].wp = realloc(vocab[i2].wp, (len+1) * sizeof(int));

		/* add i1 to the list of weak pairs indexes of word2 */
		vocab[i2].wp[len] = i1;
		vocab[i2].n_wp++;
	}

	fclose(fi);
	return 0;
}

/* read_vocab: read the file given as -input. For each word, either add it in
 * the vocab or increment its occurrence. Also read the strong and weak pairs
 * files if provided. Sort the vocabulary by occurrences and display some infos.
 */
void read_vocab(char *input_fn, char *strong_fn, char *weak_fn)
{
	FILE *fi;
	int i, failure_strong, failure_weak;
	char word[MAXLEN];

	if ((fi = fopen(input_fn, "r")) == NULL)
	{
		printf("ERROR: training data file not found!\n");
		exit(1);
	}

	/* init the hash table with -1 */
	for (i = 0; i < HASHSIZE; ++i)
		hashtab[i] = -1;

	/* some words are longer than MAXLEN, need to indicate a maximum width
	 * to scanf so no buffer overflow. */
	while ((fscanf(fi, "%100s", word) != EOF))
	{
		/* increment total number of read words */
		train_words++;
		if (train_words % 500000 == 0)
		{
			printf("%ldK%c", train_words / 1000, 13);
			fflush(stdout);
		}

		/* add word we just read or increment its count if needed */
		add_word(word);

		/* Wikipedia has around 8M unique words, so we never hit the
		 * 21M words limit and therefore never need to reduce the vocab
		 * during creation of vocabulary. If your input file contains
		 * much more unique words, you might need to uncomment it. */
		/*if (vocab_size > HASHSIZE * 0.7)
			sort_and_reduce_vocab();*/
	}

	sort_and_reduce_vocab(HASHSIZE);

	printf("Vocab size: %ld\n", vocab_size);
	printf("Words in train file: %ld\n", train_words);

	printf("Adding strong pairs...");
	failure_strong = read_strong_pairs(strong_fn);
	printf("\nAdding weak pairs...");
	failure_weak = read_weak_pairs(weak_fn);
	if (!failure_strong || !failure_weak)
		printf("\nAdding pairs done.\n");

	/* compute the discard probability for each word (only if we
	 * subsample)*/
	if (args.sample > 0)
		compute_discard_prob();

	/* each thread is assigned a part of the input file. To distribute
	 the work to each thread, we need to know the total size of the file */
	file_size = ftell(fi);
	fclose(fi);
}

/* init_network: initialize matrix WI (random values) and WO (zero values) */
void init_network()
{
	float r, l;
	int i, j;

	if ((WI = malloc(sizeof *WI * vocab_size * args.dim)) == NULL)
	{
		printf("Memory allocation failed for WI\n");
		exit(1);
	}


	if ((WO = calloc(vocab_size * args.dim, sizeof *WO)) == NULL)
	{
		printf("Memory allocation failed for WO\n");
		exit(1);
	}

	/* WI is initialized with random values from (-0.5 / vec_dimension)
	 * and (0.5 / vec_dimension). Multiply is faster than divide so
	 * precompute 1 / RAND_MAX and 1 / dim. */
	r = 1.0 / RAND_MAX;
	l = 1.0 / args.dim;
	for (i = 0; i < vocab_size; ++i)
		for (j = 0; j < args.dim; ++j)
			WI[i * args.dim + j] = ( (rand() * r) - 0.5 ) * l;
}

/* destroy_network: free the memory allocated for matrices WI and WO */
void destroy_network()
{
	if (WI != NULL)
		free(WI);

	if (WO != NULL)
		free(WO);
}

void *train_thread(void *id)
{
	FILE *fi;
	char word[MAXLEN];
	int w_t, w_c, c, d, target, line_size, pos, line[MAXLINE];
	int index1, index2, k, half_ws, helper;
	long word_count_local, negsamp_discarded, negsamp_total;
	float label, dot_prod, grad, *hidden;
	double progress, wts, discarded, cps, d_train, lr_coef;

	clock_t now;
	int rnd = (intptr_t) id;

	if ((fi = fopen(args.input, "r")) == NULL)
	{
		printf("ERROR: training data file not found!\n");
		exit(1);
	}

	/* init variables */
	fseek(fi, file_size / args.num_threads * rnd, SEEK_SET);
	word_count_local = negsamp_discarded = negsamp_total = 0;
	hidden           = calloc(args.dim, sizeof *hidden);
	half_ws          = args.window / 2;
	helper           = SIGMOID_SIZE / MAX_SIGMOID / 2;
	wts = discarded  = 0.0f;
	cps              = 1000.0f / CLOCKS_PER_SEC;
	d_train          = 1.0f / train_words;
	lr_coef          = args.starting_alpha / ((double) (args.epoch * train_words));

	while (word_count_actual < (train_words * (current_epoch + 1)))
	{
		/* update learning rate and print progress */
		if (word_count_local > 20000)
		{
			args.alpha -= word_count_local * lr_coef;
			word_count_actual += word_count_local;
			word_count_local = 0;
			now = clock();

			/* "Discarded" is the percentage of discarded negative
			 * samples because they form either a strong or a weak
			 * pair with context word */
			progress = word_count_actual * d_train * 100;
			progress -= 100 * current_epoch;
			wts = word_count_actual / ((double)(now - start) * cps);
			discarded = negsamp_discarded * 100.0 / negsamp_total;
			printf("%clr: %f  Progress: %.2f%%  Words/thread/sec:"
			       " %.2fk  Discarded: %.2f%% ",
			       13, args.alpha, progress, wts, discarded);
			fflush(stdout);
		}

		/* read MAXLINE words from input file. Add words in line[] if
		 * they are in vocabulary and not discarded. So length of line
		 * might be less than MAXLINE (in practice, length of line is
		 * 500 +/- 50. */
		line_size = 0;
		for (k = MAXLINE; k--;)
		{
			/* some words are longer than MAXLEN, need to indicate a
			 * maximum width to scanf so no buffer overflow. */
			fscanf(fi, "%100s", word);
			w_t = hashtab[find(word)];

			/* word is not in vocabulary, move to next one */
			if (w_t == -1)
				continue;

			/* we processed one more word */
			++word_count_local;

			/* discard word or add it in the sentence */
			rnd = rnd * 1103515245 + 12345;
			if (vocab[w_t].pdiscard < (rnd & 0xFFFF) / 65536.0)
				continue;
			else
				line[line_size++] = w_t;
		}

		/* for each word of the line */
		for (pos = half_ws; pos < line_size - half_ws; ++pos)
		{
			w_t = line[pos];  /* central word */

			/* for each word of the context window */
			for (c = pos - half_ws; c < pos + half_ws +1; ++c)
			{
				if (c == pos)
					continue;

				w_c = line[c];
				index1 = w_c * args.dim;

				/* zero the hidden vector */
				memset(hidden, 0.0, args.dim * sizeof *hidden);

				/* STANDARD AND NEGATIVE SAMPLING UPDATE */
				for (d = args.negative+1; d--;)
				{
					/* target is central word */
					if (d == 0)
					{
						target = w_t;
						label = 1.0;
					}

					/* target is random word */
					else
					{
						do
						{
							target = negtab[neg_pos++];
							if (neg_pos >= NEGSIZE)
								neg_pos = 0;
						} while (target == w_t);

						/* if random word form a strong a weak pair
						 with w_c, move to next one */
						if (contains(vocab[w_c].sp, target,
						             vocab[w_c].n_sp) ||
						    contains(vocab[w_c].wp, target,
						             vocab[w_c].n_wp))
						{
							++negsamp_discarded;
							continue;
						}

						++negsamp_total;
						label = 0.0;
					}


					/* forward propagation */
					index2 = target * args.dim;
					dot_prod = 0.0;
					for (k = 0; k < args.dim; ++k)
						dot_prod += WI[index1 + k] * WO[index2 + k];

					if (dot_prod > MAX_SIGMOID)
						grad = args.alpha * (label - 1.0);
					else if (dot_prod < -MAX_SIGMOID)
						grad = args.alpha * label;
					else
						grad = args.alpha * (label - sigmoid[(int)
						       ((dot_prod + MAX_SIGMOID) * helper)]);

					/* back-propagation. 2 for loops is more
					 cache friendly because processor
					 can load the entire hidden array in
					 cache. Using a unique for loop to do
					 the 2 operations is slower. */
					for (k = 0; k < args.dim; ++k)
						hidden[k] += grad * WO[index2 + k];
					for (k = 0; k < args.dim; ++k)
						WO[index2 + k] += grad * WI[index1 + k];
				}

				/* POSITIVE SAMPLING UPDATE (strong pairs) */
				for (d = args.strong_draws; d--;)
				{
					/* can't do anything if no strong pairs
					 */
					if (vocab[w_c].n_sp == 0)
						break;

					if (vocab[w_c].pos_sp > vocab[w_c].n_sp - 1)
						vocab[w_c].pos_sp = 0;
					target = vocab[w_c].sp[vocab[w_c].pos_sp++];

					index2 = target * args.dim;
					dot_prod = 0;
					for (k = 0; k < args.dim; ++k)
						dot_prod += WI[index1 + k] * WO[index2 + k];

					/* dot product is already high, nothing to do */
					if (dot_prod > MAX_SIGMOID)
						continue;
					else if (dot_prod < -MAX_SIGMOID)
						grad = args.alpha * args.beta_strong;
					else
						grad = args.alpha * args.beta_strong *
						    (1 - sigmoid[(int)((dot_prod + MAX_SIGMOID) * helper)]);


					for (k = 0; k < args.dim; ++k)
						hidden[k] += grad * WO[index2 + k];
					for (k = 0; k < args.dim; ++k)
						WO[index2 + k] += grad * WI[index1 + k];
				}

				/* POSITIVE SAMPLING UPDATE (weak pairs) */
				for (d = args.weak_draws; d--;)
				{
					/* can't do anything if no weak pairs */
					if (vocab[w_c].n_wp == 0)
						break;

					if (vocab[w_c].pos_wp > vocab[w_c].n_wp - 1)
						vocab[w_c].pos_wp = 0;
					target = vocab[w_c].wp[vocab[w_c].pos_wp++];

					index2 = target * args.dim;
					dot_prod = 0;
					for (k = 0; k < args.dim; ++k)
						dot_prod += WI[index1 + k] * WO[index2 + k];

					if (dot_prod > MAX_SIGMOID)
						continue;
					else if (dot_prod < -MAX_SIGMOID)
						grad = args.alpha * args.beta_weak;
					else
						grad = args.alpha * args.beta_weak *
						    (1 - sigmoid[(int)((dot_prod + MAX_SIGMOID) * helper)]);

					for (k = 0; k < args.dim; ++k)
						hidden[k] += grad * WO[index2 + k];
					for (k = 0; k < args.dim; ++k)
						WO[index2 + k] += grad * WI[index1 + k];
				}

				/* Back-propagate hidden -> input */
				for (k = 0; k < args.dim; ++k)
					WI[index1 + k] += hidden[k];

			} /* end for each word in the context window */

		}     /* end for each word in line */
	}         /* end while() loop for reading file */

	/* sometimes, progress go over 100% because of rounding float error.
	print a proper 100% progress */
	if (args.alpha < 0) args.alpha = 0;
	printf("%clr: %f  Progress: %.2f%%  Words/thread/sec: %.2fk  Discarded:"
	       " %.2f%% ", 13, args.alpha, 100.0, wts, discarded);
	fflush(stdout);

	fclose(fi);
	free(hidden);
	pthread_exit(NULL);
}

/* save the word vectors in output file. If epoch > 0, add the suffix
 * indicating the epoch */
void save_vectors(char *output, int epoch)
{
	FILE *fo;
	char suffix[15], copy[MAXLEN];
	int i, j;

	if (epoch > 0)
	{
		sprintf(suffix, "-epoch-%d.vec", epoch);
		strcpy(copy, output);
		fo = fopen(strcat(copy, suffix), "w");
	}

	else
		fo = fopen(strcat(output, ".vec"), "w");

	if (fo == NULL)
	{
		printf("Cannot open %s: permission denied\n", output);
		exit(1);
	}

	/* first line is number of vectors + dimension */
	fprintf(fo, "%ld %d\n", vocab_size, args.dim);

	for (i = 0; i < vocab_size; i++)
	{
		fprintf(fo, "%s ", vocab[i].word);

		for (j = 0; j < args.dim; j++)
			fprintf(fo, "%.3f ", WI[i * args.dim + j]);

		fprintf(fo, "\n");
	}

	fclose(fo);
}

int arg_pos(char *str, int argc, char **argv)
{
	int a;

	/* goes from 1 to argc-1 because position of option str can't be
	 0 (program name) or last element of argv */
	for (a = 1; a < argc - 1; ++a)
	{
		if (!strcmp(str, argv[a]))
			return a;
	}

	return -1;
}

void print_help()
{
	/* ISO 90 forbids strings longer than 509 characters (including
	 * concatenated strings, so documentation is splitted into multiple
	 * strings. */
	printf(
	"Dict2vec: Learning Word Embeddings using Lexical Dictionaries\n"
	"Author: Julien Tissier <30314448+tca19@users.noreply.github.com>\n\n"
	);

	printf(
	"Options:\n"
	"  -input <file>\n"
	"    Train the model with text data from <file>\n\n"
	"  -strong-file <file>\n"
	"    Add strong pairs data from <file> to improve the model\n\n"
	"  -weak-file <file>\n"
	"    Add weak pairs data from <file> to improve the model\n\n"
	"  -output <file>\n"
	"    Save word embeddings in <file>\n\n"
	);

	printf(
	"  -size <int>\n"
	"    Size of word vectors; default 100\n\n"
	"  -window <int>\n"
	"    Window size for target/context pairs generation; default 5\n\n"
	"  -sample <float>\n"
	"    Value of the threshold t used for subsampling frequent words in\n"
	"    the original word2vec paper of Mikolov; default 1e-4\n\n"
	"  -min-count <int>\n"
	"    Do not train words with less than <int> occurrences; default 5\n\n"
	"  -negative <int>\n"
	"    Number of random words used for negative sampling; default 5\n\n"
	"  -alpha <float>\n"
	"    Starting learning rate; default 0.025\n\n"
	);

	printf(
	"  -strong-draws <int>\n"
	"    Number of strong pairs picked for positive sampling; default 0\n\n"
	"  -weak-draws <int>\n"
	"    Number of weak pairs picked for positive sampling; default 0\n\n"
	"  -beta-strong <float>\n"
	"    Coefficient for strong pairs; default 1.0\n\n"
	"  -beta-weak <float>\n"
	"    Coefficient for weak pairs; default 0.25\n\n"
	"  -threads <int>\n"
	"    Number of threads to use; default 1\n\n"
	"  -epoch <int>\n"
	"    Number of epoch; default 1\n\n"
	"  -save-each-epoch <int>\n"
	"    Save the embeddings after each epoch; 0 (off, default), 1 (on)"
	);

	printf(
	"\n\nUsage:\n"
	"./dict2vec -input data/enwiki-50M -output data/enwiki-50M \\\n"
	"-strong-file data/strong-pairs.txt -weak-file data/weak-pairs.txt \\\n"
	"-size 100 -window 5 -sample 1e-4 -min-count 5 -negative 5 \\\n"
	"-strong-draws 4 -beta-strong 0.8 -weak-draws 5 -beta-weak 0.45 \\\n"
	"-alpha 0.025 -threads 8 -epoch 5 -save-each-epoch 0\n\n"
	);
}

void parse_args(int argc, char **argv, struct parameters *args,
		char *spairs_file, char *wpairs_file)
{
	for (++argv, --argc; argc >= 2; ++argv, argc -= 2)
	{
		/* string arguments */
		if (strcmp(*argv, "-strong-file") == 0)
			strcpy(spairs_file, *++argv);
		if (strcmp(*argv, "-weak-file") == 0)
			strcpy(wpairs_file, *++argv);
		if (strcmp(*argv, "-input") == 0)
			strcpy(args->input, *++argv);
		if (strcmp(*argv, "-output") == 0)
			strcpy(args->output, *++argv);

		/* integer arguments */
		if (strcmp(*argv, "-size") == 0)
			args->dim = atoi(*++argv);
		if (strcmp(*argv, "-window") == 0)
			args->window = atoi(*++argv);
		if (strcmp(*argv, "-min-count") == 0)
			args->min_count = atoi(*++argv);
		if (strcmp(*argv, "-negative") == 0)
			args->negative = atoi(*++argv);
		if (strcmp(*argv, "-strong-draws") == 0)
			args->strong_draws = atoi(*++argv);
		if (strcmp(*argv, "-weak-draws") == 0)
			args->weak_draws = atoi(*++argv);
		if (strcmp(*argv, "-threads") == 0)
			args->num_threads = atoi(*++argv);
		if (strcmp(*argv, "-epoch") == 0)
			args->epoch = atoi(*++argv);
		if (strcmp(*argv, "-save-each-epoch") == 0)
			args->save_each_epoch = atoi(*++argv);

		/* float arguments */
		if (strcmp(*argv, "-alpha") == 0)
			args->starting_alpha = args->alpha = atof(*++argv);
		if (strcmp(*argv, "-sample") == 0)
			args->sample = atof(*++argv);
		if (strcmp(*argv, "-beta-strong") == 0)
			args->beta_strong = atof(*++argv);
		if (strcmp(*argv, "-beta-weak") == 0)
			args->beta_weak = atof(*++argv);
	}
}

int main(int argc, char **argv)
{
	char spairs_file[MAXLEN], wpairs_file[MAXLEN];
	int i;
	float d;
	pthread_t *threads;

	/* no arguments given. Print help and exit */
	if (argc == 1)
	{
		print_help();
		return 0;
	}

	parse_args(argc, argv, &args, spairs_file, wpairs_file);

	if (strlen(args.input) == 0)
	{
		printf("Cannot train the model without: -input <file>\n");
		exit(1);
	}

	/* initialise vocabulary table */
	vocab = calloc(vocab_max_size, sizeof(struct entry));

	/* initialise the sigmoid table. The array represents the values of the
	sigmoid function from -MAX_SIGMOID to MAX_SIGMOID. The array
	contains SIGMOID_SIZE cells so each cell i contains the sigmoid of :

	X = ( (-SIGMOID_SIZE + (i * 2)) / SIGMOID_SIZE ) * MAX_SIGMOID

	  for i = [0 .. SIGMOID_SIZE]

	One can verify that X goes from -MAX_SIGMOID to MAX_SIGMOID */

	/* faster to multiply than divide so pre-compute 1/ SIGMOID_SIZE */
	d = 1 / (float) SIGMOID_SIZE;
	for (i = 0; i < SIGMOID_SIZE ; ++i)
	{
		/* start by computing exp(X) */
		sigmoid[i] = exp( (((i * 2) - SIGMOID_SIZE) * d) * MAX_SIGMOID);
		/* then compute sigmoid(X) = exp(X) / (1 + exp(X)) */
		sigmoid[i] = sigmoid[i] / (sigmoid[i] + 1);
	}

	/*********** train ***/
	/* variable for future use */

	if ((threads = calloc(args.num_threads, sizeof *threads)) == NULL)
	{
		printf("Cannot allocate memory for threads\n");
		exit(1);
	}

	/* get words from input file */
	printf("Starting training using file %s\n", args.input);
	read_vocab(args.input, spairs_file, wpairs_file);

	/* instantiate the network */
	init_network();

	/* instantiate negative table (for negative sampling) */
	if (args.negative > 0)
		init_negative_table();

	/* train the model for multiple epoch */
	start = clock();
	for (current_epoch = 0; current_epoch < args.epoch; current_epoch++)
	{
		printf("\n-- Epoch %d/%d\n", current_epoch+1, args.epoch);

		/* create threads */
		for (i = 0; i < args.num_threads; i++)
			pthread_create(&threads[i], NULL, train_thread,
					(void *) (intptr_t) i);

		/* wait for threads to join. When they join, epoch is finished
		 */
		for (i = 0; i < args.num_threads; i++)
			pthread_join(threads[i], NULL);

		if (args.save_each_epoch)
		{
			printf("\nSaving vectors for epoch %d.", current_epoch+1);
			save_vectors(args.output, current_epoch+1);
		}

	}

	/* save the file only if we didn't save it earlier with the
	 * save-each-epoch option */
	if (!args.save_each_epoch)
	{
		printf("\n-- Saving word embeddings\n");
		save_vectors(args.output, -1);
	}

	free(threads);
	destroy_vocab();

	/******** end train ****/

	destroy_network();

	return 0;
}
