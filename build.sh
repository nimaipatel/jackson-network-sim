#!/bin/sh

set -xe

CC=clang
FLAGS="-lm"
DEV_FLAGS="-Wall -Wextra -Wpedantic -fsanitize=address,undefined -fsanitize-trap -Wconversion -Wdouble-promotion -Wno-unused-parameter -Wno-unused-function -g3"

$CC $FLAGS $DEV_FLAGS ./main.c -o ./main
