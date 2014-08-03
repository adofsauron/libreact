CC=gcc
CFLAGS=-g -fPIC -I.
OUT=ev

all: $(OUT)

$(OUT): *.c
	$(CC) $(CFLAGS) $^ -o $@

stream_io: stream_io.c ev_rbtree.c
	$(CC) $(CFLAGS) $^ -o $@
