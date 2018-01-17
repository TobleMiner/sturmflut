CC=gcc
CCFLAGS=-O3 -Wall -ggdb
RM=rm -f

all: clean test_gif

test_gif:
	$(CC) $(CCFLAGS) ../../image.c main.c `pkg-config --cflags --libs MagickWand` -o test

clean:
	$(RM) test
