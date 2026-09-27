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

all: $(LIB) $(LIB_SO)

#
# $(OBJDIR)/.flags - rebuild when the COMPILE LINE changes
#
# A flag change is invisible to every timestamp: the sources are older than the
# objects and make sees nothing to do, so the build silently keeps objects
# compiled with the previous flags. This records them and makes the objects
# depend on the record.
#
$(OBJDIR)/.flags: FORCE
	@mkdir -p $(OBJDIR)
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

$(OBJDIR)/%.o: %.c $(OBJDIR)/.flags
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

#
# Removed first: `ar r` replaces and adds but never removes, so an object that
# is no longer built stays in the archive forever, and the next link quietly
# uses code that is not in the tree any more.
#
$(LIB): $(OBJECTS)
	@rm -f $@
	ar rcs $@ $(OBJECTS)

$(LIB_SO): $(OBJECTS)
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
	rm -rf obj $(LIB) $(LIB_SO) *.o *.d *.gcno *.gcda

FORCE:

.PHONY: all install di ci clean FORCE

-include $(DEPS)
