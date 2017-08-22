#ifndef CLIENTS_H
#define CLIENTS_H

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "uici.h"
#include "uiciname.h"
#include "restart.h"
#include "strerror_r.h"

/* ------- Debug settings ------- */
#define DEBUG 1
#if DEBUG
#define err(str) perror_r(str)
#else
#define err(str)
#endif
/* ------- End ------- */

int setup_signal_handler(int signal);
int setup_args(int m, int p, u_port_t port, const char* hostname);
int start_logger(void);
int stop_logger(void);
int start_clients(int _clients_num);
int join_clients(void);

#endif