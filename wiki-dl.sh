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

#echo "Downloading SH Wikipedia dump of October 2020..."
URL=https://dumps.wikimedia.org/shwiki/latest/shwiki-latest-pages-articles-multistream.xml.bz2
time wget -qO- $URL | bzip2 -d | perl wiki-parser.pl > "$DATA_DIR/shwiki-full"
echo "Done."
echo

echo "Creating shwiki-50M and shwiki-200M..."
head -c 295988890 "$DATA_DIR/shwiki-full" > "$DATA_DIR/shwiki-50M"
head -c 1164667415 "$DATA_DIR/shwiki-full" > "$DATA_DIR/shwiki-200M"
echo "Done."
echo
