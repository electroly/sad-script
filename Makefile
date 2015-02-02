# CC may be overridden on the command line.  Try 'gcc', 'tcc', 'lc', 'bcc32', and 'msvc'.
CC=gcc
CC2=$(CC)
CFLAGS=
LFLAGS=
SEP=/
SAD_OUT_FLAG=-o bin/sad
SAD_TEST_OUT_FLAG=-o bin/sad-test

ifeq ($(CC),gcc)
	CFLAGS=-ansi -pedantic -Wall -Wextra -Werror -O2 -x c
	LFLAGS=-lm
endif

ifeq ($(CC),msvc)
	CC2=cmd /K msvc-cl.bat
	CFLAGS=
	LFLAGS=
	SAD_OUT_FLAG=-Febin/sad
	SAD_TEST_OUT_FLAG=-Febin/sad-test
endif

ifeq ($(CC),lc)
	CFLAGS=-A -shadows -unused
	SEP=\\
endif

ifeq ($(CC),bcc32)
	CFLAGS=-q -Q -w -w! -w-8004 -w-8073 -A -tWC -v -O2 -IC:/Borland/BCC55/Include -nbin
	LFLAGS=-LC:/Borland/BCC55/Lib
	SAD_OUT_FLAG=-obin/sad
	SAD_TEST_OUT_FLAG=-obin/sad-test
endif

SAD_SOURCES=src/sad.c src/sad-script.c
SAD_TEST_SOURCES=src/sad-test.c

TESTS=$(wildcard tests/*.sad)
TESTRESULTS=$(TESTS:tests/%.sad=testresults/%.testresult)

all: bin/sad bin/sad-test

bin/sad: bin bin/prelude.sad $(SAD_SOURCES)
	$(CC2) $(CFLAGS) $(LFLAGS) $(subst /,$(SEP),$(SAD_SOURCES)) $(SAD_OUT_FLAG)
	-@rm -f *.obj

bin/sad-test: bin bin/sad $(SAD_TEST_SOURCES)
	$(CC2) $(CFLAGS) $(LFLAGS) $(subst /,$(SEP),$(SAD_TEST_SOURCES)) $(SAD_TEST_OUT_FLAG)
	-@rm -f *.obj

bin/prelude.sad: src/prelude.sad
	cp src/prelude.sad bin/prelude.sad

bin:
	mkdir bin

testresults:
	mkdir testresults

cleantests:
	@rm -f testresults/*

clean: cleantests
	@rm -f bin/*

test: cleantests testresults bin/sad bin/sad-test bin/prelude.sad $(TESTRESULTS)

$(TESTRESULTS): 
	@echo $(@:testresults/%.testresult=%)
	-@bin/sad --prelude bin$(SEP)prelude.sad $(subst /,$(SEP),$(@:testresults/%.testresult=tests/%.sad)) > $@ 2> $@.err
	@cat $@.err >> $@
	@rm $@.err
	@bin/sad-test $(@:testresults/%.testresult=tests/%.sad) $@


#OBJ_FILES_CORRECT=$(subst \,/,$(SAD_SOURCES))
