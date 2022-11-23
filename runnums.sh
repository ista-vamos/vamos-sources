#!/bin/bash

SOURCESDIR="$(pwd)/../sources"
REGEXSOURCE=$SOURCESDIR/regex
NUM=$1
test -z $NUM && NUM=10000
seq 1 $NUM | $REGEXSOURCE /nums1 num "([0-9]+)" i&
seq 1 $NUM | $REGEXSOURCE /nums2 num "([0-9]+)" i&

wait
wait
