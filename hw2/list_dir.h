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

#include "list.h"

/* returns 1 on error , 0 on success*/
int traverseDirectory(const char* string, const char* directory, FILE* logFilep, FILE * tempFilep);
int totalStrings(FILE* tempFilep);

#endif