#!/bin/bash

if [ $# -gt 1 ]; then
	python ../spec/main.py $1 | tail -n $2 > /tmp/$1.out.txt
else
	python ../spec/main.py $1 > /tmp/$1.out.txt
fi

diff $1.out.txt /tmp/$1.out.txt
