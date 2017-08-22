#include <stdio.h>
#include <unistd.h>

#include "list_dir.h"

int main(int argc,const char* argv[])
{
	FILE* logFilep = NULL;
	int rwfd = 0;
	int rfd = 0;
	int total = 0;

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

	/*----------------------------------------------------------- CREATE FIFO*/
	if((mkfifo("fifo",FIFO_PERMS) == -1) && (errno!=EEXIST))
	{
		perror("mkfifo()");
		fclose(logFilep);
		unlink("log.txt");
		return 1;
	}

	
	/*----------------------------------------------------------- OPEN FIFO FOR WRITING AND READING */
	while((rwfd = open("fifo",O_RDWR)) == -1 && (errno == EINTR));
	if(rwfd == -1)
	{
		perror("open()");
		fclose(logFilep);
		unlink("fifo");
		unlink("log.txt");
	}
	
 
	if(traverseDirectory(argv[1],argv[2],logFilep,rwfd))/*----------------------------------------------------------- PASS FIFO FD FOR WRITING AND READING*/
	{
		fprintf(stderr, "%s\n","Error!");
		fclose(logFilep);
		close(rwfd);
		unlink("fifo");
		unlink("log.txt");
		return 1;
	}

	/*----------------------------------------------------------- OPEN FIFO FOR READING ONLY */
	while((rfd = open("fifo",O_RDONLY)) == -1 && (errno == EINTR));
	if(rfd == -1)
	{
		perror("open()");
		fclose(logFilep);
		close(rwfd);
		unlink("fifo");
		unlink("log.txt");
	}

	/*----------------------------------------------------------- CLOSE FIFO FD FOR WRITING AND READING*/
	if( (close(rwfd) == -1) || ((total = totalStrings(rfd)) == -1) )
	{
		perror("close() or totalStrings()");
		fclose(logFilep);
		close(rfd);
		unlink("fifo");
		unlink("log.txt");
		return 1;
	}

	fprintf(logFilep, "\n%d %s were found in total.",total,argv[1]); /*----------------------------------------------------------- WRITE FROM FIFO TO FILE*/

	/*----------------------------------------------------------- CLOSE FIFO  FOR READING ONLY*/
	close(rfd);
	/*----------------------------------------------------------- UNLINK FIFO*/
	unlink("fifo");

	fclose(logFilep);

	return 0;
}