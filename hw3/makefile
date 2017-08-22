all: exe

exe: list.o list_dir.o main.o
	gcc main.o list.o list_dir.o -o exe

list.o: list.c list.h
	gcc -c -ansi -pedantic-errors -Wall list.c

list_dir.o: list_dir.c
	gcc -c -ansi -pedantic-errors -Wall list_dir.c

main.o: main.c
	gcc -c -ansi -pedantic-errors -Wall main.c

clean:
	rm *.o exe log.txt