#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <wait.h>
#include "restart.h"

#define FIFO_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#define LOG_FILE_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)

#define NEW_CONNECTIONS_FIFO "new_connections_fifo"
#define SHOW_RESULTS_FIFO_NAME "show_results_fifo"
#define LOG_FILE_NAME "logs/showResults.txt"
#define BUF_SIZE 1024

typedef struct results_s
{
	pid_t pid;
	double result1;
	double timeElapsed1;
	double result2;
	double timeElapsed2;

}results_t;

static volatile sig_atomic_t isSigInt = 0;
static volatile sig_atomic_t serverPid = 0;

static void sigIntHandler(int signal);
static int setUpSigHandler(int signal,void(*handler)(int) );
static int connectToServer(void);
static void sigUsr2Handler(int signo, siginfo_t* info, void *context);
static int setUpSaSigHandler(int signal,void (*sa_act)(int, siginfo_t *, void *));

/****************************************************************************************************************************************************************/
int main(int argc,const char* argv[])
{
	results_t nextResult;
	int bytesRead = 0;
	sigset_t maskMost;
	int logfd;
	int fifo;
	char buffer[BUF_SIZE];
	int error = 0;

	if(argc != 1)
	{
		fprintf(stderr, "Usage:%s\n",argv[0]);
		return 1;
	}

	if(setUpSigHandler(SIGINT,sigIntHandler) || setUpSaSigHandler(SIGUSR2,sigUsr2Handler)) /* set up signal handler for SIGINT*/
	{
		perror("setUpSigHandler()");
		return 1;
	}

	if (sigfillset(&maskMost) || sigdelset(&maskMost,SIGINT) || sigdelset(&maskMost,SIGUSR2)) /*setup masks*/
	{
		perror("Signal set operations failer");
		return 1;
	}
	if(sigprocmask(SIG_BLOCK,&maskMost,NULL)) /*block all signals except for SIGINT and SIGUSR2*/
	{
		perror("sigprocmask()");
		return 1;
	}

	if(mkfifo(SHOW_RESULTS_FIFO_NAME,FIFO_PERMS) && errno !=EEXIST) /*  fifo created*/
	{
		perror("mkfifo()");
		return 1;
	}
	if((fifo = r_open3(SHOW_RESULTS_FIFO_NAME,O_NONBLOCK,O_RDONLY)) == -1) /*fifo opened*/
	{
		perror("r_open3()");
		unlink(SHOW_RESULTS_FIFO_NAME);
		return 1;
	}
	if(unlink(LOG_FILE_NAME) == -1 && errno!=ENOENT)
	{
		perror("unlink()");
		r_close(fifo);
		unlink(SHOW_RESULTS_FIFO_NAME);
		return 1;
	}
	if((logfd = r_open3(LOG_FILE_NAME, O_CREAT | O_WRONLY,LOG_FILE_PERMS)) == -1 && errno!=EEXIST) /*logfile opened*/
	{
		perror("r_open3()");
		r_close(fifo);
		unlink(SHOW_RESULTS_FIFO_NAME);
		return 1;
	}

	if(!isSigInt && connectToServer()) /*blocks until server starts*/
	{
		perror("connectToServer()");
		r_close(fifo);
		unlink(SHOW_RESULTS_FIFO_NAME);
		return 1;
	}

	/*-----------------------------------------SHOW_RESULTS LOOP-------------------------------------*/
	while(!error && !isSigInt)
	{
		while((((bytesRead = r_read(fifo,&nextResult,sizeof(results_t))) < 0 && errno==EAGAIN) || bytesRead == 0) && !isSigInt);
		
		if(!isSigInt)
		{
			if(bytesRead != sizeof(results_t))
			{
				perror("r_read()");
				error = 1;
			}
			else
			{
				sprintf(buffer,"Client's pid: %d, result 1: %lf, result 2: %lf\n",nextResult.pid,nextResult.result1,nextResult.result2);
				if(r_write(STDOUT_FILENO,buffer,strlen(buffer)) != strlen(buffer))
				{
					perror("r_write()");
					error = 1;
				}
				sprintf(buffer,"Client's pid: %d\nResult 1: %lf Time elapsed: %lf\nResult 2: %lf Time elapsed: %lf\n",
					nextResult.pid,nextResult.result1,nextResult.timeElapsed1,nextResult.result2,nextResult.timeElapsed2);
				if(!error && r_write(logfd,buffer,strlen(buffer)) != strlen(buffer))
				{
					perror("r_write()");
					error = 1;
				}
			}
		}
	}

	if(!error && serverPid && !kill(serverPid,0)) /*check if the server is still around*/
	{
		if(kill(serverPid,SIGINT) == -1)
		{
			perror("kill()");
			error = 1;
		}
	}

	r_close(fifo);
	r_close(logfd);
	unlink(SHOW_RESULTS_FIFO_NAME);
	if(error)
		unlink(LOG_FILE_NAME);

	return 1;
}
/****************************************************************************************************************************************************************/

static int connectToServer(void)
{
	int newConFifoFd ;
	pid_t mypid = getpid();

	while(!isSigInt && (newConFifoFd = r_open2(NEW_CONNECTIONS_FIFO,O_WRONLY)) == -1  && errno == ENOENT); /*open fifo for new collections*/
	if(!isSigInt && newConFifoFd == -1 && errno != ENOENT) 
	{
		return 1;
	}
	else if(!isSigInt)
	{
		if(r_write(newConFifoFd,&mypid,sizeof(pid_t)) != sizeof(pid_t)) /*send pid to server*/
		{
			r_close(newConFifoFd);
			return 1;
		}
	}

	return 0;
}

static void sigIntHandler(int signal)
{
	isSigInt = 1;
}

static int setUpSigHandler(int signal,void(*handler)(int) )
{
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    return (sigfillset(&act.sa_mask) || sigaction(signal, &act, NULL));
}

static void sigUsr2Handler(int signo, siginfo_t* info, void *context)
{	
	serverPid = info->si_value.sival_int;
}

static int setUpSaSigHandler(int signal,void (*sa_act)(int, siginfo_t *, void *))
{
    struct sigaction act;
     act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = sa_act;
    return (sigfillset(&act.sa_mask) || sigaction(signal, &act, NULL));
}