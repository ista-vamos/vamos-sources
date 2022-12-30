#!/bin/bash

set -e
set -x

DIR="$(dirname 0)"
SHAMONDIR="$DIR/../.."
LLVM_PASS_DIR=$DIR/../llvm
CFLAGS="-DDEBUG_STDOUT -g" #-O3
SHAMON_INCLUDES=-I$DIR/../../
SHAMON_LIBS="$SHAMONDIR/core/libshamon-arbiter.a\
             $SHAMONDIR/shmbuf/libshamon-shmbuf.a\
             $SHAMONDIR/core/libshamon-utils.a\
             $SHAMONDIR/core/libshamon-stream.a\
             $SHAMONDIR/core/libshamon-parallel-queue.a\
             $SHAMONDIR/core/signatures.c\
             $SHAMONDIR/streams/libshamon-streams.a"


clang $CFLAGS -emit-llvm -S -fsanitize=thread $@ -o code.ll
opt -enable-new-pm=0 -load $LLVM_PASS_DIR/race-instrumentation.so -vamos-race-instrumentation code.ll -o code-instr.bc

clang $CFLAGS $SHAMON_INCLUDES -std=c11 -emit-llvm -S tsan_impl.c
llvm-link tsan_impl.ll code-instr.bc -o code-linked.bc
llvm-dis code-linked.bc

clang -pthread $CFLAGS code-linked.ll $SHAMON_LIBS
