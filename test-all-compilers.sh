#!/bin/sh
make -f Makefile.tcc EXTRAFLAGS=-DSD_DEBUG_GC clean all test && \
   make -f Makefile.msvc EXTRAFLAGS=-DSD_DEBUG_GC clean all test && \
   make -f Makefile.msvc64 EXTRAFLAGS=-DSD_DEBUG_GC clean all test && \
   make -f Makefile.lcc EXTRAFLAGS=-DSD_DEBUG_GC clean all test && \
   make -f Makefile.bcc32 EXTRAFLAGS=-DSD_DEBUG_GC clean all test && \
   make -f Makefile.watcom EXTRAFLAGS=-DSD_DEBUG_GC clean all test && \
   make -f Makefile.emcc EXTRAFLAGS=-DSD_DEBUG_GC clean all test && \
   make EXTRAFLAGS=-DSD_DEBUG_GC clean all test 
