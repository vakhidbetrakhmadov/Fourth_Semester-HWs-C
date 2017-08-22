#include "list_dir.h"


static int fork_is_safe; /*modified*/
static pthread_mutex_t fork_mutex; /*modified*/
static pthread_cond_t fork_cond; /*modified*/

static pthread_mutex_t mutex; /*process mutex*/
static int totalStringsFoundInThisDir;
static int totalLinesSearchedInThisDir;
static int globalError;

static int getGloabalError(void);
static int createNewThread(my_thread_t* threads,int* threadsSize,void*(*func)(void*),const char*fileName,const char* string,FILE * outputp);
static int countEntriesInThisDir(int whichEntries);
static int allocateMyThreadTArray(my_thread_t** threads,int capacity);
static int allocatePidsArray(pid_t** pids,int capacity);
static void* searchThread(void * arg);
static int getInterProcessError(void);
static int resendSignal(pid_t* pids,int pidsSize,int signal);

enum {COUNT_FILES,COUNT_DIRS};

/* returns 1 on error , 0 on success*/
int traverseDirectory(const char* string,const char* directory,FILE* logFilep) 
{	
	int waitValue = 0;
	int i = 0;
	int errnoFirst = 0;
	int error = 0;
	struct stat statbuf;
	struct dirent dirEntry;
	struct dirent* dirEntryPtr = NULL; 
	DIR* dirp = NULL;
	char* path = NULL;
	char* fullEntryPath = NULL;
	char* oldPath = NULL;
	long fullEntryPathSize = 0;
	int totalRegFilesInThisDir = 0;
	int totalSubDirsInThisDir = 0;
	int totalFilesSearchedInThisDir = 0;
	int totalThreadsCreatedInThisDir = 0; 
	pid_t* pids = NULL;
	int pidsSize = 0;
	logs_t data;
	my_thread_t* threads = NULL;
	int threadsSize = 0;
	long pathMax = pathconf("/",_PC_PATH_MAX);
	long nameMax = pathconf("/",_PC_NAME_MAX);

	totalStringsFoundInThisDir = 0;
	totalLinesSearchedInThisDir = 0;
	globalError = 0;
	fork_is_safe = 0; /*modified*/
	main_semaphore = NULL;

	if(string == NULL || directory == NULL || logFilep == NULL)
		return 1;

	if(nameMax == -1 || pathMax == -1)
	{
		printDebug("pathconf()");
		return 1;
	}

	fullEntryPathSize = pathMax + nameMax;

	if(getnamed(SEMAPHORE_NAME,&main_semaphore,1)) /*create or open named semaphore in new process*/
	{
		printDebug("getnamed()");
		error = 1;
	}

	if(!error && pthread_mutex_init(&mutex,NULL)) /*initialize mutex*/
	{
		printDebug("pthread_mutex_init()");
		error = 1;
	}

	if(!error && (pthread_mutex_init(&fork_mutex,NULL) || pthread_cond_init(&fork_cond,NULL))) /*modified*/
	{
		printDebug("pthread_mutex_init() or pthread_cond_signal()");
		error = 1;
	}

	if(!error && (oldPath = (char*) calloc(pathMax,sizeof(char))) == NULL)
	{
		printDebug("calloc()");
		error = 1;
	}

	if(!error && getcwd(oldPath,pathMax) == NULL) /* save initial working directory*/
	{
		printDebug("getcwd()");
		error = 1;		
	}

	if(!error && chdir(directory)) /*move to subdirectory*/
	{
		printDebug("chdir()");
		error = 1;
	}

	if(!error && (path = (char*) calloc(pathMax,sizeof(char))) == NULL)
	{
		printDebug("calloc()");
		error = 1;
	}

	if(!error && (fullEntryPath = (char*) calloc(fullEntryPathSize,sizeof(char))) == NULL )
	{
		printDebug("calloc()");
		error = 1;
	}
	else if(!error)
	{
		fullEntryPath[0] = '\0';
	}

	if(!error && getcwd(path,pathMax) == NULL) /*get subdirectory's full path*/
	{
		printDebug("getcwd()");
		error = 1;
	}

	if(!error && ((totalRegFilesInThisDir  = countEntriesInThisDir(COUNT_FILES)) == -1 || allocateMyThreadTArray(&threads,totalRegFilesInThisDir)))
	{
		printDebug("error while allocating my_thread_t arr");
		error = 1;
	}

	if(!error && ((totalSubDirsInThisDir = countEntriesInThisDir(COUNT_DIRS)) == -1 || allocatePidsArray(&pids,totalSubDirsInThisDir)))
	{
		printDebug("error while allocating pid_t arr");
		error = 1;
	}

	if(!error && (dirp = opendir(".")) == NULL) /* open subdirectory for reading*/
	{
		printDebug("opendir()");
		error = 1;
	}

	while(!error && !getGloabalError() && !sigreceived && !getInterProcessError() && !readdir_r(dirp,&dirEntry,&dirEntryPtr) && dirEntryPtr!=NULL )
	{
		if(!(strcmp(".",dirEntry.d_name)) || !(strcmp("..",dirEntry.d_name)) || (!(strcmp(LOG_FILE_NAME,dirEntry.d_name)) && !strcmp(oldPath,path)) )  
		{
			/*skip link to the current dir, parent dir and log.txt in initial dir 
			All are loop conditions ... */
		}
		else
		{
			strcpy(fullEntryPath,path);
			strcat(fullEntryPath,"/");
			strcat(fullEntryPath,dirEntry.d_name);
			if(lstat(fullEntryPath,&statbuf))
			{
				printDebug("lstat()");
				error = 1;
			}
			else if(S_ISDIR(statbuf.st_mode)) /********************************* NEW PROCESS ******************************************/
			{
				pthread_mutex_lock(&fork_mutex); /*wait until fork is safe*/ /*modified*/
				while (fork_is_safe > 0)
					pthread_cond_wait(&fork_cond, &fork_mutex);

				if((pids[pidsSize] = fork()) == -1)
				{
					printDebug("fork()");
					error = 1;
				}
				
				pthread_mutex_unlock(&fork_mutex); /*modified*/
				
				if(!error && pids[pidsSize] == 0) 
				{
					if(fullEntryPath)
						free(fullEntryPath);
					if(path)
						free(path);
					if(oldPath)
						free(oldPath);
					if(threads)
						free(threads);
					if(pids)
						free(pids);

					if(closedir(dirp))
					{
						printDebug("closedir()");
						if(!errnoFirst) errnoFirst = errno;
						error = 1;
					}
					if(sem_close(main_semaphore))
					{
						printDebug("sem_close()");
						if(!errnoFirst) errnoFirst = errno;
						error = 1;
					}
					if(pthread_mutex_destroy(&mutex) )
					{
						printDebug("pthread_mutex_destroy()");
						if(!errnoFirst) errnoFirst = errno;
						error = 1;
					}
					if(pthread_cond_destroy(&fork_cond) || pthread_mutex_destroy(&fork_mutex))
					{
						printDebug("pthread_mutex_destroy() or pthread_cond_destroy()");
						if(!errnoFirst) errnoFirst = errno;
						error = 1;
					}

					if(!error && traverseDirectory(string,dirEntry.d_name,logFilep))
					{
						printDebug("traverseDirectory()");
						if(!errnoFirst) errnoFirst = errno;
						error = 1;
					}

					close(pipefd[0]);
					close(pipefd[1]);
					close(cascadePipeFd[0]);
					close(cascadePipeFd[1]);
					fclose(logFilep);

					exit(0);
				}	
				else
				{
					++pidsSize;
				}
			}									/********************************* NEW PROCESS ******************************************/
			else if(S_ISREG(statbuf.st_mode))   /*################################ NEW THREAD ##########################################*/
			{
				++totalFilesSearchedInThisDir;
				++totalThreadsCreatedInThisDir;
				if(createNewThread(threads,&threadsSize,searchThread,dirEntry.d_name,string,logFilep))
				{
					printDebug("createNewThread()");
					error = 1;
				}
			}							 /*################################ NEW THREAD ##########################################*/
			else
			{
				/*any other entries are ignored*/
			}
		}
	}

	errnoFirst = errno;

	if(sigreceived) /*if signal was received resend it to parent and children*/
	{
		if(resendSignal(pids,pidsSize,SIGINT)) /*pids - children pids if any */ 
		{
			printDebug("resendSignal()");
			if(!errnoFirst) errnoFirst = errno;
			error = 1;
		}
	}

	for(i = 0; i < threadsSize; ++i) /*join all threads*/
	{
		if(pthread_join(threads[i].tid,NULL))
		{
			printDebug("pthread_join()");
			if(!errnoFirst) errnoFirst = errno;
				error = 1;
		}
	}

	/*update gloabal data*/
	while(sem_wait(main_semaphore) == -1) /* LOCK SEMAPHORE*/
	{
		if(errno != EINTR)
		{
			fprintf(stderr, "%s\n","sem_wait()");
			if(!errnoFirst) errnoFirst = errno;
			error = 1;
		}
	}
	getDataFromPipe(&data);
	data.totalStringsFound += totalStringsFoundInThisDir;
	data.totalDirSearched++;
	data.totalFilesSearched += totalFilesSearchedInThisDir;
	data.totalLinesSearched += totalLinesSearchedInThisDir;
	
	if(totalThreadsCreatedInThisDir > data.maxCascadeThreadsCreated) data.maxCascadeThreadsCreated = totalThreadsCreatedInThisDir;
	r_write(cascadePipeFd[1],&totalThreadsCreatedInThisDir,sizeof(int));
	
	data.totalThreadsCreated += totalThreadsCreatedInThisDir;
	if(!data.error) data.error = globalError;
	putDataToPipe(&data);
	if(sem_post(main_semaphore))               /* UNLOCK SEMAPHORE*/
	{
		fprintf(stderr, "%s\n","sem_post()");
		if(!errnoFirst) errnoFirst = errno;
		error = 1;
	}

	while((waitValue = wait(NULL)) > 0 || (waitValue == -1 && errno == EINTR)) /*wait for all children*/
	{
		if(sigreceived) /*if signal was received while waiting resend it to the parent and children*/
		{
			if(resendSignal(pids,pidsSize,SIGINT)) /*pids - children pids if any */  
			{
				printDebug("resendSignal()");
				if(!errnoFirst) errnoFirst = errno;
				error = 1;
			}
		}
	}

	/* ######## RELEASE ALL RESOURCES ########*/
	if((oldPath && chdir(oldPath))) /*go back to the initial directory*/
	{
		printDebug("chdir()");
		if(!errnoFirst) errnoFirst = errno;
		error = 1;
	}

	if(fullEntryPath)
		free(fullEntryPath);
	if(path)
		free(path);
	if(oldPath)
		free(oldPath);
	if(threads)
		free(threads);
	if(pids)
		free(pids);
	
	if(closedir(dirp))
	{
		printDebug("closedir()");
		if(!errnoFirst) errnoFirst = errno;
			error = 1;
	}
	/* ######## RELEASE ALL RESOURCES ########*/

	if(sigreceived)
	{
		while(sem_wait(main_semaphore) == -1) /* LOCK SEMAPHORE*/
		{
			if(errno != EINTR)
			{
				fprintf(stderr, "%s\n","sem_wait()");
				if(!errnoFirst) errnoFirst = errno;
				error = 1;
			}
		}
		getDataFromPipe(&data);
		if(!data.signal) data.signal = sigreceived; 
		putDataToPipe(&data);
		if(sem_post(main_semaphore))               /* UNLOCK SEMAPHORE*/
		{
			fprintf(stderr, "%s\n","sem_post()");
			if(!errnoFirst) errnoFirst = errno;
			error = 1;
		}
	}
	
	if(sem_close(main_semaphore))
	{
		printDebug("sem_close()");
		if(!errnoFirst) errnoFirst = errno;
		error = 1;
	}

	errno = errnoFirst;

	return error;
}
/*############################################################################################################################################################################################*/

static int resendSignal(pid_t* pids,int pidsSize,int signal)
{
	int i = 0;
	int error = 0;
	pid_t ppid = 0;
	pid_t gpid = 0;
	pid_t my_pid = 0;
	ppid = getppid();
	gpid = getpgrp();
	my_pid = getpid();

	if(my_pid != gpid && !kill(ppid,0)) /*resend signal to the parent if parent is grepTh process and is still around*/
		if(kill(ppid,signal))
			error = 1;

	for(i = 0;!error && i < pidsSize; ++i)
	{
		if(!kill(pids[i],0)) /*resend signal to the child if the child is still around*/
			if(kill(pids[i],signal))
				error = 1;
	}
	
	return error;
}

static int getInterProcessError(void)
{
	int value = 0;
	logs_t data;
	while(sem_wait(main_semaphore) == -1) /* LOCK SEMAPHORE*/
		if(errno != EINTR)
		{
			fprintf(stderr, "%s\n","sem_wait()");
			return 1;	
		}

	getDataFromPipe(&data);
	value = data.error; 
	putDataToPipe(&data);
	
	if(sem_post(main_semaphore))               /* UNLOCK SEMAPHORE*/
	{
		fprintf(stderr, "%s\n","sem_post()");
		return 1;
	}

	return value;
}

static int allocatePidsArray(pid_t** pids,int capacity)
{
	if(*pids == NULL)
		if((*pids = (pid_t*) calloc(capacity,sizeof(pid_t))) == NULL)
			return 1;
	return 0;
}

static int allocateMyThreadTArray(my_thread_t** threads,int capacity)
{
	if(*threads == NULL)
		if((*threads = (my_thread_t*) calloc(capacity,sizeof(my_thread_t))) == NULL)
			return 1;
	return 0;
}

static int countEntriesInThisDir(int whichEntries)
{
    int count = 0; 
    int error = 0;
    DIR* dirPtr = NULL ;
    char * fullEntryPath = NULL;
    char* path = NULL;
    struct dirent dirEntry;
    struct dirent* result = NULL;
    struct stat statbuf;
    long fullEntryPathSize = 0;
    long pathMax = pathconf("/",_PC_PATH_MAX);
	long nameMax = pathconf("/",_PC_NAME_MAX);

	if(pathMax == -1 || nameMax == -1)
		return 1;

	fullEntryPathSize = pathMax + nameMax;

    if((dirPtr = opendir(".")) == NULL)
        error = 1;
    if(!error && (fullEntryPath = calloc(fullEntryPathSize,sizeof(char))) == NULL)
        error = 1;
    if(!error && (path = calloc(pathMax,sizeof(char))) == NULL)
        error = 1;
    if(!error && getcwd(path,pathMax) == NULL)
        error = 1;

    while(!error && !(error = readdir_r(dirPtr,&dirEntry,&result)) && result!=NULL )
    {
        sprintf(fullEntryPath,"%s/%s",path,dirEntry.d_name);
        if(lstat(fullEntryPath,&statbuf))
        {
            error = 1;
        }
        else if(whichEntries == COUNT_FILES && S_ISREG(statbuf.st_mode))
        {
            ++count;
        }
        else if(whichEntries == COUNT_DIRS && S_ISDIR(statbuf.st_mode))
        {
        	++count;
        }
    }

    if(fullEntryPath)
        free(fullEntryPath);
    if(path)
        free(path);
    if(closedir(dirPtr))
    	error = 1;

    return (error ? -1 : count);
}

static int createNewThread(my_thread_t* threads,int* threadsSize,void*(*func)(void*),const char*fileName,const char* string,FILE * outputp)
{
	int error = 0;

 	strcpy(threads[*threadsSize].args.fileName,fileName);
 	threads[*threadsSize].args.string = string;
 	threads[*threadsSize].args.outputp = outputp;
	if(pthread_create(&threads[*threadsSize].tid,NULL,func,&threads[*threadsSize].args))
		error = 1;
	else
		++(*threadsSize);	

	return error;
}

static int getGloabalError(void)
{
	int value;
	pthread_mutex_lock(&mutex);                            /* LOCK MUTEX*/
	value = globalError;
	pthread_mutex_unlock(&mutex);                            /* UNLOCK MUTEX*/
	return value;
}

void printDebug(const char* message)
{
	#if DEBUG
		perror(message);
	#endif
}

int getnamed(const char *name, sem_t **sem, int val) {
   while (((*sem = sem_open(name, FLAGS , PERMS, val)) == SEM_FAILED) &&
           (errno == EINTR)) ;
   if (*sem != SEM_FAILED)
       return 0;
   if (errno != EEXIST)
      return -1;
   while (((*sem = sem_open(name, 0)) == SEM_FAILED) && (errno == EINTR)) ;
   if (*sem != SEM_FAILED)
       return 0;
   return -1;
}

int destroynamed(const char *name, sem_t *sem) {
    int error = 0;
    if (sem_close(sem) == -1)
       error = errno;
    if ((sem_unlink(name) != -1) && !error)
       return 0;
    if (error)        /* set errno to first error that occurred */
       errno = error;
    return -1;
}

static void* searchThread(void * arg)
{
	int error = 0;
	int stringsFoundInFile = 0;
	int linesSearchedInFile = 0;
	logs_t data;
	search_thread_arg_t* searchThreadArgs = (search_thread_arg_t*) arg;
	
	while(sem_wait(main_semaphore) == -1)      /* LOCK SEMAPHORE*/
		if(errno != EINTR)
		{
			fprintf(stderr, "%s\n","sem_wait()");
			return NULL;
		}	

	getDataFromPipe(&data);
	data.currentlyActiveThreads++;
	if(data.currentlyActiveThreads > data.maxConcurrentlyActive) /*increment number of currently active threads, check for max concurrent threads run */
		data.maxConcurrentlyActive = data.currentlyActiveThreads;
	putDataToPipe(&data);
	
	if(sem_post(main_semaphore))               /* UNLOCK SEMAPHORE*/
	{
		fprintf(stderr, "%s\n","sem_post()");
		return NULL;
	}

	if(pthread_mutex_lock(&fork_mutex))   /* LOCK MUTEX*/ 
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock()" );
		return NULL;
	}
	++fork_is_safe; /*fork is not safe */
	if(pthread_mutex_unlock(&fork_mutex))  /* UNLOCK MUTEX*/
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock()" ); 
		return NULL;
	}

	/* ---- do the main job ---- */
	stringsFoundInFile = findAllStringsInFile(searchThreadArgs->fileName,searchThreadArgs->string,searchThreadArgs->outputp,&linesSearchedInFile); 

	if(pthread_mutex_lock(&fork_mutex)) /* LOCK MUTEX*/
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock()" );
		return NULL;
	}
	--fork_is_safe; /*fork might be safe, notify main thread*/
	
	if(pthread_cond_signal(&fork_cond) || pthread_mutex_unlock(&fork_mutex)) /* UNLOCK MUTEX*/ 
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock() or pthread_cond_signal()" );
		return NULL;
	}

	if(stringsFoundInFile == -1)
		error = 1;

	if(pthread_mutex_lock(&mutex))                            /* LOCK MUTEX*/
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock()" );
		return NULL;
	}

	if(!error)
	{
		totalStringsFoundInThisDir+=stringsFoundInFile;
		totalLinesSearchedInThisDir+=linesSearchedInFile;
	}
	else if (!globalError)
		globalError = errno;

	if(pthread_mutex_unlock(&mutex))                            /* UNLOCK MUTEX*/
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock()" ); 
		return NULL;
	}

	while(sem_wait(main_semaphore) == -1)  /* LOCK SEMAPHORE*/
		if(errno != EINTR)
		{
			fprintf(stderr, "%s\n","sem_wait()");
			return NULL;
		}	

	getDataFromPipe(&data);
	data.currentlyActiveThreads--;
	putDataToPipe(&data);
	
	if(sem_post(main_semaphore))  /* UNLOCK SEMAPHORE*/
	{
		fprintf(stderr, "%s\n","sem_post()");
		return NULL;
	}
	
	return NULL;
}

int getDataFromPipe(logs_t* data)
{
	return r_read(pipefd[0],data,sizeof(logs_t)) != sizeof(logs_t);
}

int putDataToPipe(logs_t* data)
{
	return r_write(pipefd[1],data,sizeof(logs_t)) != sizeof(logs_t);
}
