#include "list_dir.h"

static pid_t r_wait(int *stat_loc);
static int fromPipeToFifo(int fifo,int pipe);
static ssize_t r_read(int fd, void *buf, size_t size);
static ssize_t r_write(int fd, void *buf, size_t size);

/* returns 1 on error , 0 on success*/
int traverseDirectory(const char* string,const char* directory,FILE* logFilep,int fifofd) /*----------------------------------------------------------- PASS FIFO FD*/
{	
	int childExitStatus = 0;
	int error = 0;
	int stringsFoundInFile = 0;
	int pid = 0;
	struct stat statbuf;
	struct dirent dirEntry;
	struct dirent* dirEntryPtr = NULL; 
	DIR* dirp = NULL;
	char* path = NULL;
	char* fullEntryPath = NULL;
	char* oldPath = NULL;
	long fullEntryPathSize = 0;
	long pathMax = pathconf("/",_PC_PATH_MAX);
	long nameMax = pathconf("/",_PC_NAME_MAX);

	/*--------------------------------------------------------------------------------------------------------------------------------------DECLARE PIPE FDs*/
	int pipefd[2];
	char pipebuf[PIPE_BUF];

	if(nameMax == -1 || pathMax == -1)
	{
		fprintf(stderr, "%s\n","pathconf()");
		return 1;
	}
	fullEntryPathSize = pathMax + nameMax;

	if(string == NULL || directory == NULL || logFilep == NULL)
		return 1;

	oldPath = (char*) calloc(pathMax,sizeof(char));
	if(oldPath == NULL)
	{
		perror("calloc()");
		return 1;
	}

	if(getcwd(oldPath,pathMax) == NULL)
	{
		perror("getcwd()");
		free(oldPath);		
		return 1;
	}

	if(chdir(directory))
	{
		perror("chdir()");
		free(oldPath);
		return 1;
	}

	path = (char*) calloc(pathMax,sizeof(char));
	if(path == NULL)
	{
		perror("calloc()");
		free(oldPath);
		return 1;
	}

	fullEntryPath = (char*) calloc(fullEntryPathSize,sizeof(char));
	if(fullEntryPath == NULL)
	{
		perror("calloc()");
		free(oldPath);
		free(path);
		return 1;
	}
	fullEntryPath[0] = '\0';

	if(getcwd(path,pathMax) == NULL)
	{
		perror("getcwd()");
		free(oldPath);
		free(path);
		free(fullEntryPath);		
		return 1;
	}

	if((dirp = opendir(".")) == NULL)
	{
		perror("opendir()");
		free(oldPath);
		free(path);
		free(fullEntryPath);
		return 1;
	}

	/* ---------------------------------------------------------------------------------------------------------------------------------CREATE PIPE*/
	if(pipe(pipefd) == -1)
	{
		perror("pipe()");
		free(oldPath);
		free(path);
		free(fullEntryPath);
		closedir(dirp);
		return 1;
	}

	while( !readdir_r(dirp,&dirEntry,&dirEntryPtr) && dirEntryPtr!=NULL )
	{
		if(!(strcmp(".",dirEntry.d_name)) || !(strcmp("..",dirEntry.d_name)))
		{
			/*skip*/
		}
		else
		{
			strcpy(fullEntryPath,path);
			strcat(fullEntryPath,"/");
			strcat(fullEntryPath,dirEntry.d_name);
			if(lstat(fullEntryPath,&statbuf))
			{
				perror("stat()");
				free(oldPath);
				free(fullEntryPath);
				free(path);
				closedir(dirp);
				close(pipefd[0]);
				close(pipefd[1]);
				return 1;
			}

			if(S_ISDIR(statbuf.st_mode))
			{
				if((pid = fork()) == -1)
				{
					perror("fork()");
					free(oldPath);
					free(fullEntryPath);
					free(path);
					closedir(dirp);
					close(pipefd[0]);
					close(pipefd[1]);
					return 1;
				}
				else if(pid == 0)
				{
					free(fullEntryPath);
					free(path);
					free(oldPath);
					close(pipefd[0]);
					close(pipefd[1]);
					closedir(dirp);
					
					if(traverseDirectory(string,dirEntry.d_name,logFilep,fifofd)) /*----------------------------------------------------------- PASS FIFO FD*/
					{
						fclose(logFilep);
						close(fifofd);
						exit(1);
					}
					
					fclose(logFilep);
					close(fifofd);
					exit(0);
				}
				else
				{
					/*this is for parant*/
				}
			}
			else if(S_ISREG(statbuf.st_mode))
			{
				if((pid = fork()) == -1)
				{
					perror("fork()");
					free(oldPath);
					free(fullEntryPath);
					free(path);
					closedir(dirp);
					close(pipefd[0]);
					close(pipefd[1]);
					return 1;
				}
				else if(pid == 0) 
				{
					/* ---------------------------------------------------------------------------------------------------CLOSE PIPE READ END FOR CHILD*/
					close(pipefd[0]);
					free(fullEntryPath);
					free(path);
					free(oldPath);
					closedir(dirp);
					
					stringsFoundInFile = findAllStringsInFile(dirEntry.d_name,string,logFilep);
					if(stringsFoundInFile == -1)
					{
						/* ---------------------------------------------------------------------------------------------------CLOSE PIPE WRITE END FOR CHILD*/
						close(pipefd[1]);
						fclose(logFilep);
						close(fifofd);
						exit(1);
					}
					else
					{
						/* ----------------------------------------------------------------------------------------------------------------WRITE TO PIPE*/
						memcpy(pipebuf,&stringsFoundInFile,sizeof(int));
						if(r_write(pipefd[1],pipebuf,sizeof(int)) != sizeof(int))
						{
							close(pipefd[1]);
							fclose(logFilep);
							close(fifofd);
							exit(1);
						} 
					}

					/* ---------------------------------------------------------------------------------------------------CLOSE PIPE WRITE END FOR CHILD*/
					close(pipefd[1]);
					fclose(logFilep);
					close(fifofd);
					exit(0);
				}
				else
				{
					/*this is for parant*/ 
				}
			}
			else
			{
				/*any other entries are ignored*/
			}
		}
	}

	/*------------------------------------------------------------------------------------------------------------------------CLOSE PIPE WRITE END OF PARENT*/

	if(close(pipefd[1]) || chdir(oldPath))
	{
		perror("chdir()");
		free(fullEntryPath);
		free(path);
		free(oldPath);
		close(pipefd[0]);
		closedir(dirp);
		return 1;
	}
	
	free(fullEntryPath);
	free(path);
	free(oldPath);
	closedir(dirp);

	while(r_wait(&childExitStatus) > 0)
	{
		if(WEXITSTATUS(childExitStatus) == 1)
			error = 1;
	}

	/*------------------------------------------------------------------------------------------------------------------------TRANSFER FROM PIPE TO FIFO AND CLOSE PIPE READ END FOR PARENT*/
	if(fromPipeToFifo(fifofd,pipefd[0]))
		error =1;


	if(close(pipefd[0]) || error)
		return 1;

	return 0;
}

int fromPipeToFifo(int fifo,int pipe)
{
	int bytesRead = 0;
	char buffer[PIPE_BUF];

	while( (bytesRead = r_read(pipe,buffer,PIPE_BUF)) > 0 )
	{
		if(r_write(fifo,buffer,bytesRead) != bytesRead)
		{
			return -1;
		}
	}

	return bytesRead;
}

pid_t r_wait(int *stat_loc) {
	int retval;
	while (((retval = wait(stat_loc)) == -1) && (errno == EINTR)) ;
	return retval;
}

int totalStrings(int fifo)
{
	int sum = 0;
	int nextNum = 0;
	int bytesRead = 0;

	while((bytesRead = r_read(fifo,&nextNum,sizeof(int))) > 0)
		sum+=nextNum;

	return (bytesRead == -1 ? -1 : sum);
}

ssize_t r_write(int fd, void *buf, size_t size) {
   char *bufp;
   size_t bytestowrite;
   ssize_t byteswritten;
   size_t totalbytes;

   for (bufp = buf, bytestowrite = size, totalbytes = 0;
        bytestowrite > 0;
        bufp += byteswritten, bytestowrite -= byteswritten) {
      byteswritten = write(fd, bufp, bytestowrite);
      if ((byteswritten) == -1 && (errno != EINTR))
         return -1;
      if (byteswritten == -1)
         byteswritten = 0;
      totalbytes += byteswritten;
   }
   return totalbytes;
}

ssize_t r_read(int fd, void *buf, size_t size) {
   ssize_t retval;
   while (retval = read(fd, buf, size), retval == -1 && errno == EINTR) ;
   return retval;
}