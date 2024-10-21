#!/bin/sh

set -xe

CC=gcc
FLAGS="-Wall -Wextra -Wpedantic -fsanitize=address,undefined -fsanitize-trap -Wconversion -Wdouble-promotion -Wno-unused-parameter -Wno-unused-function -g3"

$CC $FLAGS ./main.c -o ./main
