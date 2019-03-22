CC = gcc
CFLAGS = -O3 -Wall

hw3: UOB.o
	gcc UOB.o -o UOB -lreadline
UOB.o: UOB.c
	gcc UOB.c -c
clean:
	rm -rf UOB.o UOB
