#!/bin/sh
make -f Makefile.tcc clean all test && \
   make clean all test && \
   make -f Makefile.msvc clean all test && \
   make -f Makefile.lcc clean all test && \
   make -f Makefile.bcc32 clean all test && \
   make -f Makefile.watcom clean all test