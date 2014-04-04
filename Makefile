CC = cc
CFLAGS = -Wall -O2
LDFLAGS =

all: refer
.c.o:
	$(CC) -c $(CFLAGS) $<
refer: refer.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o refer
