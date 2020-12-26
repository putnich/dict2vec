dict-dl
=======

dict-dl is a tool from Dict2vec to download online lexical word definitions, and
generate strong and weak pairs from these definitions.  The dictionaries used to
download definitions are :

  * [Online recnik](https://recnik.rs/leksikon/srpski/)


Requirements
------------

To use scripts in this folder, you will need :

  * python3
  * numpy (python3 version)


Download definitions
--------------------

To download online definitions, you first need a file containing a list of words
(one per line). The script will download the definitions for each of these
words. We provide an example with the file 5000-words.txt.

Then run `download_definitions.py` with the filename of the file containing the
list of words, like :

```bash
$ ./download_definitions.py 5000-words.txt
```

At this moment we do not deal with the PoS tag.

All the definitions for each word will be downloaded,
regardless of the sense.

This will write all the fetched definitions in the file
5000-words-definitions.txt. Each line will look like this :

```
            XXX     word    def1 def2 def3 def4 def5 def6
             ^       ^      \___________________________/
             1       2                   3

        1: code representing the dictionary the definition comes from.

                          Ors -> Online recnik

        2: the fetched word.
        3: words from the definition. Stopwords have been removed. All words
           are lowercased.
```

It is possible that you see :

```
ERROR: * timeout error.
       * retry Online recnik - natural
```

during the execution of the script. This means the server was not able to
respond to the HTTP request, so we can not have the definition for this word.
The error message tells you the word and the dictionary that were faulty, so
you can redownload the definition manually. This error happens rarely and is
only dependent on the web server load.


Clean definitions
-----------------

Fetched definitions are not regrouped and can contain words that are not in
the vocabulary. To clean the definitions (remove words we don't want and
regroup the 4 definitions together for each word), run :

```bash
$ ./clean_definitions.py -d 5000-words-definitions.txt -v 5000-words.txt
```

#### Options

```
 -d, --definitions  FILE         File containing the fetched definitions from
                                 previous script
 -v, --vocab FILE                Remove all words that are not in this
                                 vocabulary file
```

This will produce a file named all-definitions-cleaned.txt, where the first
word of each line is the fetched word, and the rest of the line are the words
from all its definitions.


Generate strong/weak pairs
--------------------------

To generate strong and weak pairs, you need pre-trained word embeddings to
compute the K closest neighbours (used to form additional strong pairs). Then
run :

```bash
$ ./generate_pairs.py -d all-definitions-cleaned.txt -e vectors.vec -K 5
```

#### Options

```
 -d, --definitions FILE          File containing word definitions
 -e, --embedding FILE            File containing the word embeddings used to
                                 compute the K closest neighbours
 -K NUMBER                       Number of artificially generated strong pairs
                                 for each real strong pairs
 -sf, --strong-file FILE         Output filename for saving strong pairs
 -wf, --weak-file FILE           Output filename for saving weak pairs
```
