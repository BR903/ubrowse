# Makefile for ubrowse
#
# Note: "make clean-all" will force the next build to download the
# current Unicode standard.

CC = gcc
CFLAGS = -Wall -Wextra -ansi -pedantic -Wno-overlength-strings -Wno-format
CFLAGS += -Os -I/usr/include/ncursesw
LDFLAGS = -Wall -s
LOADLIBES = -lncursesw

CHARLISTURL = http://www.unicode.org/Public/UNIDATA/UnicodeData.txt
BLOCKLISTURL = http://www.unicode.org/Public/UNIDATA/Blocks.txt

.PHONY: clean clean-all

ubrowse: ubrowse.o charlist.o blocklist.o
ubrowse.o: ubrowse.c data.h
charlist.o: charlist.c data.h
blocklist.o: blocklist.c data.h

charlist.c: mkcharlist.py
	curl $(CHARLISTURL) | ./mkcharlist.py > $@
blocklist.c: mkblocklist.py
	curl $(BLOCKLISTURL) | ./mkblocklist.py > $@

clean:
	rm -f ubrowse ubrowse.o charlist.o blocklist.o

clean-all: clean
	rm -f charlist.c blocklist.c
