# CC may be overridden on the command line.  Try gcc or tcc.
CC=gcc

ifeq ($(CC),gcc)
	CFLAGS=-ansi -pedantic -Wall -Werror -O2 -x c
else
	CFLAGS=
endif

ifeq ($(CC),gcc)
	LFLAGS=-lm
else
	LFLAGS=
endif

SAD_SOURCES=src/sad.c src/sad-script.c

SAD_TEST_SOURCES=src/sad-test.c

TESTS=$(wildcard tests/*.sad)
TESTRESULTS=$(TESTS:tests/%.sad=testresults/%.testresult)

all: bin/sad bin/sad-test

bin/sad: bin bin/prelude.sad $(SAD_SOURCES)
	$(CC) $(CFLAGS) $(LFLAGS) $(SAD_SOURCES) -o $@

bin/sad-test: bin bin/sad $(SAD_TEST_SOURCES)
	$(CC) $(CFLAGS) $(LFLAGS) $(SAD_TEST_SOURCES) -o $@

bin/prelude.sad: src/prelude.sad
	cp src/prelude.sad bin/prelude.sad

bin:
	mkdir bin

testresults:
	mkdir testresults

cleantests:
	@rm -f $(TESTRESULTS)

clean: cleantests
	@rm -f bin/sad bin/sad.exe
	@rm -f bin/sad-test bin/sad-test.exe
	@rm -f bin/prelude.sad

test: cleantests testresults bin/sad bin/sad-test bin/prelude.sad $(TESTRESULTS)

$(TESTRESULTS): 
	@echo "-----------------------------------------------------"
	@echo -n "$(@:testresults/%.testresult=%): "
	@bin/sad --prelude bin/prelude.sad $(@:testresults/%.testresult=tests/%.sad) > $@
	@bin/sad-test $(@:testresults/%.testresult=tests/%.sad) $@
