#
# Makefile for the VML tools. Slightly complicated due to dependencies;
# the loader uses parts of Purple (a different project), the saver needs
# the Enough storage library.
#

VERSE	= ../verse

CFLAGS	= -Wall -I$(VERSE)
LDFLAGS	= -L$(VERSE)
LDLIBS	= -lverse
DATE	= `date --iso-8601 | tr -d -`

ALL:	loader saver

.PHONY:	clean dist

PLIBS	= dynstr.o list.o log.o mem.o memchunk.o strutil.o xmlnode.o

# -------------------------------------------------------------

loader:		loader.c typemaps.o $(PLIBS)

typemaps.o:	typemaps.c typemaps.h

# -------------------------------------------------------------

# Now for the saver. This depends on the Enough library, which
# is not included here for reasons of modularity. It is in the
# CVS module named "quelsolaar", and should be built separately.

# Edit this to point at location of "enough.h" and "libenough.a",
# or set it to nothing if installed system-wide.
ENOUGH	= ../quelsolaar
#ENOUGH = ""

# This supports having Enough installed in a system-wide location,
# by just setting ENOUGH to nothing, above. The "saver:" prefix
# makes these into target-specific values, which is a GNUism.
ifdef ENOUGH
saver:	CFLAGS	+= -I$(ENOUGH)
saver:	LDFLAGS	+= -L$(ENOUGH)
endif
saver:	LDLIBS	+= -lenough -lm

saver:	saver.c

# -------------------------------------------------------------

# This command removes the local copies of the Purple utility
# modules, and replaces them with symlinks to a Purple tree.
# If you already have Purple checked out, this is the recommended
# way of doing things since you will then stay in sync with any
# changes done to the shared code, there.
PURPLE	= ../purple
purplelink:
	for p in $(PLIBS); do rm -f $$(basename $$p .o).{c,h} && ln -s $(PURPLE)/$$(basename $$p .o).{c,h} .; done

# This replaces the symlinks with direct copies. Handy when
# building distribution archives.
purplecopy:
	for p in $(PLIBS); do rm -f $$(basename $$p .o).{c,h} && cp $(PURPLE)/$$(basename $$p .o).{c,h} . ; done

# This removes the Purple modules totally. Use purplelink or
# purplecopy to re-create them from the Purple tree.
purplerm:
	for p in $(PLIBS); do rm -f $$(basename $$p .o).* ; done

# -------------------------------------------------------------

clean:
	rm -f *.o loader saver

dist:
	make clean && cp -RL ../vml ../vml-tools && cd .. ; tar czvf vml-tools-$(DATE).tar.gz vml-tools ; rm -rf vml-tools/
