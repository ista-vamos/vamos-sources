#!/bin/bash

PYTHONPATH=$(pwd)/../python
export PYTHONPATH

if [ $# -gt 1 ]; then
	python "$PYTHONPATH"/vamos_sources/spec/main.py $1 | tail -n $2 > /tmp/$1.out.txt
else
	python "$PYTHONPATH"/vamos_sources/spec/main.py $1 > /tmp/$1.out.txt
fi

diff $1.out.txt /tmp/$1.out.txt
