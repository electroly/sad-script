#!/bin/sh
make -f Makefile.tcc EXTRAFLAGS=-DNDEBUG clean all test && \
   make -f Makefile.msvc EXTRAFLAGS=-DNDEBUG clean all test && \
   make -f Makefile.msvc64 EXTRAFLAGS=-DNDEBUG clean all test && \
   make -f Makefile.lcc EXTRAFLAGS=-DNDEBUG clean all test && \
   make -f Makefile.bcc32 EXTRAFLAGS=-DNDEBUG clean all test && \
   make -f Makefile.watcom EXTRAFLAGS=-DNDEBUG clean all test && \
   make -f Makefile.emcc EXTRAFLAGS=-DNDEBUG clean all test && \
   make EXTRAFLAGS=-DNDEBUG clean all test 
