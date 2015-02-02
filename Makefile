# CC may be overridden on the command line.  Try 'gcc', 'tcc', and 'lc'.  On Windows try 'msvc' to use the VS 2013 compiler.
CC=gcc
CC2=$(CC)
CFLAGS=
LFLAGS=
SEP=/

ifeq ($(CC),gcc)
	CFLAGS=-ansi -pedantic -Wall -Wextra -Werror -O2 -x c
	LFLAGS=-lm
endif

ifeq ($(CC),msvc)
	CC2=cmd /K msvc-cl.bat
	CFLAGS=
	LFLAGS=
endif

ifeq ($(CC),lc)
	CFLAGS=-A -shadows -unused
	SEP=\\
endif

SAD_SOURCES=src/sad.c src/sad-script.c
SAD_TEST_SOURCES=src/sad-test.c

TESTS=$(wildcard tests/*.sad)
TESTRESULTS=$(TESTS:tests/%.sad=testresults/%.testresult)

all: bin/sad bin/sad-test

bin/sad: bin bin/prelude.sad $(SAD_SOURCES)
	$(CC2) $(CFLAGS) $(LFLAGS) $(subst /,$(SEP),$(SAD_SOURCES)) -o $@
	@rm -f *.obj

bin/sad-test: bin bin/sad $(SAD_TEST_SOURCES)
	$(CC2) $(CFLAGS) $(LFLAGS) $(subst /,$(SEP),$(SAD_TEST_SOURCES)) -o $@
	@rm -f *.obj

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
	-@bin/sad --prelude bin/prelude.sad $(@:testresults/%.testresult=tests/%.sad) > $@ 2> $@.err
	@cat $@.err >> $@
	@rm $@.err
	@bin/sad-test $(@:testresults/%.testresult=tests/%.sad) $@


#OBJ_FILES_CORRECT=$(subst \,/,$(SAD_SOURCES))
