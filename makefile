CC=gcc
CFLAGS=-g -Wall -Iinclude

TARGET=run_tests

TARGET: main.c i8080.o
	$(CC) $(CFLAGS) -o $(TARGET) main.c i8080.o

i8080.o: i8080.c
	$(CC) $(CFLAGS) -c i8080.c

clean:
	$(RM) $(TARGET) *.o
