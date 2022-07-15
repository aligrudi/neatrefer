CC = cc
CFLAGS = -Wall -O2
LDFLAGS =

OBJS = refer.o

all: refer
.c.o:
	$(CC) -c $(CFLAGS) $<
refer: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
clean:
	rm -f *.o refer
