CC=gcc
CCFLAGS=-o3 -Wall
RM=rm -f

all: clean sturmflut

sturmflut:
	$(CC) $(CCFLAGS) main.c -lpthread -o sturmflut

clean:
	$(RM) sturmflut
