#!/bin/bash

set -x

sudo apt-get install libtool autotools-dev automake flex bison

libtoolize
aclocal
autoheader
automake --add-missing
autoconf

./configure
make

set +x
