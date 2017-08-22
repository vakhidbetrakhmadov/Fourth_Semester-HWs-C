#ifndef LIST_DIR_H
#define LIST_DIR_H

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
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

#include "list.h"
#include "restart.h"

#define DEBUG 0
#define BUF_SIZE 1024
#define D_NAME_SIZE 256
#define LOG_FILE_NAME "log.txt"
#define SEMAPHORE_NAME "main_semaphore"
#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS (O_CREAT | O_EXCL)


typedef struct logs_s
{
	int totalStringsFound;
	int totalDirSearched;
	int totalFilesSearched;
	int totalLinesSearched;
	int maxCascadeThreadsCreated;
	int totalThreadsCreated;
	int currentlyActiveThreads;
	int maxConcurrentlyActive;
	int error;
	int signal;
}logs_t;

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



extern sem_t* main_semaphore; /*named semaphore for inter-process sync*/
extern int pipefd[2];
extern int cascadePipeFd[2];
extern volatile sig_atomic_t sigreceived ;

/* returns 1 on error , 0 on success*/
int traverseDirectory(const char* string, const char* directory, FILE* logFilep);
void printDebug(const char* message);
int getnamed(const char *name, sem_t **sem, int val);
int destroynamed(const char *name, sem_t *sem);
int getDataFromPipe(logs_t* data);
int putDataToPipe(logs_t* data);

#endif