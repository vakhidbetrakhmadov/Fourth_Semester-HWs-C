all: grepSh

grepSh: list.o list_dir.o restart.o grepSh.o
	gcc -o  grepSh  grepSh.o list.o list_dir.o restart.o -lpthread	

list.o: list.c list.h 
	gcc -c  -pedantic-errors -Wall list.c

list_dir.o: list_dir.c list_dir.h
	gcc -c  -pedantic-errors -Wall list_dir.c

restart.o: restart.c restart.h
	gcc -c -pedantic-errors -Wall restart.c

grepSh.o: grepSh.c
	gcc -c  -pedantic-errors -Wall grepSh.c

clean:
	rm *.o grepSh log.txt