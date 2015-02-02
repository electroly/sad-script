#!/bin/sh
make CC=tcc clean all test && make CC=gcc clean all test && make CC=msvc clean all test && make CC=lc clean all test
