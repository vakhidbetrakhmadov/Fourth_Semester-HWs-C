#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include "strerror_r.h"
#include "uici.h"
#include "restart.h"
#include "serve_request.h"

#define DEFAULT_PORT 9898
#define BUF_SIZE 1024
#define LOGS_FILE_NAME "server_logs.txt"
#define PERMS_LOGS (mode_t) (S_IRWXU | S_IRWXG | S_IRWXO)
#define FLAGS_LOGS (O_CREAT | O_WRONLY)

/* ------  Global variables ----------*/
volatile sig_atomic_t sigreceived = 0;
int pipe_logs_fd[2]; 

int passive_sock_fd = 0;  /* made global, so that could be closed at fork in serve_request file*/
int file_logs_fd = 0;     /* made global, so that could be closed at fork in serve_request file*/
/* ------  End ----------*/

static int setup_signal_handler(int signal);
static void signal_handler(int signal);
static int is_valid_number(const char* str);
static void* write_logs(void* args); /* thread function for writing logs */


#ifdef TH_POOL /* ------------------------- CODE FOR THE THREADS POOL IMPLEMENTATION ------------------------------- */

static int workers_alive = 0;
static pthread_mutex_t mutex_workers_alive = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_workers_alive = PTHREAD_COND_INITIALIZER;

static int decrement_workers_alive(void);
static int wait_until_all_workers_die(void);
static void* start_worker(void* args);

int main(int argc, const char* argv[])
{
	int i = 0;
	int error = 0;
	sigset_t mask_most;
	sigset_t mask_one;
	int pool_size = 0;
	u_port_t port = 0;
	pthread_t screener_tid;
	pthread_t logger_tid;
	pthread_t tid;
	pthread_attr_t thread_attr; /* modified */

	if((argc != 2 && argc != 3) || (argc == 2 && !is_valid_number(argv[1])) || (argc == 3 &&  (!is_valid_number(argv[1]) || !is_valid_number(argv[2])))) /* ---- Usage check ---- */
	{
		fprintf(stderr,"Usage: %s <thpool size> or %s <port> <thpool size>\n",argv[0],argv[0]);
		return 1;
	}

	if(argc == 3)                           /* ---- Choose the passive port number and sizeof the thread pool----- */
	{
		port = (u_port_t ) atoi(argv[1]);
		pool_size = atoi(argv[2]);
	}	
	else
	{
		port = DEFAULT_PORT;
		pool_size = atoi(argv[1]);
	}

	if(sigfillset(&mask_most) || sigdelset(&mask_most,SIGINT) || sigprocmask(SIG_BLOCK,&mask_most,NULL) || setup_signal_handler(SIGINT)) /*block all signals except for SIGINT, setup signal handler*/
	{
		err("sigfillset() || sigdelset() || sigprocmask() || setup_signal_handler()");
		return 1;
	}
	else if(pthread_attr_init(&thread_attr) || pthread_attr_setdetachstate(&thread_attr,PTHREAD_CREATE_DETACHED))
	{
		err("pthread_attr_init() || pthread_attr_setdetachstate()");
		return 1;
	}
	else if(pipe(pipe_logs_fd))
	{
		err("pipe()");
		return 1;
	}
	else if(pthread_create(&screener_tid,NULL,write_to_screen,NULL) || pthread_create(&logger_tid,NULL,write_logs,NULL)) /*create a thread to print to the screen and a thread to write logs*/
	{
		err("pthread_create()");
		error = 1;
	}
	else if((passive_sock_fd = u_open(port)) == -1) /*created, bind and mark socket as passive */
	{
		err("u_open()");
		error = 1;
	}
	else
	{
		workers_alive = pool_size;
		for( i = 0; !error && i < pool_size; ++i)
		{
			if(pthread_create(&tid,&thread_attr,start_worker,NULL))
			{
				err("pthread_create()");
				error = 1;
			}
		}
		if(sigemptyset(&mask_one) || sigaddset(&mask_one,SIGINT) || pthread_sigmask(SIG_BLOCK,&mask_one,NULL)) /* block SIGINT for this thread*/
		{
			err("sigemptyset() || sigaddset() || pthread_sigmask()");
			error = 1;
		}
		if(!error && wait_until_all_workers_die())
		{
			err("wait_until_all_workers_die()");
			error = 1;
		}
	}

	pthread_cancel(screener_tid);
	pthread_join(screener_tid,NULL);

	r_close(pipe_logs_fd[1]); /*let logger thread exit*/
	pthread_join(logger_tid,NULL);

	r_close(pipe_logs_fd[0]); 

	pthread_attr_destroy(&thread_attr); 

	return error;
}

static void* start_worker(void* args)
{
	int private_sock_fd = 0;

	while(!sigreceived)
	{
		if(!sigreceived && (private_sock_fd = u_accept(passive_sock_fd,NULL,0)) == -1 && errno != EINTR) 
		{
			err("u_accept()");
			continue;
		}

		if(!sigreceived)
			serve_request(&private_sock_fd);
	}

	if(signal_other_thread())  /* notify other thread of SIGINT, triggers chain reaction*/
		err("signal_other_thread()");

	r_close(passive_sock_fd);

	if(decrement_workers_alive())
		err("In start_worker decrement_workers_alive()");

	return NULL;
}

static int wait_until_all_workers_die(void)
{
	int error = 0;
	if(pthread_mutex_lock(&mutex_workers_alive))
		return 1;
	while(workers_alive != 0)
	{
		if(pthread_cond_wait(&cond_workers_alive,&mutex_workers_alive))
		{
			error = errno;
			pthread_mutex_unlock(&mutex_workers_alive);
			errno = error;
			return 1;
		}
	}
	if(pthread_mutex_unlock(&mutex_workers_alive))
		return 1;

	return 0;
}

static int decrement_workers_alive(void)
{
	int error = 0;
	if(pthread_mutex_lock(&mutex_workers_alive))
		return 1;
	--workers_alive;
	if(workers_alive == 0 && pthread_cond_broadcast(&cond_workers_alive))
	{
		error = errno;
		pthread_mutex_unlock(&mutex_workers_alive);
		errno = error;
		return 1;
	}
	else if(pthread_mutex_unlock(&mutex_workers_alive))
		return 1;

	return 0;
}
#else  /* ---------------------------------------------- END  ---------------------------------------------------------- */

/* ----------------------------------- CODE FOR THE THREAD PER REQUEST IMPLEMENTATION -----------------------------------*/
int main(int argc,const char* argv[]) 
{
	int error = 0;
	sigset_t mask_most;
	u_port_t port = 0;
	int private_sock_fd = 0;
	pthread_t screener_tid;
	pthread_t logger_tid;
	pthread_t tid;
	pthread_attr_t thread_attr; /* modified */

	if(argc > 2 || (argc == 2 && !is_valid_number(argv[1]))) /* ---- Usage check ---- */
	{
		fprintf(stderr,"Usage: %s or %s <port>\n",argv[0],argv[0]);
		return 1;
	}

	if(argc == 2)                           /* ---- Choose the passive port number----- */
		port = (u_port_t ) atoi(argv[1]);
	else
		port = DEFAULT_PORT;

	if(sigfillset(&mask_most) || sigdelset(&mask_most,SIGINT) || sigprocmask(SIG_BLOCK,&mask_most,NULL) || setup_signal_handler(SIGINT)) /*block all signals except for SIGINT, setup signal handler*/
	{
		err("sigfillset() || sigdelset() || sigprocmask() || setup_signal_handler()");
		return 1;
	}
	else if(pthread_attr_init(&thread_attr) || pthread_attr_setdetachstate(&thread_attr,PTHREAD_CREATE_DETACHED)) /* modified */
	{
		err("pthread_attr_init() || pthread_attr_setdetachstate()");
		return 1;
	}
	else if(pipe(pipe_logs_fd))
	{
		err("pipe()");
		return 1;
	}
	else if(pthread_create(&screener_tid,NULL,write_to_screen,NULL) || pthread_create(&logger_tid,NULL,write_logs,NULL)) /*create a thread to print to the screen and a thread to write logs*/
	{
		err("pthread_create()");
		error = 1;
	}
	else if((passive_sock_fd = u_open(port)) == -1) /*created, bind and mark socket as passive */
	{
		err("u_open()");
		error = 1;
	}
	else
	{
		while(!sigreceived) /* main server loop */
		{
			if((private_sock_fd = u_accept(passive_sock_fd,NULL,0)) == -1 && errno != EINTR)
			{
				err("u_accept()");
				error = 1;
				continue;
			}
			else if(!sigreceived)
			{
				
				if(pthread_create(&tid,&thread_attr,serve_request,&private_sock_fd))
				{
					err("pthread_create()");
					error = 1;
					continue;
				}
				if(wait_args_delivery(&private_sock_fd))
				{
					err("wait_args_delivery()");
					error = 1;
					continue;
				}
			}
		}

		r_close(passive_sock_fd);

		if(signal_other_thread())  /* notify other thread of SIGINT, triggers chain reaction*/
		{
			err("signal_other_thread()");
			error = 1;
		}
		if(wait_requests_being_served()) /*wait untill all request being served are finished*/
		{
			err("wait_requests_being_served()");
			error = 1;
		}
	}

	pthread_cancel(screener_tid);
	pthread_join(screener_tid,NULL);

	r_close(pipe_logs_fd[1]); /*let logger thread exit*/
	pthread_join(logger_tid,NULL);

	r_close(pipe_logs_fd[0]); 

	pthread_attr_destroy(&thread_attr);  /* modified */

	return error;
}

#endif /* ------------------------------------ END ------------------------------------------------- */

static void* write_logs(void* args)
{
	sigset_t mask_one;
	ssize_t bytes_read = 0;
	char buffer[BUF_SIZE];

	if(sigemptyset(&mask_one) || sigaddset(&mask_one,SIGINT) || pthread_sigmask(SIG_BLOCK,&mask_one,NULL)) /* block SIGINT for this thread*/
	{
		err("In write_logs(): error while setting sigmask");
		return NULL;
	}

	if(unlink(LOGS_FILE_NAME) == -1 && errno!=ENOENT)
	{
		err("In write_logs(): unlink()");
	}
	else if((file_logs_fd = r_open3(LOGS_FILE_NAME,FLAGS_LOGS,PERMS_LOGS)) == -1) 
	{
		err("In write_logs(): r_open3()");
		return NULL;
	}

	while((bytes_read = r_read(pipe_logs_fd[0], buffer, BUF_SIZE)) != 0 && (bytes_read != -1))
	{
		if(r_write(file_logs_fd, buffer, bytes_read) != bytes_read)
		{
			err("In write_logs(): r_write()");
			continue;
		}
	}
	if(bytes_read == -1)
		err("In write_logs(): r_read()");

	close(file_logs_fd);

	return NULL;
}

static int setup_signal_handler(int signal)
{
    struct sigaction act;
    memset(&act,'\0',sizeof(struct sigaction));
    act.sa_handler = signal_handler;
    return (sigfillset(&act.sa_mask) || sigaction(signal,&act,NULL));
}

static void signal_handler(int signal)
{
	sigreceived = signal;
}

static int is_valid_number(const char* str)
{
	int result = 1;
	int i = 0;
	for(i = 0; str[i] != '\0' && result; ++i)
		if(!isdigit(str[i]))
			result = 0;

	return result;
}