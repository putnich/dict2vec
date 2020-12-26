Dict2vec
========

Dict2vec is a framework to learn word embeddings using lexical dictionaries.

Requirements
------------

To compile and run our Dict2vec model, you will need:

  * gcc (4.8.4 or newer)
  * make

To evaluate the learned embeddings on the word similarity task, you will need:

  * python3
  * numpy (python3 version)
  * scipy (python3 version)

To fetch definitions from online dictionaries, you will need:

  * python3

To run demo scripts and download training data, you will also need a machine
with **wget**, **bzip2**, **perl** and **bash** installed.

Run the code
------------

Before running the example script, open `demo-train.sh` and modify line 62
so the variable THREADS is equal to the number of cores in your machine. By
default, it is equal to 8, so if your machine only has 4 cores, update it to be:

```
THREADS=4
```

Then run `demo-train.sh` to have a quick glimpse of Dict2vec performances.

```bash
$ ./demo-train.sh
```

This will:

  * download a training file of 50M words
  * download strong and weak pairs for training
  * compile Dict2vec source code into a binary executable
  * train word embeddings with a dimension of 100
  * evaluate the embeddings on synonyms

To directly compile the code and interact with the software, run:

```bash
$ make
$ ./dict2vec
```

**_Full documentation of each possible parameters is displayed when you run_**
`./dict2vec` **_without any arguments._**


Evaluate word embeddings
------------------------

Run `evaluate.py` to evaluate a trained word embedding. 
The evaluation is performed on a set of synonyms, parsed from Serbo-Croatian Wiktionary 
and downloaded as a TEI Lex0 specified XML structure. 
We load all synonyms but select only those appearing in strong or weak pairs. 


Once the evaluation is done, you get something like this:

```bash
./evaluate.py -w2v "data/w2v.vec" -d2v "data/d2v.vec"
Filename            |  Missed words/pairs | Average improvement 
================================================================
synonyms-strong.txt |   32.20% / 49.60%   |            69.68%
synonyms-weak.txt   |   33.71% / 57.51%   |            54.67%
```

The script computes cosine similarities between synonym pairs using
both Word2Vec and Dict2Vec. The average improvement gives the percentage 
of synonym pairs whose similarity score increased when using Dict2Vec. 


Download more data
------------------

### Wikipedia

You can generate the same 3 files (50M, 200M and full) we use for training in
the paper by running the script `wiki-dl.sh`.

```bash
$ ./wiki-dl.sh
```

This script will download the full Serbo-Croatian Wikipedia dump of December 2020,
uncompress it and directly feed it into [Mahoney's parser
script](http://mattmahoney.net/dc/textdata#appendixa). It also cuts the entire
dump into two smaller datasets: one containing the first 10M tokens
(shwiki-10M), and the other one containing the first 50M tokens (shwiki-50M). 
The full Wikipedia contains around 130M tokens.
We report the following filesizes:

  * shwiki-10M:  65.5MB
  * shwiki-50M: 325.1MB
  * shwiki-full: 863.1MB


Cite this paper
---------------

Please cite this paper if you use our code to learn word embeddings or download
definitions or use our pre-trained word embeddings.

J. Tissier, C. Gravier, A. Habrard, *Dict2vec : Learning Word Embeddings using
Lexical Dictionaries*

```
@inproceedings{tissier2017dict2vec,
  title     = {Dict2vec : Learning Word Embeddings using Lexical Dictionaries},
  author    = {Tissier, Julien and Gravier, Christophe and Habrard, Amaury},
  booktitle = {Proceedings of the 2017 Conference on Empirical Methods in Natural Language Processing},
  pages     = {254--263},
  year      = {2017}
}
```


License
-------

This project is licensed under the GNU GPL v3 license. See the
[LICENSE](LICENSE) file for details.
