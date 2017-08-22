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

#include "list.h"

#define FIFO_PERMS (S_IRWXU | S_IWGRP| S_IWOTH)

/* returns 1 on error , 0 on success*/
int traverseDirectory(const char* string, const char* directory, FILE* logFilep, int fifofd);
int totalStrings(int fifo);

#endif