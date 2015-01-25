TARGET=sad

CC=gcc
CFLAGS=-ansi -pedantic -Wall -Werror

LINKER=gcc -o
LFLAGS=-lm

SOURCES=src/sad.c src/sad-script.c
OBJECTS=$(SOURCES:src/%.c=obj/%.o)

bin/$(TARGET): obj bin $(OBJECTS) bin/prelude.sad
	$(LINKER) $@ $(LFLAGS) $(OBJECTS)

bin/prelude.sad:
	cp src/prelude.sad bin/prelude.sad

obj:
	mkdir obj

bin:
	mkdir bin

$(OBJECTS): obj/%.o : src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS)

distclean: clean
	rm -f bin/$(TARGET)
