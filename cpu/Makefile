CC=gcc
CFLAGS=-g -Wall
LDFLAGS=-lvirt

all: vcpu_scheduler

vcpu_scheduler: vcpu_scheduler.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) -f *.o vcpu_scheduler
