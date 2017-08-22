#include "list_dir.h"

static int fork_is_safe; /* fork is safe when fork_is_safe == 0 */
static pthread_mutex_t fork_mutex;  
static pthread_cond_t fork_cond;
static pthread_mutex_t shared_mem_mutex; /*process shared memory mutex*/


/*------------------------------------------------------------ DEFINE GLOBAL POINTER TO SHARED_MEM_T OBJECT*/
static shared_mem_t* shared_mem_ptr = NULL;

static int getGloabalError(void); /*get this process error state and notify other processes through message queue if error was detected */
static int createNewThread(my_thread_t* threads,int* threadsSize,void*(*func)(void*),const char*fileName,const char* string,FILE * outputp);
static int countEntriesInThisDir(int whichEntries);
static int allocateMyThreadTArray(my_thread_t** threads,int capacity);
static int allocatePidsArray(pid_t** pids,int capacity);
static void* searchThread(void * arg);
static int getInterProcessError(void); /*check message queue for presence of inter-process error, if detected recend and return 1, otherwise do nothing and return 0*/ 
static int resendSignal(pid_t* pids,int pidsSize,int signal);
static int msgwrite(int message_queue_id, void *buf, int len, int mtype);
static int detachandremove(int shmid, void *shmaddr);

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
	int shared_mem_id = 0; /* -------- shared memory id */

	/* ---------------------------------FIELDS TO FILL IN INTO MESSAGE*/
	int totalFilesSearched = 0;
	int totalThreadsCreated = 0; 
	logs_t message; /*message to send*/

	pid_t* pids = NULL;
	int pidsSize = 0;
	my_thread_t* threads = NULL;
	int threadsSize = 0;

	long pathMax = pathconf("/",_PC_PATH_MAX);
	long nameMax = pathconf("/",_PC_NAME_MAX);

	fork_is_safe = 0; 

	if(string == NULL || directory == NULL || logFilep == NULL)
		return 1;

	if(nameMax == -1 || pathMax == -1)
	{
		printDebug("pathconf()");
		return 1;
	}

	fullEntryPathSize = pathMax + nameMax;

	/* --------------------------------------------------------------------------------------------------CREATE SHARED MEMORY AND ATTACH TO THIS PROCESS*/
	if(!error && ((shared_mem_id = shmget(IPC_PRIVATE,sizeof(shared_mem_t),SHARED_MEM_PERM)) == -1 
		|| (shared_mem_ptr = (shared_mem_t*) shmat(shared_mem_id,NULL,0)) == (void*)-1 ))
	{
		printDebug("shmget() or shmat()");
		error = 1;
	} 

	if(!error && pthread_mutex_init(&shared_mem_mutex,NULL)) /*initialize mutex*/
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
					if(pthread_mutex_destroy(&shared_mem_mutex) )
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

					fclose(logFilep);

					exit(0);
				}	
				else
				{
					++pidsSize;
				}
			}									
			else if(S_ISREG(statbuf.st_mode))   /*################################ NEW THREAD ##########################################*/
			{
				++totalFilesSearched;
				++totalThreadsCreated;
				if(createNewThread(threads,&threadsSize,searchThread,dirEntry.d_name,string,logFilep))
				{
					printDebug("createNewThread()");
					error = 1;
				}
			}							 
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

	/* ------------------------------------------------------------------------- PREPARE AND SEND MESSAGE TO THE MAIN PROCESS */
	message.totalStringsFound = shared_mem_ptr->totalStringsFound;
 	message.totalFilesSearched = totalFilesSearched;
 	message.totalLinesSearched = shared_mem_ptr->totalLinesSearched;
 	message.totalThreadsCreated = totalThreadsCreated;
 	message.error = shared_mem_ptr->error; /* globalError for this process*/
 	message.signal = sigreceived;
 	if(msgwrite(message_queue_id,&message,sizeof(logs_t),LOGS_TYPE)) /*send message*/
 	{
 		printDebug("msgwrite()");
		if(!errnoFirst) errnoFirst = errno;
			error = 1;
 	}


	/* RELEASE ALL RESOURCES */
	if((oldPath && chdir(oldPath))) /*go back to the initial directory*/
	{
		printDebug("chdir()");
		if(!errnoFirst) errnoFirst = errno;
		error = 1;
	}

	/*------------------------------------------------------------------------ DETTACH SHARED_MEM_T OBJECT AND REMOVE IT*/
	if(detachandremove(shared_mem_id,shared_mem_ptr))
	{
		printDebug("detachandremove()");
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

static int getGloabalError(void)
{
	int value = 0;
	mymsg_t error_message;
	error_message.mtype = INTER_PROC_ERROR_TYPE;

	pthread_mutex_lock(&shared_mem_mutex);                              /* LOCK MUTEX*/
	value = shared_mem_ptr->error; /*get error state in this process*/
	pthread_mutex_unlock(&shared_mem_mutex);                            /* UNLOCK MUTEX*/
	
	if(value)/*error in this process, notify the others*/
		msgsnd(message_queue_id,&error_message,sizeof(char),0);

	return value;
}

static int getInterProcessError(void)
{
	int result = 0;
	mymsg_t error_message;

	result = msgrcv(message_queue_id,&error_message,sizeof(char),INTER_PROC_ERROR_TYPE,IPC_NOWAIT);
	if(result != -1)
	{
		error_message.mtype = INTER_PROC_ERROR_TYPE;
		msgsnd(message_queue_id,&error_message,sizeof(char),0);
	}

	return (result == -1 ? 0 : 1);
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

void printDebug(const char* message)
{
	#if DEBUG
		perror(message);
	#endif
}

static void* searchThread(void * arg)
{
	int error = 0;
	int stringsFoundInFile = 0;
	int linesSearchedInFile = 0;
	mymsg_t new_thread_message;
	search_thread_arg_t* searchThreadArgs = (search_thread_arg_t*) arg;
	
	/*---------------------------------------------------------------------- SEND MESSAGE OF CONCURRENT_THREADS_TYPE WITH VALUE 1 TO THE MESSAGE QUEUE */
	new_thread_message.mtype = CONCURRENT_THREADS_TYPE;
	new_thread_message.mtext[0] = 1;
	if(msgsnd(message_queue_id,&new_thread_message,sizeof(char),0))	
	{
		fprintf(stderr, "%s\n","msgsnd()" );
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

	if(pthread_mutex_lock(&shared_mem_mutex))                            /* LOCK MUTEX*/
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock()" );
		return NULL;
	}

	if(!error)
	{
		shared_mem_ptr->totalStringsFound += stringsFoundInFile; 
		shared_mem_ptr->totalLinesSearched += linesSearchedInFile;
	}
	else if (!shared_mem_ptr->error)
		shared_mem_ptr->error = errno; 

	if(pthread_mutex_unlock(&shared_mem_mutex))                            /* UNLOCK MUTEX*/
	{
		fprintf(stderr, "%s\n","pthread_mutex_lock()" ); 
		return NULL;
	}

	/*---------------------------------------------------------------------- SEND MESSAGE OF CONCURRENT_THREADS_TYPE WITH VALUE -1 TO THE MESSAGE QUEUE */
	new_thread_message.mtext[0] = -1;
	if(msgsnd(message_queue_id,&new_thread_message,sizeof(char),0))	
	{
		fprintf(stderr, "%s\n","msgsnd()" );
		return NULL;
	}
	
	return NULL;
}

static int detachandremove(int shmid, void *shmaddr) {
   int error = 0; 

   if (shmdt(shmaddr) == -1)
      error = errno;
   if ((shmctl(shmid, IPC_RMID, NULL) == -1) && !error)
      error = errno;
   if (!error)
      return 0;
   errno = error;
   return -1;
}

static int msgwrite(int message_queue_id, void *buf, int len, int mtype) {     /* output buffer of specified length */ 
   int error = 0;
   mymsg_t *mymsg;
   
   if ((mymsg = (mymsg_t *)malloc(sizeof(mymsg_t) + len - 1)) == NULL)
      return -1;
   memcpy(mymsg->mtext, buf, len);
   mymsg->mtype = mtype;                            /* message type is always 1 */
   if (msgsnd(message_queue_id, mymsg, len, 0) == -1)
      error = errno;
   free(mymsg);
   if (error) {
      errno = error;
      return -1;
   }
   return 0;
}
