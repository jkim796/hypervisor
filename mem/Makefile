CC=gcc
CFLAGS=-g -Wall
LDFLAGS=-lvirt

all: mem_coordinator

mem_coordinator: mem_coordinator.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) -f *.o mem_coordinator
