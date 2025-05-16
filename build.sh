#!/bin/bash
set -e

make distclean
./cpop-config.sh x86_64
make -j${nproc} || exit 255
make install
