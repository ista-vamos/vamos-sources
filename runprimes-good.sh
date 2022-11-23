#!/bin/bash

cat primes.txt | ./regex /primes1 prime "([0-9]+)\s*([0-9]+)" dd&
cat primes.txt | ./regex /primes2 prime "([0-9]+)\s*([0-9]+)" dd&

wait
wait
