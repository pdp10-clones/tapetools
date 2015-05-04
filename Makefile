# Backup-10 for POSIX environments
#

# Copyright (c) 2015 Timothe Litt litt at acm ddot org
# All rights reserved.
#
# This software is provided under GPL V2, including its disclaimer of
# warranty.  Licensing under other terms may be available from the author.
#
# See the LICENSE file for the well-known text of GPL V2.
#
# Bug reports, fixes, suggestions and improvements are welcome.
#
# This is a rewrite of backup.c and backwr.c, which have been
# distributed on the internet for some time - with no authors listed.
# This rewrite fixes many bugs, adds features, and has at most
# a loose relationship to its predecessors.
#

CC=gcc
TAR=tar

CPPFLAGS+=$(shell getconf LFS_CFLAGS)

# Compiler options

CFLAGS=-g -O2 -Wall -Wshadow -Wextra -pedantic
#CFLAGS+=-Woverflow -Wstrict-overflow 

LDLIBS+=-lm $(shell getconf LFS_LIBS)

LDFLAGS+=$(shell getconf LFS_LDFLAGS)

OBJS=backup36.o data36.o math36.o sysdep.o magtape.o
TOBJS=tape36.o data36.o magtape.o

PACKAGED=LICENSE README.md backup36.c tape36.c magtape.c data36.c math36.c sysdep.c backup.h magtape.h data36.h math36.h sysdep.h version.h Makefile

VERDEF:=$(shell /bin/sh version.sh)

.PHONY: all clean dist

all: backup36 tape36


backup36: $(OBJS) Makefile
	$(CC) $(LDFLAGS) -o backup36 $(OBJS) $(LDLIBS)

tape36: $(TOBJS) Makefile
	$(CC) $(LDFLAGS) -o tape36 $(TOBJS) $(LDLIBS)

-include $(OBJS:.o=.d)

-include $(TOBJS:.o=.d)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS)  -o $*.o $*.c
	@$(CC) -MM $(CPPFLAGS) -MF $*.d $*.c
	@cp -f $*.d $*.d.tmp
	@sed -e 's/.*://' -e 's/\\$$//' $*.d.tmp | fmt -1 | \
		sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

dist: $(PACKAGED)
	$(TAR) -czf backup36.tar.gz $(PACKAGED)

clean:
	rm -f backup36 tape36 *.o *.d *.d.tmp version.h.tmp

