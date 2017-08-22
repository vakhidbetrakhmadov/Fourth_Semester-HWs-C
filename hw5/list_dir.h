#ifndef LIST_DIR_H
#define LIST_DIR_H

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif 
#ifndef _XOPEN_SOURCE 
#define _XOPEN_SOURCE
#endif 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>    
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#include "list.h"
#include "restart.h"

#define DEBUG 0
#define BUF_SIZE 1024
#define D_NAME_SIZE 256
#define LOG_FILE_NAME "log.txt"
#define MESSAGE_QUEUE_PERM (S_IRUSR | S_IWUSR)
#define SHARED_MEM_PERM (S_IRUSR | S_IWUSR)
#define FLAGS (O_CREAT | O_EXCL)

enum{LOGS_TYPE = 1,CONCURRENT_THREADS_TYPE,INTER_PROC_ERROR_TYPE}; /*message types*/

typedef struct {
   long mtype;
   char mtext[1];
} mymsg_t;

/*to include into message*/
typedef struct logs_s
{
	int totalStringsFound;
	int totalFilesSearched;
	int totalLinesSearched;
	int totalThreadsCreated;
	int error;
	int signal;
}logs_t;

/*shared memory for threads*/
typedef struct shared_s
{
	int totalStringsFound;
	int totalLinesSearched;
	int error;
}shared_mem_t;

typedef struct search_thread_arg_s
{
	char fileName[D_NAME_SIZE];
	const char* string;
	FILE* outputp;
}search_thread_arg_t;

typedef struct my_thread_s
{
	pthread_t tid;
	search_thread_arg_t args;
}my_thread_t;

extern volatile sig_atomic_t sigreceived;
extern int message_queue_id;

/* returns 1 on error , 0 on success*/
int traverseDirectory(const char* string, const char* directory, FILE* logFilep);
void printDebug(const char* message);

#endif