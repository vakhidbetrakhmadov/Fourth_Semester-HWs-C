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
#include "matrix.h"

#define FIFO_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#define LOG_FILE_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#define THOUSAND 1000
#define MILLION 1000000L
#define BUF_SIZE 1024
#define LOG_FILE_NAME "logs/logTimerServer.txt"
#define NEW_CONNECTIONS_FIFO "new_connections_fifo"

static volatile sig_atomic_t isSigUsr1 = 0;
static volatile sig_atomic_t isSigInt = 0;

static void sigIntHandler(int signal);
static void sigUsr1Handler(int signal);
static int setUpSigHandler(int signal,void(*handler)(int));
static void setUpSleepTimeLegth(struct timespec* sleepTimeLegth, long miliSeconds); /*sets up timespec struct to given number miliseconds*/
static int isNumber(const char* string);
static int writeLogs(int pipeReadEnd,int logfd); 
static int logSigIntDetection(int logfd);
static int sendMatrixToClient(pid_t clientPid,void* buffer,int size);
static int generateInvertableMatrixForClient(int size,pid_t clientPid,int pipeWriteEnd,const struct timespec* serverStartTime);
static double countMiliSecondsPassed(const struct timespec* start,const struct timespec* end);
static int addClient(pid_t ** pidList,int* size,int* capacity,pid_t client);
static int sigIntAllConnections(const pid_t* pidList,int size);
static int keepTrackOfNewConnections(pid_t serverPid);

/*****************************************************************************************************************************************************************************************************/
int main(int argc,const char* argv[])
{	
	int error = 0,readBytes = 0;
	struct timespec serverStartTime;
	struct timespec sleepTimeLegth;
	sigset_t maskAll,maskSome;
	int matrixSize = 0;
	long miliSeconds = 0;
	int logPipe[2];
	int mainFifoFd = -1;
	int logfd = -1;
	pid_t clientPid = 0;
	pid_t pid = 0;
	pid_t serverPid;
	const char* mainFifoName = argv[3];

	if(argc != 4 || !isNumber(argv[1]) || !isNumber(argv[2]) 
		|| (miliSeconds = atol(argv[1])) <= 0 || (matrixSize = 2*atoi(argv[2])) <= 0)
	{
		fprintf(stdout, "Usage:%s <tics per msec> <n> <mainpipe>\n",argv[0]);
		return 1;
	}
	
	serverPid = getpid(); /* save my pid*/
	
	if(setUpSigHandler(SIGINT,sigIntHandler) || setUpSigHandler(SIGUSR1,sigUsr1Handler)) /*setup signal handlers*/
	{	
		perror("setsigUsr1Handler()");
		return 1;
	}

	if(clock_gettime(CLOCK_REALTIME,&serverStartTime)) /*save server start time*/
		return 1;

	if (sigfillset(&maskAll) || sigemptyset(&maskSome) || sigaddset(&maskSome,SIGINT) || sigaddset(&maskSome,SIGUSR1)) /*setup masks*/
	{
		perror("Signal set operations failer");
		return 1;
	}
	if(sigprocmask(SIG_BLOCK,&maskAll,NULL)) /*block all signals*/
	{
		perror("sigprocmask()");
		return 1;
	}

	if((pid = fork()) == -1) /*proccess to keep track of all new connections*/
	{
		perror("fork()");
		error = 1;
	}
	else if(!pid)
	{
		if(keepTrackOfNewConnections(serverPid))
		{
			perror("keepTrackOfNewConnections()");
		}
		exit(0);
	}

	if (pipe(logPipe)) /*logs pipe opened*/
	{
		perror("pipe()");
		error = 1;
	}
	if(!error && mkfifo(mainFifoName,FIFO_PERMS) && errno !=EEXIST) /* main fifo created*/
	{
		perror("mkfifo()");
		error = 1;
	}
	if(!error && (mainFifoFd = r_open3(mainFifoName,O_NONBLOCK,O_RDONLY)) == -1) /*main fifo opened*/
	{
		perror("open()");
		error = 1;
	}
	if(!error && unlink(LOG_FILE_NAME) == -1 && errno!=ENOENT)
	{
		perror("unlink()");
		error = 1;
	}
	if(!error && (logfd = r_open3(LOG_FILE_NAME, O_CREAT | O_WRONLY,LOG_FILE_PERMS)) == -1 && errno!=EEXIST) /*logfile opened*/
	{
		perror("r_open3()");
		error = 1;
	}
	if(!error && (pid = fork()) == -1)/*fork a process to write logs*/
	{
		perror("fork()");
		error = 1;
	}
	else if(!pid)
	{	
		r_close(logPipe[1]);
		r_close(mainFifoFd);
		
		if(writeLogs(logPipe[0],logfd)) /* process to write logs*/
			perror("writeLogs()");

		r_close(logPipe[0]);
		r_close(logfd);
		exit(0);
	}
	if(!error && r_close(logPipe[0])) /* close log pipe read end in parent*/
	{
		perror("r_close()");
		error = 1;
	}
	if(!error)
		setUpSleepTimeLegth(&sleepTimeLegth,miliSeconds);

	/*-----------------------------------------SERVER LOOP-------------------------------------*/
	while(!error && !isSigInt)
	{
		while(waitpid(0,NULL,WNOHANG) > 0); /*wait for the terminated children(zombies)*/
		
		if(!error && nanosleep(&sleepTimeLegth,NULL))
		{
			perror("nanosleep()");
			error = 1;
		}

		if(!error && (sigprocmask(SIG_UNBLOCK,&maskSome,NULL) || sigprocmask(SIG_BLOCK,&maskAll,NULL)))
		{
			perror("sigprocmask()");
			error = 1;
		}

		if(!error && !isSigInt)
		{
			if(isSigUsr1)
			{
				if(!error && (readBytes = r_read(mainFifoFd, &clientPid,sizeof(pid_t))) != sizeof(pid_t) && errno != EAGAIN) 
				{	
					perror("main fifo read error...r_read()");
					error = 1;
				}
				else if (readBytes == sizeof(pid_t))
				{
					if(!error && (pid = fork()) == -1) /* create a procces to generate matrix */
					{
						perror("fork()");
						error =1;
					}
					else if(!pid) /*child*/
					{	
						r_close(mainFifoFd);
						r_close(logfd);
												
						if(generateInvertableMatrixForClient(matrixSize,clientPid,logPipe[1],&serverStartTime)) /*-------------------- call matrix generator*/
						{
							perror("generateInvertableMatrixForClient()");
						}
						
						r_close(logPipe[1]);
						exit(0);	
					}
				}
			}
				
			isSigUsr1 = 0;
		}
	}

	kill(0,SIGINT); /*send SIGINT to the procces keeping track of all new connections*/
	r_close(logPipe[1]); /*so that writeLogs process returns*/
	while(r_wait(NULL) > 0); /*wait for all children*/

	if(!error && logSigIntDetection(logfd))
	{
		perror("logSigIntDetection()");
		error = 1;
	}
		
	if(logfd != -1)
		r_close(logfd);
	if(mainFifoFd != -1)
		r_close(mainFifoFd);

	unlink(mainFifoName);
	if(error)
		unlink(LOG_FILE_NAME);

	return error;

}
/*****************************************************************************************************************************************************************************************************/

static int keepTrackOfNewConnections(pid_t serverPid)
{
	int error = 0,readBytes = 0;
	int newConFifoFd = -1;
	sigset_t mask;
	pid_t newClientPid = 0;
	pid_t* pidList = NULL;
	int pidListSize = 0;
	int pidListCapacity = 10;
	union sigval value;
	memset(&value,0,sizeof(union sigval));
	
	if(sigemptyset(&mask) || sigaddset(&mask,SIGINT) || sigprocmask(SIG_UNBLOCK,&mask,NULL)) /* unblock SIGINT*/ 
		error = 1;

	if(!error && mkfifo(NEW_CONNECTIONS_FIFO,FIFO_PERMS) && errno !=EEXIST) /* pid box fifo created*/
		error = 1;
	if(!error && (newConFifoFd = r_open3(NEW_CONNECTIONS_FIFO,O_NONBLOCK,O_RDONLY)) == -1) /*main fifo opened*/
		error = 1;

	value.sival_int = serverPid;
	while(!error && !isSigInt && (((readBytes = r_read(newConFifoFd,&newClientPid,sizeof(pid_t))) < 0 && errno==EAGAIN) 
		|| readBytes == sizeof(pid_t) || readBytes == 0))
	{
		if(readBytes == sizeof(pid_t))
		{
			if(sigqueue(newClientPid,SIGUSR2,value) || addClient(&pidList,&pidListSize,&pidListCapacity,newClientPid)) /*send to the new client server's pid and save his*/
				error = 1;
		}
	}

	if(sigIntAllConnections(pidList,pidListSize))
		error = 1;

	if(newConFifoFd != -1)
		r_close(newConFifoFd);
	if(pidList)
		free(pidList);
	unlink(NEW_CONNECTIONS_FIFO);
	return error;
}

static int sigIntAllConnections(const pid_t* pidList,int size)
{
	int i;
	int error = 0;

	for(i = 0;!error && i < size; ++i)
	{
		if(!kill(pidList[i],0)) /*check if the procces is still around*/
			if(kill(pidList[i],SIGINT) == -1)
				error = 1;
	}

	return error;
}

static int addClient(pid_t ** pidList,int* size,int* capacity, pid_t client)
{
	if(*pidList == NULL)
	{
		if((*pidList = (pid_t *) calloc(*capacity,sizeof(pid_t))) == NULL)
			return 1;
		*size = 0;
	}
	else if (*size >= *capacity)
	{
		(*capacity)*=2;
		if((*pidList = realloc(*pidList,*capacity*sizeof(pid_t))) == NULL)
			return 1;
	}

	(*pidList)[(*size)] = client;
	++(*size);
	return 0;
}

static int generateInvertableMatrixForClient(int size,pid_t clientPid,int pipeWriteEnd,const struct timespec* serverStartTime)
{	
	char buffer[PIPE_BUF];
	double * matrix;
	double	timeMatrixGenerated = 0;
	double determinant = 0;
	int error = 0;
	int matrixSizeInBytes;
	struct timespec endTime;

	if((matrix = calloc(size*size,sizeof(double))) == NULL)
		error = 1;

	if(!error)
		generateInvertableMatrix(matrix,size);
	
	if(!error &&clock_gettime(CLOCK_REALTIME,&endTime))
		error = 1;
	
	if(!error)
		timeMatrixGenerated = countMiliSecondsPassed(serverStartTime,&endTime);
	
	if(!error && findDeterminant(matrix,size,&determinant))
		error = 1;

	if(!error)
		sprintf(buffer, "time matrix generated : %.6lf, client pid : %d, determinant : %.6lf\n",timeMatrixGenerated,
	 		clientPid,determinant);

	if(!error && r_write(pipeWriteEnd,buffer,strlen(buffer)) != strlen(buffer))
		error = 1;

	matrixSizeInBytes = size*size*sizeof(double);
	if(!error && sendMatrixToClient(clientPid,matrix,matrixSizeInBytes)) 
		error = 1;
	
	if(matrix)
		free(matrix);

	return error;
}

static double countMiliSecondsPassed(const struct timespec* start,const struct timespec* end)
{
	return THOUSAND*(end->tv_sec - start->tv_sec) +
		(double)(end->tv_nsec - start->tv_nsec)/MILLION;
}

static int sendMatrixToClient(pid_t clientPid,void* buffer,int size)
{
	int bytesWriten;
	int fd;
	char fifoName[BUF_SIZE];
	
	sprintf(fifoName,"%d",clientPid);	
	if((fd = r_open2(fifoName,O_WRONLY)) == -1 && errno!=ENOENT) /* FIFO has been closed by dying client*/
		return 1;
	else if(fd == -1)
		return 0;

	r_write(fd,&size,sizeof(int)); /*-------------------- size of data*/
	bytesWriten =  r_write(fd,buffer,size);

	r_close(fd);
	return (bytesWriten == size ? 0 : (errno == EBADF ? 0 : 1)) ;
}


static int logSigIntDetection(int logfd)
{
	struct tm* tmtime;
	time_t ttime;
	char buffer[BUF_SIZE];
	
	time(&ttime);
	tmtime = localtime(&ttime);
	sprintf(buffer,"SIGINT received at %d:%d:%d\n",tmtime->tm_hour,tmtime->tm_min,tmtime->tm_sec);
	return	(r_write(logfd,buffer,strlen(buffer)) != strlen(buffer));
}

static void setUpSleepTimeLegth(struct timespec* sleepTimeLegth, long miliSeconds)
{	
	sleepTimeLegth->tv_sec = miliSeconds/THOUSAND;
	sleepTimeLegth->tv_nsec = (miliSeconds - (sleepTimeLegth->tv_sec * THOUSAND)) * MILLION;
}

static int isNumber(const char* string)
{
	int i;
	for (i = 0; i < strlen(string); ++i)
	{
		if(!isdigit(string[i]))
			return 0;	
	}

	return 1;
}

static void sigIntHandler(int signal)
{
	isSigInt = 1;
}

static void sigUsr1Handler(int signal)
{
	isSigUsr1 = 1;
}

static int setUpSigHandler(int signal,void(*handler)(int) )
{
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    return (sigfillset(&act.sa_mask) || sigaction(signal, &act, NULL));
}

static int writeLogs(int pipeReadEnd,int logfd)
{
	ssize_t bytesRead = 0;
	char buffer[BUF_SIZE];

	while((bytesRead = r_read(pipeReadEnd, buffer, BUF_SIZE)) != 0)
	{
		if(r_write(logfd, buffer, bytesRead) != bytesRead)
		{
			perror("r_write");
			return 1;
		}
	}

	return 0;
}