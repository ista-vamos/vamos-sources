#!/bin/bash

cd $(dirname $0)

./sendaddr /addr1 $1&
./sendaddr /addr2 $1&

wait
wait
