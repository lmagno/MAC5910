CC = gcc
CFLAGS = -Wall -g

ep1: ep1.c
	$(CC) $(CFLAGS) $^ -o $@
