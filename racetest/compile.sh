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

OPT=opt
CLANG=clang
LINK=llvm-link
DIS=llvm-dis

$CLANG $CFLAGS -emit-llvm -S -fsanitize=thread $@ -o code.ll
$OPT -enable-new-pm=0 -load $LLVM_PASS_DIR/race-instrumentation.so -vamos-race-instrumentation code.ll -o code-instr.bc

$CLANG $CFLAGS $SHAMON_INCLUDES -std=c11 -emit-llvm -S tsan_impl.c
$LINK tsan_impl.ll code-instr.bc -o code-linked.bc
$DIS code-linked.bc || echo "llvm-dis does not work, ignoring"

clang -pthread $CFLAGS code-linked.bc $SHAMON_LIBS
