# GCC compiler (ends up being clang on OS X)
# Use one of the other makefiles to use other compilers. (make -f Makefile.ext)

CC=gcc
COMPILER=GCC
CFLAGS=-ansi -pedantic -Wall -Wextra -Werror -O2 -x c
LFLAGS=-lm
SEP=/
SAD_OUT_FLAG=-o bin/sad
SAD_TEST_OUT_FLAG=-o bin/sad-test
SAD_RUN=bin/sad
SAD_TEST_RUN=bin/sad-test

include Makefile.base
