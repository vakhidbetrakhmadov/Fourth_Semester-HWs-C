#ifndef LIST_H
#define LIST_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

typedef struct coord_struct
{
	int row;
	int col;
}coord_t;

/*finds coordinates of all strings in given file and prints them into output file*/
/* returns -1 on error, >= 0 on success */
int findAllStringsInFile(const char* fileName, const char* string,FILE* outputp);

#endif