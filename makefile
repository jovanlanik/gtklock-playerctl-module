# gtklock-playerctl-module
# Copyright (c) 2022 Jovan Lanik

# Makefile

NAME := playerctl-module.so

PREFIX = /usr/local
INSTALL = install

LIBS := gtk+-3.0 gmodule-export-2.0 playerctl libsoup-2.4
CFLAGS += -std=c11 -fPIC $(shell pkg-config --cflags $(LIBS))
LDLIBS += $(shell pkg-config --libs $(LIBS))

SRC = $(wildcard *.c) 
OBJ = $(SRC:%.c=%.o)

TRASH = $(OBJ) $(NAME)

.PHONY: all clean install uninstall

all: $(NAME)

clean:
	@rm $(TRASH) | true

install:
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/lib/gtklock
	$(INSTALL) $(NAME) $(DESTDIR)$(PREFIX)/lib/gtklock/$(NAME)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(NAME)

$(NAME): $(OBJ)
	$(LINK.c) -shared $^ $(LDLIBS) -o $@
