#ifndef SERVE_REQUES_H
#define SERVE_REQUES_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>

#include "restart.h"
#include "strerror_r.h"
#include "linear.h"

/* ------- Debug settings ---- */
#define DEBUG 1
#if DEBUG
#define err(str) perror_r(str)
#else
#define err(str)
#endif
/* ----------- End ----------- */

/* ------  Global variables ----------*/
extern volatile sig_atomic_t sigreceived;
extern int pipe_logs_fd[2];

extern int passive_sock_fd; /* made global, so that could be closed at fork */
extern int file_logs_fd;  /* made global, so that could be closed at fork */
/* ------------  End -----------------*/


void* serve_request(void* args);
int wait_args_delivery(int* private_sock_fd);
int wait_requests_being_served(void);
void* write_to_screen(void* args);
int signal_other_thread(void);

#endif