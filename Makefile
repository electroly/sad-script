CC=gcc
CFLAGS=-ansi -pedantic -Wall -Werror -O2 -x c

LINKER=gcc -o
LFLAGS=-lm

SAD_SOURCES=src/sad.c src/sad-script.c
SAD_OBJECTS=$(SAD_SOURCES:src/%.c=obj/%.o)

SAD_TEST_SOURCES=src/sad-test.c
SAD_TEST_OBJECTS=$(SAD_TEST_SOURCES:src/%.c=obj/%.o)

TESTS=$(wildcard tests/*.sad)
TESTRESULTS=$(TESTS:tests/%.sad=obj/%.testresult)

all: bin/sad bin/sad-test

bin/sad: obj bin $(SAD_OBJECTS) bin/prelude.sad
	$(LINKER) $@ $(LFLAGS) $(SAD_OBJECTS)

bin/sad-test: obj bin bin/sad $(SAD_TEST_OBJECTS)
	$(LINKER) $@ $(LFLAGS) $(SAD_TEST_OBJECTS)

obj/%.o : src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

bin/prelude.sad:
	cp src/prelude.sad bin/prelude.sad

obj:
	mkdir obj

bin:
	mkdir bin

cleantests:
	@rm -f $(TESTRESULTS)

clean: cleantests
	@rm -f $(SAD_OBJECTS)

distclean: clean
	@rm -f bin/sad bin/sad.exe
	@rm -f bin/prelude.sad

tests: cleantests bin/sad bin/sad-test $(TESTRESULTS)

$(TESTRESULTS): 
	@bin/sad $(@:obj/%.testresult=tests/%.sad) > $@
	@bin/sad-test $(@:obj/%.testresult=tests/%.sad) $@ $(@:obj/%.testresult=%)
