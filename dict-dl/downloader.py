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

import re
import requests
from bs4 import BeautifulSoup


def download_online_recnik(word):
    URL = 'https://recnik.rs/leksikon/srpski/' + word

    try:
        html_page = requests.get(URL)
        soup = BeautifulSoup(html_page.content, 'html.parser')
        defs = []
        for h3 in soup.find('div', {'id': 'main'}).find_all('h3'):
            defs.append(h3.find_next_sibling().text)
        return defs
    except UnicodeDecodeError:
        return -1
    except Exception as e:
        print("\nERROR: * timeout error.")
        print("       * retry Online recnik -", word)
        return -1

MAP_DICT = {
    "Ors": download_online_recnik
}

# STOPSWORD = set()
# with open('stopwords.txt') as f:
#     for line in f:
#         STOPSWORD.add(line.strip().lower())


def download_word_definition(dict_name, word, clean=True):
    """
    Download the definition(s) for word from the dictionary dict_name. If clean
    is True, clean the definition before returning it (remove stopwords and non
    letters characters in definition).
    """
    words = []
    download = MAP_DICT[dict_name]
    res = download(word)
    if res == -1:  # no definition fetched
        res = []

    for definition in res:  # there can be more than one definition fetched
        for word in definition.split():
            word = ''.join([c.lower() for c in word
                            if c.isalpha()])
            # if not word in STOPSWORD:
            words.append(word)

    return words


if __name__ == '__main__':
    print("-- TEST : definitions of kosa --")
    print("Online recnik")
    print(download_online_recnik("kosa"))
