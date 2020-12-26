#!/bin/bash
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

DATA_DIR=./data
DICT_DIR=./dict-dl
W2V_PATH=$DATA_DIR/w2v.vec
D2V_PATH=$DATA_DIR/d2v.vec

# hyperparameters
VECTOR_SIZE=100
WINDOW_SIZE=5
NEGATIVE=5
NB_STRONG=4
NB_WEAK=5
BETA_STRONG=0.8
BETA_WEAK=0.45
THREADS=8
EPOCH=5
K=5

# download training file if not already downloaded
TEXT_DATA=shwiki-50M
# if [ ! -e "$DATA_DIR/$TEXT_DATA" ]; then
#   echo "Downloading training file $TEXT_DATA..."
#   wget -qO- https://dict2vec.s3.amazonaws.com/enwiki-50M.tar.bz2 \
#   | tar -jxv -C $DATA_DIR
#   echo "Done."
#   echo
# fi

# download strong pairs if not already downloaded
STRONG_PAIRS=strong-pairs.txt
# if [ ! -e "$DATA_DIR/$STRONG_PAIRS" ]; then
#   echo "Downloading file $STRONG_PAIRS..."
#   wget -qO- https://dict2vec.s3.amazonaws.com/strong-pairs.tar.bz2 \
#   | tar -jxv -C $DATA_DIR
#   echo "Done."
#   echo
# fi

# download weak pairs if not already downloaded
WEAK_PAIRS=weak-pairs.txt
# if [ ! -e "$DATA_DIR/$WEAK_PAIRS" ]; then
#   echo "Downloading file $WEAK_PAIRS..."
#   wget -qO- https://dict2vec.s3.amazonaws.com/weak-pairs.tar.bz2 \
#   | tar -jxv -C $DATA_DIR
#   echo "Done."
#   echo
# fi


# remove existing trained vectors
if [ -e "$W2V_PATH" ]; then
  rm "$W2V_PATH"
fi

# # remove existing trained vectors
if [ -e "$D2V_PATH" ]; then
  rm "$D2V_PATH"
fi

make

# train word2vec
# executable adds the .vec extension when saving vectors so don't worry if
# -output is the same as -input
echo
echo --------------- Training vectors... ---------------
time ./dict2vec -input "$DATA_DIR/$TEXT_DATA" -output "$DATA_DIR/w2v" \
     -size $VECTOR_SIZE -window $WINDOW_SIZE -negative $NEGATIVE \
     -strong-draws $NB_STRONG -weak-draws $NB_WEAK \
     -beta-strong $BETA_STRONG  -beta-weak $BETA_WEAK \
     -sample 1e-4 -threads $THREADS -epoch $EPOCH #-save-each-epoch 1

# download definitions
echo
echo -------------- Downloading definitions... --------------
./$DICT_DIR/download_definitions.py "$DICT_DIR/5000-words.txt"

# clean definitions
echo
echo -------------- Cleaning definitions... --------------
./$DICT_DIR/clean_definitions.py -d "$DICT_DIR/5000-words-definitions.txt" -v "$DICT_DIR/5000-words.txt"

# generate pairs
echo
echo -------------- Generating pairs... --------------
./$DICT_DIR/generate_pairs.py -d "$DATA_DIR/all-definitions-cleaned.txt" -e "$DATA_DIR/w2v.vec" -wf "$DATA_DIR/$WEAK_PAIRS" -sf "$DATA_DIR/$STRONG_PAIRS" -K $K

# train dict2vec
# executable adds the .vec extension when saving vectors so don't worry if
# -output is the same as -input
echo
echo --------------- Training vectors... ---------------
time ./dict2vec -input "$DATA_DIR/$TEXT_DATA" -output "$DATA_DIR/d2v" \
     -strong-file "$DATA_DIR/$STRONG_PAIRS-K$K.txt" -weak-file "$DATA_DIR/$WEAK_PAIRS-K$K.txt" \
     -size $VECTOR_SIZE -window $WINDOW_SIZE -negative $NEGATIVE \
     -strong-draws $NB_STRONG -weak-draws $NB_WEAK \
     -beta-strong $BETA_STRONG  -beta-weak $BETA_WEAK \
     -sample 1e-4 -threads $THREADS -epoch $EPOCH #-save-each-epoch 1

# evaluate the trained embeddings
echo
echo -------------- Evaluating vectors... --------------
./evaluate.py -w2v $W2V_PATH -d2v $D2V_PATH

