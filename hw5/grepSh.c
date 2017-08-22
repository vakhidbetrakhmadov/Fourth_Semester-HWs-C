#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "list_dir.h"
#include "restart.h"

#define THOUSAND 1000
#define MILLION 1000000

volatile sig_atomic_t sigreceived = 0; 
/* --------------------------------------------------------------------------------------------------------- DEFINE GLOBAL ID FOR MESSAGE_QUEUE */
int message_queue_id = 0;

typedef struct results_s
{
	int totalStringsFound;
	int totalDirSearched;
	int totalFilesSearched;
	int totalLinesSearched;
	int totalThreadsCreated;
	int maxConcurrentlyActive;
	int error;
	int signal;
} results_t;

static double countMiliSecondsPassed(const struct timespec* start,const struct timespec* end);
static void sigintHandler(int signal);
static int setupSignalHandler(int signal);
static int read_all_messages(int message_queue_id, results_t* results, int** cascade_threads,int* cascade_threads_size, int* cascade_threads_capacity);
static int add_to_cascade_threads(int** cascade_threads, int* cascade_threads_size, int* cascade_threads_capacity, int next_value);
static void print_shared_memories_logs(int total_created);
static void print_cascade_threads_logs(const int* cascade_threads, int cascade_threads_size);

int main(int argc,const char* argv[])
{
	sigset_t maskMost;
	struct timespec startTime;
	struct timespec endTime;
	FILE* logFilePtr = NULL;
	int error = 0;

	int* cascade_threads = NULL;
	int cascade_threads_size = 0;
	int cascade_threads_capacity = 0;
	results_t results;
	memset(&results,'\0',sizeof(results_t));

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

	/* ----------------------------------------------------------------------------------- OPEN MESSAGE QUEUE*/	
	if(!error && (message_queue_id = msgget(IPC_PRIVATE,MESSAGE_QUEUE_PERM)) == -1)
	{
		printDebug("msgget()");
		error = 1;
	}
 
	if(!error && traverseDirectory(argv[1],argv[2],logFilePtr))
	{
		printDebug("traverseDirectory()");
		error = 1;
	}
	
	if(!error && clock_gettime(CLOCK_REALTIME,&endTime))
	{
		printDebug("clock_gettime()");
		error = 1;
	}

	/*-------------------------------------------------------------------------------------------------------------------------- READ ALL MESSAGES */
	if(!error && read_all_messages(message_queue_id,&results,&cascade_threads,&cascade_threads_size,&cascade_threads_capacity))
	{
		printDebug("read_all_messages()");
		error = 1;
	}

	/* ------------------------------------------------------------------- CHANGE PRINTS */
	if(!error)
	{
		fprintf(stdout, "Total number of strings found : %d\n"
				"Number of directories searched : %d\n"
				"Number of files searched : %d\n"
				"Number of lines searched : %d\n"
				,results.totalStringsFound
				,results.totalDirSearched
				,results.totalFilesSearched
				,results.totalLinesSearched);

		print_cascade_threads_logs(cascade_threads,cascade_threads_size);
		
		fprintf(stdout,"Number of search threads created : %d\n"
				"Max of search threads running concurrently : %d\n"
				,results.totalThreadsCreated
				,results.maxConcurrentlyActive);

		print_shared_memories_logs(results.totalDirSearched);

		fprintf(stdout,"Total run time, in milliseconds : %.6lf\n"
						"Exit condition : "
						,countMiliSecondsPassed(&startTime,&endTime));

		if(results.error)
			fprintf(stdout,"due to error %d\n",(int) results.error);
		else if(results.signal)
			fprintf(stdout, "due to signal %d\n", (int) results.signal);
		else
			fprintf(stdout, "normal\n");
	}
	
	fprintf(logFilePtr, "%d %s were found in total.\n",results.totalStringsFound,argv[1]);

	/* ------------------------------------------------------------------------------------ DEALLOCATE MESSAGE QUEUE*/
	if(message_queue_id != -1 && msgctl(message_queue_id, IPC_RMID, NULL))
		printDebug("msgctl()");

	if(cascade_threads)
		free(cascade_threads);

	fclose(logFilePtr);
	if(error)
		unlink(LOG_FILE_NAME);

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
    act.sa_handler = sigintHandler;
    return (sigfillset(&act.sa_mask) || sigaction(signal,&act,NULL));
}

static void sigintHandler(int signal)
{
	sigreceived = signal;
}

static int read_all_messages(int message_queue_id, results_t* results, int** cascade_threads,int* cascade_threads_size, int* cascade_threads_capacity)
{
	int error = 0;
	int bytes_read = 0;
	mymsg_t* logs_message = NULL;
	logs_t* logs_ptr = NULL;
	mymsg_t thread_in_out_message;
	int max_concurrently_active = 0;
	
	if(!error && (logs_message = (mymsg_t*) malloc(sizeof(mymsg_t) + sizeof(logs_t) - 1)) == NULL)
		error = 1;
	
	while(!error && (bytes_read = msgrcv(message_queue_id,logs_message,sizeof(logs_t),LOGS_TYPE,IPC_NOWAIT)) > 0)
	{
		if(bytes_read != sizeof(logs_t))
		{
			error  = 1;
		}
		else
		{
			logs_ptr = (logs_t* ) logs_message->mtext;

			results->totalStringsFound += logs_ptr->totalStringsFound;
			++(results->totalDirSearched);
			results->totalFilesSearched += logs_ptr->totalFilesSearched;
			results->totalLinesSearched += logs_ptr->totalLinesSearched;
			results->totalThreadsCreated += logs_ptr->totalThreadsCreated;
			if(results->error == 0) results->error = logs_ptr->error;
			if(results->signal == 0) results->signal = logs_ptr->signal;
			if(add_to_cascade_threads(cascade_threads,cascade_threads_size,cascade_threads_capacity,logs_ptr->totalThreadsCreated))
				error = 1;
		}
	}
	if(!error && bytes_read == -1 && errno != ENOMSG)
		error = 1;

	while(!error && (bytes_read = msgrcv(message_queue_id,&thread_in_out_message,sizeof(char),CONCURRENT_THREADS_TYPE,IPC_NOWAIT) > 0))
	{
		max_concurrently_active += thread_in_out_message.mtext[0];
		if(max_concurrently_active > results->maxConcurrentlyActive)
			results->maxConcurrentlyActive = max_concurrently_active;
	}

	if(!error && bytes_read == -1 && errno != ENOMSG)
		error = 1;

	if(logs_message)
		free(logs_message);

	return error;
}

static int add_to_cascade_threads(int** cascade_threads, int* cascade_threads_size, int* cascade_threads_capacity, int next_value)
{

	if(*cascade_threads == NULL)
	{
		*cascade_threads_capacity = 100;
		*cascade_threads_size = 0;
		if((*cascade_threads = (int*) calloc(sizeof(int),*cascade_threads_capacity)) == NULL)
			return 1;
	}
	else if(*cascade_threads_size >= *cascade_threads_capacity)
	{
		*cascade_threads_capacity *= 2;
		if((*cascade_threads = (int*) realloc(*cascade_threads,*cascade_threads_capacity)) == NULL)
			return 1;
	}

	(*cascade_threads)[*cascade_threads_size] = next_value;
	++(*cascade_threads_size);
	return 0;
}

static void print_cascade_threads_logs(const int* cascade_threads, int cascade_threads_size)
{
	int i = 0;
	fprintf(stdout,"Number of cascade threads created: ");
	for(i = 0; i < cascade_threads_size; ++i)
		fprintf(stdout, "%d ",cascade_threads[i]);
	fprintf(stdout,"\n");
}

static void print_shared_memories_logs(int total_created)
{
	int i = 0;
	fprintf(stdout, "Shared memory created: %d sizes: ",total_created);
	for(i = 0; i < total_created; ++i)
		fprintf(stdout, "%d ",(int)sizeof(shared_mem_t));
	fprintf(stdout, "\n");
}