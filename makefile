#
# FILE            makefile
#
# AUTHOR          Ken Zangelin
#
# Copyright 2026 Seamware
# SPDX-License-Identifier: Apache-2.0
#
#
# corProm - Prometheus metrics: counters, gauges and histograms updated with
# atomics on the hot path (no lock, no allocation), rendered on demand either as
# Prometheus text or as a corTree.
#
# Every library in this stack is a SIBLING repo - `-I..` and `../<name>/lib<name>.a`
# is the layout, and it is part of the build contract rather than a convenience.
#
LIB_SO        = libcorProm.so
LIB           = libcorProm.a
CC            = gcc
INCLUDE       = -I..
DFLAGS        =
#
# EXTRA_CFLAGS - the hook for a caller that needs to ADD flags to this build.
# Not DFLAGS: `make DFLAGS=...` REPLACES it, and a `DFLAGS +=` here would be
# ignored along with it, so a caller adding one flag would drop every default.
#
CFLAGS        = -O2 -Wall -Wextra -Werror -fPIC -fstack-protector-strong $(DFLAGS) $(INCLUDE) -MMD -MP $(EXTRA_CFLAGS)

LIB_SOURCES   = corProm.c         \
                corPromVersion.c

BUILD        ?= debug
OBJDIR        = obj/$(BUILD)
OBJECTS       = $(LIB_SOURCES:%.c=$(OBJDIR)/%.o)
DEPS          = $(OBJECTS:.o=.d)

#
# The archive and the shared library are built PER FLAVOUR, in $(OBJDIR), and then STAGED to the repo
# root, where every consumer links them (../corProm/libcorProm.a). Unconditionally, on every build:
# built in place, a debug archive is newer than obj/release/*.o, so `make BUILD=release` after a debug
# build found nothing to do and left the other flavour's archive in place. A plain cp, not cp -p:
# the staged file gets a new mtime, so whatever links it relinks. Copied and renamed, so a process
# that has the .so mapped keeps the old file.
#
all: $(OBJDIR)/$(LIB) $(OBJDIR)/$(LIB_SO)
	@cp -f $(OBJDIR)/$(LIB) $(LIB).tmp && mv -f $(LIB).tmp $(LIB)
	@cp -f $(OBJDIR)/$(LIB_SO) $(LIB_SO).tmp && mv -f $(LIB_SO).tmp $(LIB_SO)

#
# The staged files by name - `make libcorProm.a` stages the current flavour's archive.
#
$(LIB): $(OBJDIR)/$(LIB)
	@cp -f $< $@.tmp && mv -f $@.tmp $@

$(LIB_SO): $(OBJDIR)/$(LIB_SO)
	@cp -f $< $@.tmp && mv -f $@.tmp $@

#
# $(OBJDIR)/.flags - rebuild when the COMPILE LINE changes
#
# A flag change is invisible to every timestamp: the sources are older than the
# objects and make sees nothing to do, so the build silently keeps objects
# compiled with the previous flags. This records them and makes the objects
# depend on the record. The compiler is part of the line: `make pgo` passes
# CC="gcc -fprofile-use=...".
#
$(OBJDIR)/.flags: FORCE
	@mkdir -p $(OBJDIR)
	@echo '$(CC) $(CFLAGS)' | cmp -s - $@ || echo '$(CC) $(CFLAGS)' > $@

$(OBJDIR)/%.o: %.c $(OBJDIR)/.flags
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

#
# Removed first: `ar r` replaces and adds but never removes, so an object that
# is no longer built stays in the archive forever, and the next link quietly
# uses code that is not in the tree any more.
#
$(OBJDIR)/$(LIB): $(OBJECTS)
	@rm -f $@
	ar rcs $@ $(OBJECTS)

$(OBJDIR)/$(LIB_SO): $(OBJECTS)
	$(CC) -shared -o $@ $(OBJECTS)

#
# install - nothing to copy. Consumers compile with `-I..` and link
# `../corProm/libcorProm.a` straight out of the checkout, so `all` has already
# put the artefacts where every consumer looks for them.
#
install: all

di: all install

ci: clean install

clean:
	rm -rf obj $(LIB) $(LIB_SO) $(LIB).tmp $(LIB_SO).tmp *.o *.d *.gcno *.gcda

FORCE:

.PHONY: all install di ci clean FORCE

-include $(DEPS)
