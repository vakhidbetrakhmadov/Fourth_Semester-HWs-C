#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "list_dir.h"
#include "restart.h"

#define THOUSAND 1000
#define MILLION 1000000

sem_t* main_semaphore = NULL; 
int pipefd[2];
int cascadePipeFd[2];
volatile sig_atomic_t sigreceived = 0; 

static double countMiliSecondsPassed(const struct timespec* start,const struct timespec* end);

static void sigIntHandler(int signal);
static int setupSignalHandler(int signal);
static void printCascadeThreadsCreated(void);

int main(int argc,const char* argv[])
{
	sigset_t maskMost;
	struct timespec startTime;
	struct timespec endTime;
	FILE* logFilePtr = NULL;
	int error = 0;
	logs_t data;

	data.totalStringsFound = 0;
	data.totalDirSearched = 0;
	data.totalFilesSearched = 0;
	data.totalLinesSearched = 0;
	data.maxCascadeThreadsCreated = 0;
	data.totalThreadsCreated = 0;
	data.currentlyActiveThreads = 0;
	data.maxConcurrentlyActive = 0;
	data.error = 0;
	data.signal = 0;



	if(argc != 3)
	{
		fprintf(stderr,"Usage: %s 'string' <dirname>\n",argv[0]);
		return 1;
	}

	if(sigfillset(&maskMost) || sigdelset(&maskMost,SIGINT) || sigprocmask(SIG_BLOCK,&maskMost,NULL) || setupSignalHandler(SIGINT)) /*block all signals*/
	{
		printDebug("sigfillset() or sigdelset() or sigprocmask() or setupSignalHandler()");
		error = 1;
	}

	if(!error && clock_gettime(CLOCK_REALTIME,&startTime))
	{
		printDebug("clock_gettime()");
		error = 1;
	}

	if(!error && (logFilePtr = fopen(LOG_FILE_NAME,"w")) == NULL)
	{
		printDebug("fopen()");
		error = 1;
	}	

	if(!error && (pipe(pipefd) || pipe(cascadePipeFd))) /* main pipe creadted*/
	{
		printDebug("pipe()");
		error = 1;
	}

	if(!error && r_write(pipefd[1],&data,sizeof(logs_t)) != sizeof(logs_t))
	{
		printDebug("r_write()");
		error = 1;
	}
 
	if(!error && traverseDirectory(argv[1],argv[2],logFilePtr))
	{
		printDebug("traverseDirectory()");
		error = 1;
	}
	
	if(!error)
		getDataFromPipe(&data);
	
	if(!error && clock_gettime(CLOCK_REALTIME,&endTime))
	{
		printDebug("clock_gettime()");
		error = 1;
	}

	close(cascadePipeFd[1]);

	if(!error)
	{
		fprintf(stdout, "Total number of strings found : %d\n"
				"Number of directories searched : %d\n"
				"Number of files searched : %d\n"
				"Number of lines searched : %d\n"
				,data.totalStringsFound
				,data.totalDirSearched
				,data.totalFilesSearched
				,data.totalLinesSearched);
		
		printCascadeThreadsCreated();

		fprintf(stdout,"Number of search threads created : %d\n"
				"Max of search threads running concurrently : %d\n"
				"Total run time, in milliseconds : %.6lf\n"
				"Exit condition : "
				,data.totalThreadsCreated
				,data.maxConcurrentlyActive
				,countMiliSecondsPassed(&startTime,&endTime));

		if(data.error)
			fprintf(stdout,"due to error %d\n",(int) data.error);
		else if(data.signal)
			fprintf(stdout, "due to signal %d\n", (int) data.signal);
		else
			fprintf(stdout, "normal\n");
	}
	
	fprintf(logFilePtr, "%d %s were found in total.\n",data.totalStringsFound,argv[1]);

	fclose(logFilePtr);
	if(error)
		unlink(LOG_FILE_NAME);

	close(pipefd[0]);
	close(pipefd[1]);
	close(cascadePipeFd[0]);

	if(destroynamed(SEMAPHORE_NAME,main_semaphore) && errno!=EINVAL)
	{
		printDebug("destroynamed()");
		error = 1;
	}

	return error;
}

static double countMiliSecondsPassed(const struct timespec* start,const struct timespec* end)
{
	return (double)(end->tv_sec - start->tv_sec)*THOUSAND +
		(double)(end->tv_nsec - start->tv_nsec)/MILLION;
}

static int setupSignalHandler(int signal)
{
    struct sigaction act;
    memset(&act,'\0',sizeof(struct sigaction));
    act.sa_handler = sigIntHandler;
    return (sigfillset(&act.sa_mask) || sigaction(signal,&act,NULL));
}

static void sigIntHandler(int signal)
{
	sigreceived = signal;
}

static void printCascadeThreadsCreated(void)
{
	int next = 0;
	fprintf(stdout, "%s","Number of cascade threads created : " );
	while(r_read(cascadePipeFd[0],&next,sizeof(int)) > 0)
	{
		fprintf(stdout, "%d ",next);
	}
	fprintf(stdout, "\n");
}