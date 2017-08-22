all: timerServer seeWhat showResults

timerServer: restart.o timerServer.o matrix.o
	gcc timerServer.o restart.o matrix.o -o timerServer

seeWhat: seeWhat.o restart.o matrix.o
	gcc seeWhat.o restart.o matrix.o -o seeWhat -lm

showResults:showResults.o restart.o
	gcc showResults.o restart.o -o showResults

timerServer.o: timerServer.c
	gcc -c  -pedantic-errors -Wall timerServer.c

seeWhat.o: seeWhat.c
	gcc -c  -pedantic-errors -Wall seeWhat.c

showResults.o: showResults.c
	gcc -c  -pedantic-errors -Wall showResults.c

restart.o: restart.c restart.h
	gcc -c -pedantic-errors -Wall restart.c

matrix.o: matrix.c matrix.h
	gcc -c -pedantic-errors -Wall matrix.c

clean:
	rm *.o seeWhat timerServer showResults logs/*