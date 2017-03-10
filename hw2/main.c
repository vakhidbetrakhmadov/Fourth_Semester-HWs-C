#include <stdio.h>
#include <unistd.h>

#include "list_dir.h"

int main(int argc,const char* argv[])
{
	FILE* logFilep = NULL;
	FILE* tempFilep = NULL;

	if(argc != 3)
	{
		fprintf(stderr,"Usage: %s 'string' <dirname>\n",argv[0]);
		return 1;
	}

	if((logFilep = fopen("log.txt","w")) == NULL)
	{
		perror("fopen()");
		return 1;
	}
	if((tempFilep = fopen("temp","w+")) == NULL)
	{
		perror("fopen()");
		fclose(logFilep);
		return 1;
	}

	if(traverseDirectory(argv[1],argv[2],logFilep,tempFilep))
	{
		fprintf(stderr, "%s\n","Error!");
		fclose(logFilep);
		fclose(tempFilep);
		unlink("temp");
		return 1;
	}

	fprintf(logFilep, "\n%d %s were found in total.",totalStrings(tempFilep),argv[1]);

	fclose(logFilep);
	fclose(tempFilep);

	if(unlink("temp"))
	{
		perror("unlink()");
		return 1;
	}

	return 0;
}