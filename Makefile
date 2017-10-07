CC=gcc
RM=rm

all: sturmflut

sturmflut:
	$(CC) main.c -lpthread -o sturmflut

clean:
	$(RM) sturmflut
