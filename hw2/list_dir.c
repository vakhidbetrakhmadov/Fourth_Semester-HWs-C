#include "list_dir.h"

static pid_t r_wait(int *stat_loc);

/* returns 1 on error , 0 on success*/
int traverseDirectory(const char* string,const char* directory,FILE* logFilep,FILE * tempFilep)
{	
	int childExitStatus = 0;
	int error = 0;
	int stringsFoundInFile = 0;
	int pid = 0;
	struct stat statbuf;
	struct dirent* dirEntry = NULL;
	DIR* dirp = NULL;
	char* path = NULL;
	char* fullEntryPath = NULL;
	char* oldPath = NULL;
	long fullEntryPathSize = 0;
	long pathMax = pathconf("/",_PC_PATH_MAX);
	long nameMax = pathconf("/",_PC_NAME_MAX);
	if(nameMax == -1 || pathMax == -1)
	{
		fprintf(stderr, "%s\n","pathconf()");
		return 1;
	}
	fullEntryPathSize = pathMax + nameMax;

	if(string == NULL || directory == NULL || logFilep == NULL || tempFilep == NULL)
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

	while((dirEntry = readdir(dirp)) != NULL)
	{
		if(!(strcmp(".",dirEntry->d_name)) || !(strcmp("..",dirEntry->d_name)))
		{
			/*skip*/
		}
		else
		{
			strcpy(fullEntryPath,path);
			strcat(fullEntryPath,"/");
			strcat(fullEntryPath,dirEntry->d_name);
			if(lstat(fullEntryPath,&statbuf))
			{
				perror("stat()");
				free(oldPath);
				free(fullEntryPath);
				free(path);
				closedir(dirp);
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
					return 1;
				}
				else if(pid == 0)
				{
					free(fullEntryPath);
					free(path);
					free(oldPath);
					closedir(dirp);
					if(traverseDirectory(string,dirEntry->d_name,logFilep,tempFilep))
					{
						fclose(logFilep);
						fclose(tempFilep);
						exit(1);
					}
					
					fclose(logFilep);
					fclose(tempFilep);
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
					return 1;
				}
				else if(pid == 0)
				{
					free(fullEntryPath);
					free(path);
					free(oldPath);
					closedir(dirp);
					stringsFoundInFile = findAllStringsInFile(dirEntry->d_name,string,logFilep);
					if(stringsFoundInFile == -1)
					{
						fclose(logFilep);
						fclose(tempFilep);
						exit(1);
					}
					else
					{
						fprintf(tempFilep,"%d ",stringsFoundInFile);
					}

					fclose(logFilep);
					fclose(tempFilep);
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

	if(chdir(oldPath))
	{
		perror("chdir()");
		free(fullEntryPath);
		free(path);
		free(oldPath);
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

	if(error)
		return 1;

	return 0;
}

pid_t r_wait(int *stat_loc) {
	int retval;
	while (((retval = wait(stat_loc)) == -1) && (errno == EINTR)) ;
	return retval;
}

int totalStrings(FILE* input)
{
	int sum = 0;
	int nextNum = 0;

	rewind(input);

	while(fscanf(input,"%d" ,&nextNum) != EOF)
	{
		sum+=nextNum;
	}

	return sum;
}