#include "clients.h"

#ifndef MAXBACKLOG
#define MAXBACKLOG 50 /* Semaphore value. Related to the passive socket queue size */
#endif

#define BUF_SIZE 1024
#define THOUSAND 1000
#define MILLION 1000000

#define LOGS_FILENAME_PREFIX "clients_logs_"
#define PERMS_LOGS (S_IRWXU | S_IRWXG | S_IRWXO)
#define FLAGS_LOGS (O_CREAT | O_WRONLY)

#define SEMAPHORE_NAME "clients_semaphore"
#define PERMS_SEM (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS_SEM (O_CREAT | O_EXCL)

/*----------- Structs and typedefs ------*/
typedef struct data_s
{
	double* matrix_a;
	double* vector_b;
	double* x1;
	double* x2;
	double* x3;
	double e1;
	double e2;
	double e3;
}data_t;

typedef struct args_s
{
	int m;
	int p;
	u_port_t port;
	char* hostname;
	struct sockaddr_in server;
}args_t;

typedef	struct stats_s
{
	double x_sum;
	double x_sqr_sum;
	int n;
}stats_t;
/* --------------- End ---------------*/

/* ---------------FIle global varibales  --------------- */
static volatile sig_atomic_t sigreceived = 0;

static volatile sig_atomic_t server_is_down = 0;

static sem_t* semaphore = NULL;

static args_t args;

static stats_t stats;
static pthread_mutex_t mutex_stats = PTHREAD_MUTEX_INITIALIZER;

static int pipe_logs_fd[2];
static pthread_mutex_t mutex_pipe_logs = PTHREAD_MUTEX_INITIALIZER;

static int clients_num = 0;
static pthread_t* clients = NULL;

static pthread_t logger_tid;
/* --------------- End --------------- */

/* ----------File functions declaration -------------*/
static void signal_handler(int signal);
static int signal_other_thread(void); /* triggers chain reaction */

static void* write_logs(void* _args); /* thread functions */
static void* connect_2_server(void* _args); /* thread functions */

static int destroynamed(const char *name, sem_t *sem); 
static int getnamed(const char *name, sem_t **sem, int val);

static int send_parameters_2_server(int private_socket_fd);
static int receive_data_from_server(int private_socket_fd);

static int send_logs(data_t* data);
static int send_signal_time_2_logs(void);
static int send_matrix_a_2_logs(int m, int p, double * matrix);
static int send_vector_b_2_logs(int m, double* vector);
static int send_vectors_x_and_e_2_logs(int p, data_t* data);
static int send_connection_stats_2_logs(void);

static int init_data_object(data_t* data);
static void destroy_data_object(data_t* data);

static int update_stats(double connection_time);
static double count_miliseconds_passed(const struct timespec* start,const struct timespec* end);
/* ---------- End -------------*/

/* ---------- Interface functions ----------------*/
int setup_signal_handler(int signal)
{
    struct sigaction act;
    memset(&act,'\0',sizeof(struct sigaction));
    act.sa_handler = signal_handler;
    return (sigfillset(&act.sa_mask) || sigaction(signal,&act,NULL));
}

int setup_args(int m, int p, u_port_t port, const char* hostname)
{
	memset(&args,'\0',sizeof(args_t));
	args.m = m	; /* number of rows*/
	args.p = p; /* number of columns*/
	args.port = port; /* ---- Select the server port number ----- */
	args.hostname = (char*) hostname; /*server name or ip-adress*/

	args.server.sin_port = htons((short)args.port);
	args.server.sin_family = AF_INET;
	if (name2addr(args.hostname,&(args.server.sin_addr.s_addr)) == -1) 
	{
		errno = EINVAL;
		return -1;
	}

	return 0;
}

int start_logger(void)
{
	if(pipe(pipe_logs_fd))
		return 1;
	if(pthread_create(&logger_tid,NULL,write_logs,NULL))
	{
		r_close(pipe_logs_fd[1]);
		r_close(pipe_logs_fd[0]);
		return 1;
	}

	return 0;
}

int stop_logger(void)
{
	int error = 0;

	if(r_close(pipe_logs_fd[1]))
	{
		error = errno;
	}

	if(pthread_join(logger_tid,NULL))
	{
		if(!error)
			error = errno;
	}

	if(r_close(pipe_logs_fd[0]))
	{
		if(!error)
			error = errno;
	}

	if(error)
		errno = error;
	
	return (error ? 1 : 0);
}

int start_clients(int _clients_num)
{	
	int error = 0, i = 0, j = 0;
	clients_num = _clients_num;

	memset(&stats,'\0',sizeof(stats_t));

	if(getnamed(SEMAPHORE_NAME,&semaphore,MAXBACKLOG))
		return 1;

	if((clients = calloc(clients_num,sizeof(pthread_t))) == NULL)
	{
		error = errno;
		destroynamed(SEMAPHORE_NAME,semaphore);
		errno = error;
		return 1;
	}
	
	for(i = 0;!error && i < clients_num; ++i)
	{
		if(pthread_create(&clients[i],NULL,connect_2_server,NULL))
			error = errno;
	}

	if(error)
	{
		for(j = 0; j < i; ++j)
			pthread_cancel(clients[j]);

		destroynamed(SEMAPHORE_NAME,semaphore);
		free(clients);
		errno = error;
		return 1;
	}
	
	return 0;
}

int join_clients(void)
{
	int error = 0, i = 0;

	for(i = 0; i < clients_num; ++i)
	{
		if(pthread_join(clients[i],NULL))
			error = 1;
	}

	if(send_connection_stats_2_logs())
		error = 1;

	destroynamed(SEMAPHORE_NAME,semaphore);
	if(clients)
		free(clients);

	return error;
}
/* ---------- End ----------------*/

/* ---------- File functions implementations -------------*/
static void* connect_2_server(void* _args)
{
	int error = 0;
	double connection_time = 0;
	int private_socket_fd = 0;
	struct timespec start_time;
	struct timespec end_time;
	sigset_t mask_one;

	if(sigemptyset(&mask_one) || sigaddset(&mask_one,SIGINT) || pthread_sigmask(SIG_UNBLOCK,&mask_one,NULL)) /* unblock SIGINT for this thread*/
	{
		err("In send_request: sigemptyset() || sigaddset() || pthread_sigmask()");
		return NULL;
	}

	if(clock_gettime(CLOCK_REALTIME,&start_time)) /* get connection start time */
	{
		err("clock_gettime()");
		return NULL;
	}

	while((error = sem_wait(semaphore)) == -1 && errno == EINTR) /* lock semaphore */
	{
		if(signal_other_thread())
			err("signal_other_thread()");
	}
	if(error)
	{
		err("sem_wait()");
		return NULL;
	}

	if(!sigreceived && !server_is_down)
	{
		if((private_socket_fd = u_connect(args.port,&args.server)) == -1)  /* try to connect to the server */
		{
			server_is_down = 1;
			fprintf(stderr, "%s\n","Failed to connect to the serve. Server is down.");
		}
	}

	if(sem_post(semaphore)) /* unlock semaphore */
	{
		err("sem_post()");
		r_close(private_socket_fd);
		return NULL;
	}

	if(clock_gettime(CLOCK_REALTIME,&end_time)) /* get connection end time */
	{
		err("clock_gettime()");
		r_close(private_socket_fd);
		return NULL;
	}

	connection_time = count_miliseconds_passed(&start_time,&end_time); 
	if(update_stats(connection_time))
	{
		err("update_stats()");
		r_close(private_socket_fd);
		return NULL;
	}

	if(!server_is_down && !sigreceived)
	{
		if((error = send_parameters_2_server(private_socket_fd)) == -1)
		{
			err("send_parameters_2_server()");
		}
		else if(error == 1)
		{
			server_is_down = 1;
		}
		else /*error == 0 */
		{
			if(receive_data_from_server(private_socket_fd))
				err("receive_data_from_server()");
		}
	}

	if(sigreceived)
	{
		if(signal_other_thread())
			err("signal_other_thread()");

		if(send_signal_time_2_logs())
			err("send_signal_time_2_logs()");
	}

	r_close(private_socket_fd);

	return NULL;
}

static int send_connection_stats_2_logs(void)
{
	char buffer[BUF_SIZE];
	int bytes = 0;
	double std_deviation = 0;
	double av_connection_time = 0;
	
	memset(buffer,'\0',BUF_SIZE);

	av_connection_time = stats.x_sum / stats.n;
	std_deviation = sqrt((stats.x_sqr_sum - stats.n * pow(av_connection_time,2)) / (stats.n - 1));	

	bytes = sprintf(buffer,"Average connection time is: %f\n"
							"Standard deviation is: %f\n",av_connection_time,std_deviation);

	return (r_write(pipe_logs_fd[1],buffer,bytes) != bytes);
}

static int send_signal_time_2_logs(void)
{
	char buffer[BUF_SIZE];
	int bytes = 0;
	struct tm* tmtime;
	time_t ttime;
	pid_t pid = 0;
	long tid = 0;

	pid = getpid();
	tid = (long) pthread_self();
	time(&ttime);
	tmtime = localtime(&ttime);

	memset(buffer,'\0',BUF_SIZE);

	bytes = sprintf(buffer,"Client's pid: %d; tid: %ld\n"
		"SIGINT received at %d:%d:%d\n\n",
		pid,tid,tmtime->tm_hour,tmtime->tm_min,tmtime->tm_sec);

	return	(r_write(pipe_logs_fd[1],buffer,strlen(buffer)) != bytes);
}

static int receive_data_from_server(int private_socket_fd)
{
	int bytes_read = 0;
	int error = 0;
	data_t data;
	const int matrix_a_size = sizeof(double)*args.m*args.p;
	const int vector_b_size = sizeof(double)*args.m;
	const int vector_x_size = sizeof(double)*args.p;

	memset(&data,'\0',sizeof(data_t));

	if(init_data_object(&data))
		return 1;

	if(!sigreceived)
	{
		if((bytes_read = read(private_socket_fd,data.matrix_a,matrix_a_size)) != matrix_a_size) 
			error = errno;
		else if((bytes_read = read(private_socket_fd,data.vector_b,vector_b_size)) != vector_b_size) 
			error = errno;
		else if((bytes_read = read(private_socket_fd,data.x1,vector_x_size)) != vector_x_size)
			error = errno;
		else if((bytes_read = read(private_socket_fd,data.x2,vector_x_size)) != vector_x_size)
			error = errno;
		else if((bytes_read = read(private_socket_fd,data.x3,vector_x_size)) != vector_x_size)
			error = errno;
		else if((bytes_read = read(private_socket_fd,&(data.e1),sizeof(double))) != sizeof(double))
			error = errno;
		else if((bytes_read = read(private_socket_fd,&(data.e2),sizeof(double))) != sizeof(double)) 
			error = errno;
		else if((bytes_read = read(private_socket_fd,&(data.e3),sizeof(double))) != sizeof(double)) 
			error = errno;
	}

	if(bytes_read == 0)
	{
		error = 0;
		server_is_down = 1;
	}
	else if(bytes_read == -1 && error == EINTR)
	{
		error = 0;
	}
	else if(bytes_read != -1) /*all reads were successfull*/
	{
		if(send_logs(&data))
			error = errno;
	}
	
	destroy_data_object(&data);

	if(error)
		errno = error;

	return (error ? 1 : 0);
}

static int send_logs(data_t* data)
{
	char buffer[BUF_SIZE];
	int bytes = 0;
	int error = 0;
	pid_t pid = 0;
	long tid = 0;

	pid = getpid();
	tid = (long) pthread_self();

	memset(buffer,'\0',BUF_SIZE);

	if(pthread_mutex_lock(&mutex_pipe_logs))
		return 1;

	bytes = sprintf(buffer,"Client's pid: %d; tid: %ld\n\n",(int)pid,tid);

	if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = errno;
	else if(send_matrix_a_2_logs(args.m,args.p,data->matrix_a))
		error = errno;
	else if(send_vector_b_2_logs(args.m,data->vector_b))
		error = errno;
	else if(send_vectors_x_and_e_2_logs(args.p,data))
		error = errno;

	if(pthread_mutex_unlock(&mutex_pipe_logs))
	{
		if(error)
			errno = error;
		return 1;
	}

	return (error ? 1 : 0);
}

static int send_vectors_x_and_e_2_logs(int p, data_t* data)
{
	int error = 0, i = 0;
	char buffer[BUF_SIZE];
	int bytes = 0;
	char new_line = '\n';

	memset(buffer,'\0',BUF_SIZE);

	bytes = sprintf(buffer,"Vector x1:\n"); 
	if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;

	for(i = 0;!error && i < p; ++i)
	{
		bytes = sprintf(buffer,"%f\n",(data->x1)[i]); 
		if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
			error = 1;
	}

	if(!error && r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;

	bytes = sprintf(buffer,"Vector x2:\n"); 
	if(!error && r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;

	for(i = 0;!error && i < p; ++i)
	{
		bytes = sprintf(buffer,"%f\n",(data->x2)[i]); 
		if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
			error = 1;
	}

	if(!error && r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;

	bytes = sprintf(buffer,"Vector x3:\n"); 
	if(!error && r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;

	for(i = 0;!error && i < p; ++i)
	{
		bytes = sprintf(buffer,"%f\n",(data->x3)[i]); 
		if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
			error = 1;
	}

	if(!error && r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;

	bytes = sprintf(buffer,"Norms of the error terms: e1 = %f , e2 = %f , e3 = %f\n",data->e1,data->e2,data->e3); 
	if(!error && r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;
	
	if(!error && r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;

	return error;
}

static int send_vector_b_2_logs(int m, double* vector)
{
	int error = 0, i = 0;
	char buffer[BUF_SIZE];
	int bytes = 0;
	char new_line = '\n';

	memset(buffer,'\0',BUF_SIZE);

	bytes = sprintf(buffer,"Vector b:\n"); 
	if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;
	
	for(i = 0;!error && i < m; ++i)
	{
		bytes = sprintf(buffer,"%f\n",vector[i]); 
		if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
			error = 1;
	}

	if(!error)
		if(r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;
	

	return error;
}

static int send_matrix_a_2_logs(int m, int p, double * matrix)
{	
	char buffer[BUF_SIZE];
	int bytes = 0;
	int error = 0;
	int i = 0, j = 0, k = 0;
	char new_line = '\n';

	memset(buffer,'\0',BUF_SIZE);

	bytes = sprintf(buffer,"Matrix A:\n");
	if(!error && r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;

	for(i = 0; !error &&  i < m; ++i)
	{
		for(j = 0; !error && j < p; ++j, ++k)
		{
			bytes = sprintf(buffer,"%f ",matrix[k]);
			if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
				error = 1;
		}
		if(r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
				error = 1;
	}

	if(!error)
		if(r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
				error = 1;

	return error;
}

static int init_data_object(data_t* data)
{
	char* ptr = NULL;
	const int matrix_a_size = sizeof(double)*args.m*args.p;
	const int vector_b_size = sizeof(double)*args.m;	
	const int vector_x_size = sizeof(double)*args.p;
	const int total_bytes = matrix_a_size + vector_b_size + (vector_x_size*3);

	if((ptr = malloc(total_bytes)) == NULL)
		return 1;

	memset(ptr,'\0',total_bytes);

	data->matrix_a = (double*) ptr;
	data->vector_b = (double*) (ptr + matrix_a_size);
	data->x1 = (double*) (ptr + (matrix_a_size + vector_b_size));
	data->x2 = (double*) (ptr + (matrix_a_size + vector_b_size + vector_x_size));
	data->x3 = (double*) (ptr + (matrix_a_size + vector_b_size + (vector_x_size*2)));
	return 0;
}

static void destroy_data_object(data_t* data)
{
	if(data->matrix_a)
		free(data->matrix_a);
}

static int send_parameters_2_server(int private_socket_fd)
{
	int error = 0;
	pid_t pid = 0;
	long tid = 0;

	pid = getpid();
	tid = (long) pthread_self();

	if(r_write(private_socket_fd,&pid,sizeof(pid_t)) != sizeof(pid_t))
		error = 1;
	else if(r_write(private_socket_fd,&tid,sizeof(long)) != sizeof(long))
		error = 1;
	else if(r_write(private_socket_fd,&args.m,sizeof(int)) != sizeof(int))
		error = 1;
	else if(r_write(private_socket_fd,&args.p,sizeof(int)) != sizeof(int))
		error = 1;

	return (error ? (errno != EPIPE ? -1 : 1) : 0 ); /* EPIPE if server dropped off return 1, if error return -1, else 0*/
}

static int update_stats(double connection_time)
{
	if(pthread_mutex_lock(&mutex_stats))
		return 1;

	stats.x_sum += connection_time;
	stats.x_sqr_sum += pow(connection_time,2.0);
	stats.n += 1;

	if(pthread_mutex_unlock(&mutex_stats))
		return 1;

	return 0;
}

static double count_miliseconds_passed(const struct timespec* start,const struct timespec* end)
{
	return THOUSAND*(end->tv_sec - start->tv_sec) +
		(double)(end->tv_nsec - start->tv_nsec)/MILLION;
}

static int signal_other_thread(void)
{
	int error = 0;
	sigset_t mask_one;
	if(sigemptyset(&mask_one) || sigaddset(&mask_one,SIGINT) || pthread_sigmask(SIG_BLOCK,&mask_one,NULL)) /* block SIGINT for this thread*/
		error = errno;

	if(kill(getpid(),SIGINT)) /* notify other thread of SIGINT*/
	{
		if(error != 0)
			errno = error;
		else
			error = errno;
	}
		
	return (error ? 1 : 0);
}

static int getnamed(const char *name, sem_t **sem, int val) 
{
   while (((*sem = sem_open(name, FLAGS_SEM, PERMS_SEM, val)) == SEM_FAILED) &&
           (errno == EINTR)) ;
   if (*sem != SEM_FAILED)
       return 0;
   if (errno != EEXIST)
      return -1;
   while (((*sem = sem_open(name, 0)) == SEM_FAILED) && (errno == EINTR)) ;
   if (*sem != SEM_FAILED)
       return 0;
   return -1;
}

static int destroynamed(const char *name, sem_t *sem) 
{
    int error = 0;
    if (sem_close(sem) == -1)
       error = errno;
    if ((sem_unlink(name) != -1) && !error)
       return 0;
    if (error)        /* set errno to first error that occurred */
       errno = error;
    return -1;
}

static void* write_logs(void* _args)
{
	ssize_t bytes_read = 0;
	char buffer[BUF_SIZE];
	int file_logs_fd= 0;

	memset(buffer,'\0',BUF_SIZE);

	sprintf(buffer,"%s%d.txt",LOGS_FILENAME_PREFIX,getpid());

	if(unlink(buffer) == -1 && errno!=ENOENT)
	{
		err("In write_logs(): unlink()");
	}
	else if((file_logs_fd = r_open3(buffer,FLAGS_LOGS,PERMS_LOGS)) == -1) 
	{
		err("In write_logs(): r_open3()");
		return NULL;
	}

	while((bytes_read = r_read(pipe_logs_fd[0], buffer, BUF_SIZE)) != 0 && (bytes_read != -1))
	{
		if(r_write(file_logs_fd, buffer, bytes_read) != bytes_read)
		{
			err("In write_logs(): r_write()");
			continue;
		}
	}
	if(bytes_read == -1)
		err("In write_logs(): r_read()");

	close(file_logs_fd);

	return NULL;
}

static void signal_handler(int signal)
{
	sigreceived = 1;
}
/* ---------- End -------------*/