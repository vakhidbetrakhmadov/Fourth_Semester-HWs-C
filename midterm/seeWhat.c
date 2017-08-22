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
#include <math.h>
#include "restart.h"
#include "matrix.h"


#define FIFO_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#define LOG_FILE_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#define BILLION 1000000000L
#define BUF_SIZE 1024
#define LOG_FILE_NAME "logs/seeWhat"
#define NEW_CONNECTIONS_FIFO "new_connections_fifo"
#define SHOW_RESULTS_FIFO_NAME "show_results_fifo"

static volatile sig_atomic_t isSigInt = 0;
static volatile sig_atomic_t serverPid = 0;

static int connectToServer(void);
static void sigIntHandler(int signal);
static int setUpSigHandler(int signal,void(*handler)(int) );
static void sigUsr2Handler(int signo, siginfo_t* info, void *context);
static int setUpSaSigHandler(int signal,void(*sa_act)(int, siginfo_t*,void *));
static void recieveMatrix(int fifo,double* matrix,int matrixSizeInBytes);
static int getMatrixSize(int matrixSizeInBytes);
static int writeLogs(int logPipe,int logfd,const double* originalMatrix,int matrixSize);
static int writeMatrixTo(int fd,const double* matrix,int matrixSize, char* message);
static int doOperationOnMatrixAndSaveResults(double* matrix,int size,int(*doOperationOnMatrix)(double *,int),int logPipeWriteEnd,int showResultsPipeWriteEnd);
static double countSecondsPassed(const struct timespec* start,const struct timespec* end);
static int sendDataToShowResults(int showResultsPipeReadEnd,const char * fifoName);

typedef struct results_s
{
	pid_t pid;
	double result1;
	double timeElapsed1;
	double result2;
	double timeElapsed2;

}results_t;

/****************************************************************************************************************************************************************/
int main(int argc,const char* argv[])
{
	sigset_t maskMost;
	pid_t mypid = 0;
	pid_t pid = 0;
	int myFifo = 0;
	int logPipe[2];
	int logfd = 0;
	int showResultsPipe[2];
	char myFifoName[BUF_SIZE];
	char logFileName[BUF_SIZE];
	int mainFifo = 0;
	int error = 0;
	int readBytes = 0;
	int matrixSizeInBytes = 0;
	double * matrix = NULL;
	int matrixSize = 0;
	const char* mainFifoName = argv[1];
	
	if(argc != 2)
	{
		fprintf(stderr, "Usage:%s <mainpipe>\n",argv[0]);
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
	if(sigprocmask(SIG_BLOCK,&maskMost,NULL)) /*block all signals except for SIGINT*/
	{
		return 1;
		perror("sigprocmask()");
	}

	while((mainFifo = r_open2(mainFifoName,O_WRONLY)) == -1 && errno == ENOENT && !isSigInt); /* wait for a server to start , open main fifo*/
	if(mainFifo == -1 && errno != ENOENT )
	{
		perror("r_open2()");
		return 1;
	}

	if(!isSigInt && connectToServer()) /*establish connection with server*/
	{
		perror("connectToServer()");
		return 1;
	}

	if(!isSigInt)
	{
		mypid = getpid();
		sprintf(myFifoName,"%d",mypid);
		if(mkfifo(myFifoName,FIFO_PERMS) && errno !=EEXIST) /* create fifo for this client*/
		{
			perror("mkfifo()");
			r_close(mainFifo);
			return 1;
		}

		if((myFifo = r_open3(myFifoName,O_NONBLOCK,O_RDONLY)) == -1) /* myfifo opened*/
		{
			perror("r_open3()");
			r_close(mainFifo);
			unlink(myFifoName);
		}

		if(pipe(logPipe)) /* open pipes */
		{
			perror("pipe()");
			r_close(mainFifo);
			r_close(myFifo);
			unlink(myFifoName);
			return 1;
		}
		else if(pipe(showResultsPipe))
		{
			perror("pipe()");
			r_close(logPipe[0]);
			r_close(logPipe[1]);
			r_close(mainFifo);
			r_close(myFifo);
			unlink(myFifoName);
			return 1;
		}

		sprintf(logFileName,"%s%d.txt",LOG_FILE_NAME,mypid);
		if(unlink(logFileName) == -1 && errno!=ENOENT) /* unlink old log file if still around */
		{
			perror("unlink()");
			r_close(showResultsPipe[0]);
			r_close(showResultsPipe[1]);
			r_close(logPipe[0]);
			r_close(logPipe[1]);
			r_close(mainFifo);
			r_close(myFifo);
			unlink(myFifoName);
			return 1;
		}

		if((logfd = r_open3(logFileName, O_CREAT | O_WRONLY,LOG_FILE_PERMS)) == -1 && errno!=EEXIST) /*logfile opened*/
		{
			perror("r_open3()");
			r_close(showResultsPipe[0]);
			r_close(showResultsPipe[1]);
			r_close(logPipe[0]);
			r_close(logPipe[1]);
			r_close(mainFifo);
			r_close(myFifo);
			unlink(myFifoName);
			unlink(logFileName);
			return 1;
		}
	}

	/*-----------------------------------------CLIENT LOOP-------------------------------------*/
	while(!error && !isSigInt)
	{	
	    if(!error && r_write(mainFifo,&mypid,sizeof(pid_t)) != sizeof(pid_t) && errno!=EBADF) /*take a turn in queue for server*/
	    {
	    	perror("r_write()");
			error = 1;
	    }

	    while(!error && !isSigInt && (((readBytes = r_read(myFifo,&matrixSizeInBytes,sizeof(int))) < 0 && errno==EAGAIN) || readBytes == 0)) /* if no data from server has arrived send request again*/
	    {
	    	if(!kill(serverPid,0))
	    		kill(serverPid,SIGUSR1);
	    }

	    if(!error && !isSigInt)
	    {	    	
	    	if(readBytes != sizeof(int ))
	    	{
	    		fprintf(stderr, "%s\n","First read failure" );
	    		error =1;
	    	}

	    	if(!error && matrix == NULL) /* allocate matrix if not allocated already*/
	    	{
	    		if((matrix = (double *) malloc(matrixSizeInBytes)) == NULL) 
	    		{
	    			perror("malloc()");
					error = 1;
	    		}
	    		matrixSize = getMatrixSize(matrixSizeInBytes); 
	    	}

	    	if(!error)
	    		recieveMatrix(myFifo,matrix,matrixSizeInBytes); 
	   
	    	if(!error && (pid = fork()) == -1) /* forking first child*/
	    	{
	    		perror("fork()");
	    		error = 1;
	    	}
	    	else if(!pid) /*child1*/
	    	{
	    		r_close(showResultsPipe[0]);
	    		r_close(logPipe[0]);
				r_close(logfd);
				r_close(myFifo);
				r_close(mainFifo);

	    		if((pid = fork()) == -1)
	    		{	
	    			perror("fork()");
	    			error = 1;
	    		}
	    		else if(!pid)/*child2 (child1's child)*/ 
	    		{
	    			if(doOperationOnMatrixAndSaveResults(matrix,matrixSize,getShiftedInverse,logPipe[1],showResultsPipe[1])) /*Shifted inverse matrix operations*/
	    			{
	    				perror("doOperationOnMatrixAndSaveResults()");
	    				error = 1;
	    			}

	    			 /*release all recourses*/
					r_close(showResultsPipe[1]);
					r_close(logPipe[1]);
					if(matrix!=NULL)
						free(matrix);

	    			exit(error);
	    		}

	    		while(r_wait(NULL) > 0); /*wait for all children*/

	    		if(!error && doOperationOnMatrixAndSaveResults(matrix,matrixSize,getShifted2DConvolution,logPipe[1],showResultsPipe[1]))
	    		{
	    			perror("doOperationOnMatrixAndSaveResults()");
	    			error = 1;
	    		}
	    									
				r_close(showResultsPipe[1]);
				r_close(logPipe[1]);
				if(matrix!=NULL)
					free(matrix);

	    		exit(error);
	    	}

	    	while(r_wait(NULL) > 0); /*wait for all children*/

	    	if(!error && writeLogs(logPipe[0],logfd,matrix,matrixSize))
	    	{
	    		perror("writeLogs()");
	    		error = 1;
	    	}

	    	if(!error && sendDataToShowResults(showResultsPipe[0],SHOW_RESULTS_FIFO_NAME))
	    	{
	    		perror("sendDataToShowResults()");
	    		error = 1;
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
	
	r_close(showResultsPipe[0]);
	r_close(showResultsPipe[1]);
	r_close(logPipe[0]);
	r_close(logPipe[1]);
	r_close(logfd);
	r_close(myFifo);
	r_close(mainFifo);
	if(matrix!=NULL)
		free(matrix);
	unlink(myFifoName);
	if(error)
		unlink(logFileName);

	return error;
}
/****************************************************************************************************************************************************************/

static int connectToServer(void)
{
	int newConFifoFd ;
	pid_t mypid = getpid();

	while((newConFifoFd = r_open2(NEW_CONNECTIONS_FIFO,O_WRONLY)) == -1  && errno == ENOENT); /*open fifo for new collections*/
	
	if(newConFifoFd == -1 && errno != ENOENT) 
		return 1;
	
	if(r_write(newConFifoFd,&mypid,sizeof(pid_t)) != sizeof(pid_t)) /*send pid to server*/
	{
		r_close(newConFifoFd);
		return 1;
	}

	return 0;
}

static int sendDataToShowResults(int showResultsPipeReadEnd,const char * fifoName)
{
	results_t results;
	int showResultsFifo = 0;
	int bytesWriten = 0;
	if((showResultsFifo = r_open2(fifoName,O_WRONLY)) == -1 && errno!=ENOENT) /* FIFO has been closed by dying showResults*/ 
		return 1;
	else if(showResultsFifo == -1)
		return 0;

	memset(&results,0,sizeof(results_t));

	results.pid = getpid();
	if(r_read(showResultsPipeReadEnd,&results.result1,sizeof(double)) != sizeof(double) 
		|| r_read(showResultsPipeReadEnd,&results.timeElapsed1,sizeof(double)) != sizeof(double)
		|| r_read(showResultsPipeReadEnd,&results.result2,sizeof(double)) != sizeof(double)
		|| r_read(showResultsPipeReadEnd,&results.timeElapsed2,sizeof(double)) != sizeof(double))
	{
		return 1;
	}

	bytesWriten = r_write(showResultsFifo,&results,sizeof(results_t));
	r_close(showResultsFifo);

	return (bytesWriten == sizeof(results_t) ? 0 : (errno == EBADF ? 0 : 1) );
}

static int writeLogs(int logPipeReadEnd,int logfd,const double* originalMatrix,int matrixSize)
{
	int i;
	double* matrix = NULL;
	int error = 0;
	char buffer[BUF_SIZE];
	int matrixSizeInBytes = matrixSize*matrixSize*sizeof(double);
	memset(buffer,0,BUF_SIZE);

	if(writeMatrixTo(logfd,originalMatrix,matrixSize,"Original matrix:\n"))
		error = 1;
	
	if(!error && (matrix = malloc(matrixSizeInBytes)) == NULL)
		error = 1;

	for(i = 0; i < 2 && !error; ++i)
	{
		if(!error && r_read(logPipeReadEnd, matrix, matrixSizeInBytes) != matrixSizeInBytes)
			error = 1;

		if(i == 0)
			sprintf(buffer,"%s","Shifted inverse matrix:\n");
		else
			sprintf(buffer,"%s","Shifted 2d convolution matrix:\n");

		if(writeMatrixTo(logfd,matrix,matrixSize,buffer))
			error = 1;
	}

	if(matrix!=NULL)
		free(matrix);

	return error;	
}

static int writeMatrixTo(int fd,const double* matrix,int matrixSize,char* message)
{
	int i,j;
	char buffer[BUF_SIZE];
	char newLine = '\n';
	memset(buffer,0,BUF_SIZE);

	if(r_write(fd,message,strlen(message)) != strlen(message))
		return 1;

	for(i = 0; i < matrixSize; ++i)
	{
		for(j = 0; j < matrixSize; ++j)
		{
			sprintf(buffer,"%.5lf ",matrix[get1dIndex(i,j,matrixSize)]);
			if(r_write(fd,buffer,strlen(buffer)) != strlen(buffer))
				return 1;
		}
		if(r_write(fd,&newLine,sizeof(char)) != sizeof(char))
			return 1;
	}

	return 0;
}

static int doOperationOnMatrixAndSaveResults(double* matrix,int size,int(*doOperationOnMatrix)(double *,int),int logPipeWriteEnd,int showResultsPipeWriteEnd)
{
	double secondsPassed = 0;
	double originalMatrixDet = 0;
	double matrixDetAfterOperation = 0;
	double result = 0;
	struct timespec startTime;
	struct timespec endTime;
	int matrixSizeInBytes = size*size*sizeof(double);

	if(clock_gettime(CLOCK_REALTIME,&startTime))
		return 1;
	if(findDeterminant(matrix,size,&originalMatrixDet))
		return 1;
	doOperationOnMatrix(matrix,size);                     /*find either shifted inverse or shifted 2d convolution matrix*/
	if(findDeterminant(matrix,size,&matrixDetAfterOperation))
		return 1;
	result = originalMatrixDet - matrixDetAfterOperation;
	if(clock_gettime(CLOCK_REALTIME,&endTime))
		return 1;
	secondsPassed = countSecondsPassed(&startTime,&endTime);
	if(r_write(logPipeWriteEnd,matrix,matrixSizeInBytes) != matrixSizeInBytes)
		return 1;
	if(r_write(showResultsPipeWriteEnd,&result,sizeof(double)) != sizeof(double) || r_write(showResultsPipeWriteEnd,&secondsPassed,sizeof(double)) != sizeof(double))
		return 1;
	return 0;
}

static double countSecondsPassed(const struct timespec* start,const struct timespec* end)
{
	return (double)(end->tv_sec - start->tv_sec) +
		(double)(end->tv_nsec - start->tv_nsec)/BILLION;
}

static int getMatrixSize(int matrixSizeInBytes)
{
	return pow((matrixSizeInBytes / sizeof(double)),0.5);
}

static void recieveMatrix(int fifo,double* matrix,int matrixSizeInBytes)
{
	int readTotal = 0;
	int readBytes = 0;
	char* buf = (void*) matrix;

	while(readTotal < matrixSizeInBytes)
	{
		readBytes = r_read(fifo,buf+readTotal,1);
		if(readBytes!= -1)
        	readTotal += readBytes;
	}
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