OPTIONS = -pedantic-errors -ansi -Wall
POSIXSOURCE = -D_POSIX_C_SOURCE=200112L 
XOPEN_SOURCE = -D_XOPEN_SOURCE
LIBTHREAD = -lpthread
MATH = -lm
 
#UNCOMMENT THE LINE BELOW IN ORDER TO COMPILE SERVER"S THREAD POOL IMPLEMENTATION
#TH_POOL = -DTH_POOL 


all: server clients


server: server.o strerror_r.o uici.o uiciname.o restart.o serve_request.o linear.o qr.o svd.o
	gcc -o server server.o strerror_r.o uici.o uiciname.o serve_request.o restart.o linear.o  qr.o svd.o $(LIBTHREAD) $(MATH)

clients: clients_main.o clients.o uici.o restart.o uiciname.o strerror_r.o
	gcc -o clients clients_main.o clients.o uici.o uiciname.o restart.o strerror_r.o $(LIBTHREAD) $(MATH)


server.o: server.c
	gcc -c server.c $(OPTIONS) $(POSIXSOURCE) $(XOPEN_SOURCE) $(TH_POOL)

serve_request.o: serve_request.c serve_request.h
	gcc -c serve_request.c $(OPTIONS) $(POSIXSOURCE) $(XOPEN_SOURCE)


clients_main.o: clients_main.c 
	gcc -c clients_main.c $(OPTIONS) $(POSIXSOURCE) $(XOPEN_SOURCE)

clients.o: clients.c clients.h
	gcc -c clients.c $(OPTIONS) $(POSIXSOURCE)


strerror_r.o: strerror_r.c strerror_r.h
	gcc -c strerror_r.c $(OPTIONS) $(POSIXSOURCE)

uici.o: uici.c uici.h
	gcc -c uici.c $(OPTIONS) $(POSIXSOURCE)

uiciname.o: uiciname.c uiciname.h
	gcc -c uiciname.c $(OPTIONS) $(POSIXSOURCE)

restart.o: restart.c restart.h
	gcc -c restart.c $(OPTIONS) $(POSIXSOURCE)

linear.o: linear.c linear.h
	gcc -c linear.c $(OPTIONS) $(POSIXSOURCE)

qr.o: qr.c qr.h
	gcc -c qr.c $(OPTIONS) 

svd.o: svd.c svd.h
	gcc -c svd.c $(OPTIONS) 


clean:
	 rm *.o server clients server_logs.txt clients_logs_*