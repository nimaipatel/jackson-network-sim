#!/bin/sh

set -xe

CC=gcc
CFLAGS="-Wall -Wextra -Wpedantic -g"

$CC $CFLAGS ./main.c -o ./main
