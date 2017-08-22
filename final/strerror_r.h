#ifndef STRERROR_H
#define STRERROR_H

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

int strerror_r(int errnum, char *strerrbuf, size_t buflen);
int perror_r(const char *s);

#endif