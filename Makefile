CC=gcc
CCFLAGS=-O3 -Wall
RM=rm -f

all: clean sturmflut

sturmflut:
	$(CC) $(CCFLAGS) image.c network.c main.c -lpthread `pkg-config --cflags --libs MagickWand` -o sturmflut

clean:
	$(RM) sturmflut
