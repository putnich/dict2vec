#!/usr/bin/env python3
#
# Copyright (c) 2017-present, All rights reserved.
# Written by Julien Tissier <30314448+tca19@users.noreply.github.com>
#
# This file is part of Dict2vec.
#
# Dict2vec is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Dict2vec is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License at the root of this repository for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with Dict2vec.  If not, see <http://www.gnu.org/licenses/>.

import os
import sys
import math
import argparse
import glob
import numpy as np
import scipy.stats as st
import requests
import bz2
from bs4 import BeautifulSoup as bs

FILE_DIR = "data/eval/"
results      = dict()
missed_pairs = dict()
missed_words = dict()
imprs        = dict()

def tanimotoSim(v1, v2):
    """Return the Tanimoto similarity between v1 and v2 (numpy arrays)"""
    dotProd = np.dot(v1, v2)
    return dotProd / (np.linalg.norm(v1)**2 + np.linalg.norm(v2)**2 - dotProd)


def cosineSim(v1, v2):
    """Return the cosine similarity between v1 and v2 (numpy arrays)"""
    dotProd = np.dot(v1, v2)
    return dotProd / (np.linalg.norm(v1) * np.linalg.norm(v2))

def make_synonyms():
    """Download synonyms"""
    url = 'https://github.com/putnich/sr-sh-nlp/blob/main/SH-Wiktionary/SH-Wiktionary-Synonyms.xml.bz2?raw=true'
    path = 'data/SH-Wiktionary-Synonyms.xml'
    synonyms_weak_path = FILE_DIR + 'synonyms-weak.txt'
    synonyms_strong_path = FILE_DIR + 'synonyms-strong.txt'

    if os.path.exists(synonyms_weak_path) and os.path.exists(synonyms_strong_path):
        return
    
    with open(glob.glob('data/strong-pairs*')[0], 'r', encoding='utf-8') as f:
        strong_pairs = f.read().split('\n')
    
    with open(glob.glob('data/weak-pairs*')[0], 'r', encoding='utf-8') as f:
        weak_pairs = f.read().split('\n')

    if not os.path.exists(path):
        result = requests.get(url, allow_redirects=True)
        obj = bz2.BZ2Decompressor()
        with open(path, 'wb') as f:
            f.write(obj.decompress(result.content))
    
    """Generate synonyms list"""
    with open(path, 'r', encoding='utf-8') as f:
        soup = bs(f.read(), 'html.parser')
    entries = soup.find_all('entry')
    synonyms_strong = list()
    synonyms_weak = list()
    with open('dict-dl/5000-words.txt', 'r', encoding='utf-8') as f:
        words = f.read().split('\n')
    for entry in entries:
        head = entry.find('orth').text.strip()
        if not head in words:
            continue
        xrs = entry.find_all('xr', {'type': 'synonymy'})
        for xr in xrs:
            innerlinks = xr.find_all('innerlink')
            synonym = ''.join([i.text.strip() for i in innerlinks])
            if head == synonym:
                continue
            pair = head + ' ' + synonym
            if synonym in words and pair not in synonyms_strong and pair in strong_pairs:
                synonyms_strong.append(pair)
            if synonym in words and pair not in synonyms_weak and pair in weak_pairs:
                synonyms_weak.append(pair)
    with open(synonyms_strong_path, 'w+', encoding='utf-8') as f:
        f.write('\n'.join(synonyms_strong))
    with open(synonyms_weak_path, 'w+', encoding='utf-8') as f:
        f.write('\n'.join(synonyms_weak))

def init_results():
    """Read the filename for each file in the evaluation directory"""
    for filename in os.listdir(FILE_DIR):
        if not filename in results:
            results[filename] = []

def evaluate(filenames):
    models = dict()
    for filename in filenames:
        model = dict()
        """Compute Cosine similarity per each file and model"""

        # step 0 : read the first line to get the number of words and the dimension
        nb_line = 0
        nb_dims = 0
        with open(filename, encoding='utf-8') as f:
            line = f.readline().split()
            nb_line = int(line[0])
            nb_dims = int(line[1])

        mat = np.zeros((nb_line, nb_dims))
        wordToNum = {}
        count = 0

        with open(filename, encoding='utf-8') as f:
            f.readline() # skip first line because it does not contains a vector
            for line in f:
                line = line.split()
                word, vals = line[0], list(map(float, line[1:]))
                # if number of vals is different from nb_dims, bad vector, drop it
                if len(vals) != nb_dims:
                    continue
                mat[count] = np.array(vals)
                wordToNum[word] = count
                count += 1
        model['mat'] = mat
        model['wordToNum'] = wordToNum
        models[filename] = model
    # step 1 : iterate over each evaluation data file and compute cosine
    for filename in results:
        pairs_not_found, total_pairs = 0, 0
        words_not_found, total_words = 0, 0
        total_imprs = 0
        imprs_count = 0
        with open(os.path.join(FILE_DIR, filename), encoding='utf-8') as f:
            for line in f:
                w1, w2 = line.split()
                w1, w2 = w1.lower(), w2.lower()
                total_words += 2
                total_pairs += 1
                cosines = list()
                for key in models.keys():
                    if not w1 in models[key]['wordToNum']:
                        words_not_found += 1
                    if not w2 in models[key]['wordToNum']:
                        words_not_found += 1

                    if not w1 in models[key]['wordToNum'] or not w2 in models[key]['wordToNum']:
                        pairs_not_found += 1
                    else:
                        v1, v2 = models[key]['mat'][models[key]['wordToNum'][w1]], models[key]['mat'][models[key]['wordToNum'][w2]]
                        cosine = cosineSim(v1, v2)
                        cosines.append(cosine)
                        #tanimoto = tanimotoSim(v1, v2)
                        #file_similarity.append(val)
                        #embedding_similarity.append(tanimoto)
                if cosines:
                    total_imprs += int(cosines[1] >= cosines[0])
                    imprs_count += 1
            missed_pairs[filename] = (pairs_not_found, total_pairs)
            missed_words[filename] = (words_not_found, total_words)
            imprs[filename]        = (total_imprs, imprs_count)


def stats():
    """Compute statistics on results"""
    title = "{}| {}| {} ".format("Filename".ljust(20), "Missed words/pairs".center(20), "Average improvement".rjust(16))
    print(title)
    print("="*len(title))

    for filename in sorted(results.keys()):

        # total amount of the improvement
        total_imprs = str(round(imprs[filename][0] / imprs[filename][1] * 100, 2))

        # ratio = number of missed / total
        ratio_words = missed_words[filename][0] / missed_words[filename][1]
        ratio_pairs = missed_pairs[filename][0] / missed_pairs[filename][1]
        missed_infos = "{:.2f}% / {:.2f}%".format(
                round(ratio_words*100, 2), round(ratio_pairs*100, 2))

        print("{}| {}| {}%".format(
              filename.ljust(20),
              missed_infos.center(20),
              total_imprs.rjust(16)))


if __name__ == '__main__':

    parser = argparse.ArgumentParser(
             description="Evaluate semantic similarities of word embeddings.",
             )

    parser.add_argument('-w2v', '--word2vec', help="""File containing Word2Vec model.""", required=True)
    parser.add_argument('-d2v', '--dict2vec', help="""File containing Dict2Vec model.""", required=True)
    args = parser.parse_args()

    make_synonyms()
    init_results()
    evaluate([args.word2vec, args.dict2vec])
    stats()
