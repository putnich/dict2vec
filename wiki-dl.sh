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

#echo "Downloading the SH Wikipedia dump (December 2020)"
URL=https://dumps.wikimedia.org/shwiki/20201220/shwiki-20201220-pages-articles.xml.bz2
time wget -qO- $URL | bzip2 -d | perl wiki-parser.pl > "$DATA_DIR/shwiki-full"
echo "Done."
echo

echo "Creating shwiki-10M and shwiki-50M..."
head -c 65536000 "$DATA_DIR/shwiki-full" > "$DATA_DIR/shwiki-10M"
head -c 325058560 "$DATA_DIR/shwiki-full" > "$DATA_DIR/shwiki-50M"
echo "Done."
echo
