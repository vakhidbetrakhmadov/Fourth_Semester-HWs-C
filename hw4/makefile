all: grepTh

grepTh: list.o list_dir.o restart.o grepTh.o
	gcc -o  grepTh  grepTh.o list.o list_dir.o restart.o -lpthread	

list.o: list.c list.h 
	gcc -c  -pedantic-errors -Wall list.c

list_dir.o: list_dir.c list_dir.h
	gcc -c  -pedantic-errors -Wall list_dir.c

restart.o: restart.c restart.h
	gcc -c -pedantic-errors -Wall restart.c

grepTh.o: grepTh.c
	gcc -c  -pedantic-errors -Wall grepTh.c

clean:
	rm *.o grepTh log.txt