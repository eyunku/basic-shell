CC=gcc
CFLAGS=-Wall -Werror -pedantic -std=gnu18 -Ofast
LOGIN=eku
SUBMITPATH=~cs537-1/handin/eku/P3

.PHONY : all
all: wsh

wsh: wsh.c wsh.h
	$(CC) $(CFLAGS) $^ -o $@

run: wsh
	./wsh

pack: wsh.c wsh.h Makefile README.md
	tar -xzf $(LOGIN).tar.gz $^

submit: pack
	cp $(LOGIN).tar.gz SUBMITPATH

clean:
	rm -f wsh