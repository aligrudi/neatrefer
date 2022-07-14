CC = cc
CFLAGS = -Wall -O2
LDFLAGS =

BASE = $(PWD)
BIN = /usr/local/bin

OBJS = refer.o
LN = ln -f -s

all: refer
.c.o:
	$(CC) -c $(CFLAGS) $<
neatrefer: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
link:
	@echo "Linking refer to $(BIN)"
	@$(LN) $(BASE)/refer $(BIN)/neatrefer
clean:
	rm -f *.o refer
