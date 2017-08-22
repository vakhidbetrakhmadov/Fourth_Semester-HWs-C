#include "serve_request.h"

#define PERM_SHARED_MEM (S_IRUSR | S_IWUSR)
#define BUF_SIZE 1024

typedef struct shmem_gs_s
{
	pthread_mutex_t mutex;
	pthread_cond_t cond_var;
	int ready;
	double* matrix_a;
	double* vector_b;
}shmem_gs_t;                 /* Generator and Solver common shared memory block type */

typedef struct shmem_sv_s
{
	pthread_mutex_t mutex;
	pthread_cond_t cond_var;
	int x1_ready;
	int x2_ready;
	int x3_ready;
	double e1;
	double e2;
	double e3;
	double* x1;
	double* x2;
	double* x3;
}shmem_sv_t;                 /* Solver and Verifier common shared memory block type */

typedef struct sh_blocks_s
{
	int m; /* rows */
	int p; /* cols */
	shmem_gs_t* shmem_gs_ptr;
	shmem_sv_t* shmem_sv_ptr;
}shmem_ptrs_t;

/* ----------- Gloval variables declarations ---------------------*/
static int currently_served = 0;
static pthread_mutex_t mutex_currently_served = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_currently_served = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t mutex_args = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_args = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t mutex_pipe_logs = PTHREAD_MUTEX_INITIALIZER;
/* -------------------- END  ------------------------------------*/

static int copy_received_args(void *args, int* private_sock_fd);
static int receive_parameters_from_client(int private_sock_fd, pid_t* client_pid, long* client_tid, int* m,int* p);
static int increment_currently_served(void);
static int decrement_currently_served(void);
static int detachandremove(int shmid, void *shmaddr);
static int init_shared_sync_objects(shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr);
static int create_shared_mem_blocks(int m, int p,int* shared_mem_id, shmem_gs_t** shmem_gs_ptr, shmem_sv_t** shmem_sv_ptr);
static int send_logs(pid_t client_pid, long client_tid, int m, int p, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr);
static int send_vectors_x_2_logs(int p, shmem_sv_t* shmem_sv_ptr);
static int send_vector_b_2_logs(int m, double* vector);
static int send_matrix_a_2_logs(int m, int p, double * matrix);
static int send_data_2_client(int m, int p,int private_sock_fd, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr);

static int generate_a_and_b(int m, int p, shmem_gs_t* shmem_gs_ptr);
static int solve_a_and_b_for_x(int m, int p, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr);
static int verify_solutions_x(int m, int p, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr);
static void* solve_with_pim(void* _args);
static void* solve_with_qr(void* _args);
static void* solve_with_svd(void* _args);

static int init_shared_cond(pthread_cond_t* cond);
static int init_shared_mutex(pthread_mutex_t* mutex);

void* serve_request(void* args)
{
	int i = 0, wait_value = 0;
	pid_t pid = 0;
	int private_sock_fd = 0;
	int m = 0, p = 0; /* m - number of rows ,  p - number of columns */
	pid_t client_pid = 0;
	long client_tid;
	int receive_status = 0;
	int shared_mem_id = 0;
	shmem_gs_t* shmem_gs_ptr = NULL; 
	shmem_sv_t* shmem_sv_ptr = NULL;

	if(copy_received_args(args,&private_sock_fd))
	{
		err("copy_received_args()");
		r_close(private_sock_fd);
		return NULL;
	}
	if(increment_currently_served())
	{
		err("increment_currently_served()");
		r_close(private_sock_fd);
		return NULL;
	}

	if((receive_status = receive_parameters_from_client(private_sock_fd,&client_pid,&client_tid,&m,&p)) == -1)
	{
		err("receive_parameters_from_client()");
	}
	else if(receive_status == 1)
	{		
		if(create_shared_mem_blocks(m,p,&shared_mem_id,&shmem_gs_ptr,&shmem_sv_ptr))
		{
			err("create_shared_mem_blocks()");
		}
		else
		{	
			for(i = 0; pid != -1 && i < 3; ++i) /* create 3 child processes*/
			{
				if((pid = fork()) == -1)
					err("fork()");
				else if( pid == 0)
				{
					/*close all inherited file descripters*/
					r_close(pipe_logs_fd[0]);
					r_close(pipe_logs_fd[1]);
					r_close(passive_sock_fd);
					r_close(private_sock_fd);
					r_close(file_logs_fd);	
					
					switch (i)
					{
						case 0: 
							if(generate_a_and_b(m,p,shmem_gs_ptr))
								err("generate_a_and_b()");
						 	break;
						case 1: 
							if(solve_a_and_b_for_x(m,p,shmem_gs_ptr,shmem_sv_ptr))
									err("solve_a_and_b_for_x()");
							break;
						case 2:
							if(verify_solutions_x(m,p,shmem_gs_ptr,shmem_sv_ptr))
								err("verify_solutions_x()");
						 	break;
						default: break;
					}

					exit(0);
				}
			}

			do /* wait all child processes */
			{
				if(sigreceived)
				{
					r_close(private_sock_fd);
					if(signal_other_thread())
						err("In serve_request signal_other_thread()");
				}

			}while((wait_value = wait(NULL)) > 0 || (wait_value == -1 && errno == EINTR));

			if(pid != -1 && !sigreceived)
			{
				if(send_data_2_client(m,p,private_sock_fd,shmem_gs_ptr,shmem_sv_ptr))
					err("send_data_2_client()");
				else if(send_logs(client_pid,client_tid,m,p,shmem_gs_ptr,shmem_sv_ptr))
					err("send_logs()");
			}

			pthread_mutex_destroy(&(shmem_gs_ptr->mutex));
			pthread_mutex_destroy(&(shmem_sv_ptr->mutex));
			pthread_cond_destroy(&(shmem_gs_ptr->cond_var));
			pthread_cond_destroy(&(shmem_sv_ptr->cond_var));
			detachandremove(shared_mem_id,shmem_gs_ptr);
		}

	}/*else receive_status == 0, connection was droped*/

	if(decrement_currently_served())
	{
		err("decrement_currently_served()");
	}

	r_close(private_sock_fd);

	return NULL;
}

static int generate_a_and_b(int m, int p, shmem_gs_t* shmem_gs_ptr)
{
	int error = 0;

	if(generate_random_matrix(shmem_gs_ptr->matrix_a,m,p) || generate_random_matrix(shmem_gs_ptr->vector_b,m,1))
		return 1;

	if(pthread_mutex_lock(&(shmem_gs_ptr->mutex)))
		return 1;

	shmem_gs_ptr->ready = 1;

	if(pthread_cond_signal(&(shmem_gs_ptr->cond_var)))
	{
		error = errno;
		pthread_mutex_unlock(&(shmem_gs_ptr->mutex));
		errno = error;
		return 1;
	}

	if(pthread_mutex_unlock(&(shmem_gs_ptr->mutex)))
		return 1;

	return 0;
}

static int solve_a_and_b_for_x(int m, int p, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr)
{
	int error = 0, i = 0;
	pthread_t tid[3];
	shmem_ptrs_t args;

	if(pthread_mutex_lock(&(shmem_gs_ptr->mutex)))
		return 1;
	while(shmem_gs_ptr->ready != 1)
	{
		if(pthread_cond_wait(&(shmem_gs_ptr->cond_var),&(shmem_gs_ptr->mutex)))
		{
			error = errno;
			pthread_mutex_unlock(&(shmem_gs_ptr->mutex));
			errno = error;
			return 1;
		}
	}
	if(pthread_mutex_unlock(&(shmem_gs_ptr->mutex)))
		return 1;

	args.m = m;
	args.p = p;
	args.shmem_gs_ptr = shmem_gs_ptr;
	args.shmem_sv_ptr = shmem_sv_ptr;

	if(pthread_create(&tid[0],NULL,solve_with_svd,&args) ||
		pthread_create(&tid[1],NULL,solve_with_qr,&args) ||
		pthread_create(&tid[2],NULL,solve_with_pim,&args))
			error = 1;

	for(i = 0; i < 3; ++i)
		if(pthread_join(tid[i],NULL))
			error = 1;

	return error;
}

static int verify_solutions_x(int m, int p, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr) 
{
	int error = 0, i = 0, x_ready = 0;

	for(i = 0; i < 3; ++i)
	{
		if(pthread_mutex_lock(&(shmem_sv_ptr->mutex)))
			return 1;

		while(shmem_sv_ptr->x1_ready != 1 && shmem_sv_ptr->x2_ready != 1 && shmem_sv_ptr->x3_ready != 1 )
		{
			if(pthread_cond_wait(&(shmem_sv_ptr->cond_var),&(shmem_sv_ptr->mutex)))
			{
				error = errno;
				pthread_mutex_unlock(&(shmem_sv_ptr->mutex));
				errno = error;
				return 1;
			}
		}

		if(shmem_sv_ptr->x1_ready)
		{
			x_ready = 1;
			shmem_sv_ptr->x1_ready = 0;
		}
		else if(shmem_sv_ptr->x2_ready)
		{
			x_ready = 2;
			shmem_sv_ptr->x2_ready = 0;
		}
		else
		{
			x_ready = 3;
			shmem_sv_ptr->x3_ready = 0;
		}

		if(pthread_mutex_unlock(&(shmem_sv_ptr->mutex)))
			return 1;

		switch (x_ready)
		{
			case 1: shmem_sv_ptr->e1 = verify_x(shmem_gs_ptr->matrix_a,shmem_gs_ptr->vector_b,shmem_sv_ptr->x1,m,p); break;
			case 2: shmem_sv_ptr->e2 = verify_x(shmem_gs_ptr->matrix_a,shmem_gs_ptr->vector_b,shmem_sv_ptr->x2,m,p); break;
			case 3: shmem_sv_ptr->e3 = verify_x(shmem_gs_ptr->matrix_a,shmem_gs_ptr->vector_b,shmem_sv_ptr->x3,m,p); break;
			default: break;
		}
	}

	return 0;
}

static void* solve_with_svd(void* _args)
{
	shmem_ptrs_t* args = NULL;
	int m = 0;
	int p = 0;
	shmem_gs_t* shmem_gs_ptr = NULL;
	shmem_sv_t* shmem_sv_ptr = NULL;

	args = (shmem_ptrs_t*)_args;
	m = args->m;
	p = args->p;
	shmem_gs_ptr = args->shmem_gs_ptr;
	shmem_sv_ptr = args->shmem_sv_ptr;

	solve_for_x_using_svd(shmem_gs_ptr->matrix_a,shmem_gs_ptr->vector_b,m,p,shmem_sv_ptr->x1);

	if(pthread_mutex_lock(&(shmem_sv_ptr->mutex)))
	{
		err("In solve_with_svd: pthread_mutex_lock()");
		return NULL;
	}

	shmem_sv_ptr->x1_ready = 1;

	if(pthread_cond_signal(&(shmem_sv_ptr->cond_var)))
	{
		err("In solve_with_svd: pthread_cond_signal()");
		pthread_mutex_unlock(&(shmem_sv_ptr->mutex));
		return NULL;
	}

	if(pthread_mutex_unlock(&(shmem_sv_ptr->mutex)))
	{
		err("In solve_with_svd: pthread_mutex_unlock()");
		return NULL;
	}

	return NULL;
}

static void* solve_with_qr(void* _args)
{
	shmem_ptrs_t* args = NULL;
	int m = 0;
	int p = 0;
	shmem_gs_t* shmem_gs_ptr = NULL;
	shmem_sv_t* shmem_sv_ptr = NULL;

	args = (shmem_ptrs_t*)_args;
	m = args->m;
	p = args->p;
	shmem_gs_ptr = args->shmem_gs_ptr;
	shmem_sv_ptr = args->shmem_sv_ptr;

	solve_for_x_using_qr(shmem_gs_ptr->matrix_a,shmem_gs_ptr->vector_b,m,p,shmem_sv_ptr->x2);

	if(pthread_mutex_lock(&(shmem_sv_ptr->mutex)))
	{
		err("In solve_with_svd: pthread_mutex_lock()");
		return NULL;
	}

	shmem_sv_ptr->x2_ready = 1;

	if(pthread_cond_signal(&(shmem_sv_ptr->cond_var)))
	{
		err("In solve_with_svd: pthread_cond_signal()");
		pthread_mutex_unlock(&(shmem_sv_ptr->mutex));
		return NULL;
	}

	if(pthread_mutex_unlock(&(shmem_sv_ptr->mutex)))
	{
		err("In solve_with_svd: pthread_mutex_unlock()");
		return NULL;
	}

	return NULL;
}

static void* solve_with_pim(void* _args)
{
	shmem_ptrs_t* args = NULL;
	int m = 0;
	int p = 0;
	shmem_gs_t* shmem_gs_ptr = NULL;
	shmem_sv_t* shmem_sv_ptr = NULL;

	args = (shmem_ptrs_t*)_args;
	m = args->m;
	p = args->p;
	shmem_gs_ptr = args->shmem_gs_ptr;
	shmem_sv_ptr = args->shmem_sv_ptr;

	solve_for_x_using_mpi(shmem_gs_ptr->matrix_a,shmem_gs_ptr->vector_b,m,p,shmem_sv_ptr->x3);

	if(pthread_mutex_lock(&(shmem_sv_ptr->mutex)))
	{
		err("In solve_with_svd: pthread_mutex_lock()");
		return NULL;
	}

	shmem_sv_ptr->x3_ready = 1;

	if(pthread_cond_signal(&(shmem_sv_ptr->cond_var)))
	{
		err("In solve_with_svd: pthread_cond_signal()");
		pthread_mutex_unlock(&(shmem_sv_ptr->mutex));
		return NULL;
	}

	if(pthread_mutex_unlock(&(shmem_sv_ptr->mutex)))
	{
		err("In solve_with_svd: pthread_mutex_unlock()");
		return NULL;
	}

	return NULL;
}

static int send_logs(pid_t client_pid, long client_tid, int m, int p, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr)
{
	char buffer[BUF_SIZE];
	int bytes = 0;
	int error = 0;

	if(pthread_mutex_lock(&mutex_pipe_logs))
		return 1;

	bytes = sprintf(buffer,"Client's pid: %d; tid: %ld\n\n",(int)client_pid,client_tid);

	if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = errno;
	else if(send_matrix_a_2_logs(m,p,shmem_gs_ptr->matrix_a))
		error = errno;
	else if(send_vector_b_2_logs(m,shmem_gs_ptr->vector_b))
		error = errno;
	else if(send_vectors_x_2_logs(p,shmem_sv_ptr))
		error = errno;

	if(pthread_mutex_unlock(&mutex_pipe_logs))
	{
		if(error)
			errno = error;
		return 1;
	}

	return (error ? 1 : 0);
}

static int send_vectors_x_2_logs(int p, shmem_sv_t* shmem_sv_ptr)
{
	int error = 0, i = 0;
	char buffer[BUF_SIZE];
	int bytes = 0;
	char new_line = '\n';

	bytes = sprintf(buffer,"Vector x1:\n"); 
	if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;

	for(i = 0;!error && i < p; ++i)
	{
		bytes = sprintf(buffer,"%f\n",(shmem_sv_ptr->x1)[i]); 
		if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
			error = 1;
	}

	if(!error)
		if(r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;

	bytes = sprintf(buffer,"Vector x2:\n"); 
	if(!error && r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;

	for(i = 0;!error && i < p; ++i)
	{
		bytes = sprintf(buffer,"%f\n",(shmem_sv_ptr->x2)[i]); 
		if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
			error = 1;
	}

	if(!error)
		if(r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;

	bytes = sprintf(buffer,"Vector x3:\n"); 
	if(!error && r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
		error = 1;

	for(i = 0;!error && i < p; ++i)
	{
		bytes = sprintf(buffer,"%f\n",(shmem_sv_ptr->x3)[i]); 
		if(r_write(pipe_logs_fd[1],buffer,bytes) != bytes)
			error = 1;
	}

	if(!error)
		if(r_write(pipe_logs_fd[1],&new_line,sizeof(char)) != sizeof(char))
			error = 1;
	

	return error;
}

static int send_vector_b_2_logs(int m, double* vector)
{
	int error = 0, i = 0;
	char buffer[BUF_SIZE];
	int bytes = 0;
	char new_line = '\n';

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

static int send_data_2_client(int m, int p,int private_sock_fd, shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr)
{
	int error = 0;
	const int matrix_a_size = sizeof(double)*m*p;
	const int vector_b_size = sizeof(double)*m;
	const int vector_x_size = sizeof(double)*p;

	if(r_write(private_sock_fd,shmem_gs_ptr->matrix_a,matrix_a_size) != matrix_a_size) /*send matrix a to the client*/
		error = 1;
	else if(r_write(private_sock_fd,shmem_gs_ptr->vector_b,vector_b_size) != vector_b_size) /*send vector b to the client*/
		error = 1;
	else if(r_write(private_sock_fd,shmem_sv_ptr->x1,vector_x_size) != vector_x_size) /*send vector x1 to the client*/
		error = 1;
	else if(r_write(private_sock_fd,shmem_sv_ptr->x2,vector_x_size) != vector_x_size) /*send vector x2 to the client*/
		error = 1;
	else if(r_write(private_sock_fd,shmem_sv_ptr->x3,vector_x_size) != vector_x_size) /*send vector x3 to the client*/
		error = 1;
	else if(r_write(private_sock_fd,&(shmem_sv_ptr->e1),sizeof(double)) != sizeof(double)) /*send norm e1 to the client*/
		error = 1;
	else if(r_write(private_sock_fd,&(shmem_sv_ptr->e2),sizeof(double)) != sizeof(double)) /*send norm e2 to the client*/
		error = 1;
	else if(r_write(private_sock_fd,&(shmem_sv_ptr->e3),sizeof(double)) != sizeof(double)) /*send norm e3 to the client*/
		error = 1;

	return (error ? (errno != EPIPE ? 1 : 0) : 0 ); /* EPIPE if client dropped off*/
}

static int create_shared_mem_blocks(int m, int p,int* shared_mem_id, shmem_gs_t** shmem_gs_ptr, shmem_sv_t** shmem_sv_ptr)
{
	int error = 0;
	char* shared_mem_ptr = NULL;

	const int SOLUTIONS = 3;
	const int shmem_gs_size = sizeof(shmem_gs_t);
	const int shmem_sv_size = sizeof(shmem_sv_t);
	const int matrix_a_size = sizeof(double)*m*p;
	const int vector_b_size = sizeof(double)*m;
	const int vector_x_size = sizeof(double)*p;

	if((*shared_mem_id = shmget(
		 IPC_PRIVATE
		,shmem_gs_size + shmem_sv_size + matrix_a_size + vector_b_size + (vector_x_size*SOLUTIONS)
		,PERM_SHARED_MEM))
		 == -1)
	{
		error = errno;
	}
	else if((shared_mem_ptr = (char*) shmat(*shared_mem_id,NULL,0)) == (void*)-1)
	{
		error = errno;
		shmctl(*shared_mem_id, IPC_RMID, NULL);
	}
	else
	{
		*shmem_gs_ptr = (shmem_gs_t*) shared_mem_ptr;
		*shmem_sv_ptr = (shmem_sv_t*) (shared_mem_ptr + shmem_gs_size);
		(*shmem_gs_ptr)->matrix_a = (double*) (shared_mem_ptr + (shmem_gs_size + shmem_sv_size));
		(*shmem_gs_ptr)->vector_b = (double*) (shared_mem_ptr + (shmem_gs_size + shmem_sv_size + matrix_a_size));
		(*shmem_sv_ptr)->x1 = (double*) (shared_mem_ptr + (shmem_gs_size + shmem_sv_size + matrix_a_size + vector_b_size));
		(*shmem_sv_ptr)->x2 = (double*) (shared_mem_ptr + (shmem_gs_size + shmem_sv_size + matrix_a_size + vector_b_size + vector_x_size));
		(*shmem_sv_ptr)->x3 = (double*) (shared_mem_ptr + (shmem_gs_size + shmem_sv_size + matrix_a_size + vector_b_size + vector_x_size*2));

		if(init_shared_sync_objects(*shmem_gs_ptr,*shmem_sv_ptr))
		{
			error = errno;
			detachandremove(*shared_mem_id,shared_mem_ptr);
		}
	}

	if(error)
		errno = error;
	
	return (error ? 1 : 0);
}

static int init_shared_mutex(pthread_mutex_t* mutex)
{
	int error = 0;
	pthread_mutexattr_t mutex_attr;

	if(pthread_mutexattr_init(&mutex_attr))
		return 1;
	else if(pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED))
		error = errno;
	else if(pthread_mutex_init(mutex, &mutex_attr))
		error = errno;

	pthread_mutexattr_destroy(&mutex_attr);

	if(error)
		errno = error;

	return (error ? 1 : 0);
}

static int init_shared_cond(pthread_cond_t* cond)
{
	int error = 0;
	pthread_condattr_t cond_attr;

	if(pthread_condattr_init(&cond_attr))
		return 1;
	else if(pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED))
		error = errno;
	else if(pthread_cond_init(cond, &cond_attr))
		error = errno;

	pthread_condattr_destroy(&cond_attr);

	if(error)
		errno = error;

	return (error ? 1 : 0);
}

static int init_shared_sync_objects(shmem_gs_t* shmem_gs_ptr, shmem_sv_t* shmem_sv_ptr)
{
	int error = 0;
	if(init_shared_mutex(&(shmem_gs_ptr->mutex)))
	{
		error = errno;
	}
	else if(init_shared_mutex(&(shmem_sv_ptr->mutex)))
	{
		error = errno;
		pthread_mutex_destroy(&(shmem_gs_ptr->mutex));
	}
	else if(init_shared_cond(&(shmem_gs_ptr->cond_var)))
	{
		error = errno;
		pthread_mutex_destroy(&(shmem_gs_ptr->mutex));
		pthread_mutex_destroy(&(shmem_sv_ptr->mutex));
	}
	else if(init_shared_cond(&(shmem_sv_ptr->cond_var)))
	{
		error = errno;
		pthread_mutex_destroy(&(shmem_gs_ptr->mutex));
		pthread_mutex_destroy(&(shmem_sv_ptr->mutex));
		pthread_cond_destroy(&(shmem_gs_ptr->cond_var));
	}
	
	if(error)
		errno = error;

	return (error ? 1 : 0);
}

static int detachandremove(int shmid, void *shmaddr) 
{
   int error = 0; 

   if (shmdt(shmaddr) == -1)
      error = errno;
   if ((shmctl(shmid, IPC_RMID, NULL) == -1) && !error)
      error = errno;
   if (!error)
      return 0;
   errno = error;
   return -1;
}

static int receive_parameters_from_client(int private_sock_fd, pid_t* client_pid, long* client_tid, int* m,int* p)
{
	int error = 0;
	int bytes_read = 0;
	if((bytes_read = r_read(private_sock_fd,client_pid,sizeof(pid_t))) != sizeof(pid_t))
		error = 1;
	else if((bytes_read = r_read(private_sock_fd,client_tid,sizeof(long))) != sizeof(long))
		error = 1;
	else if((bytes_read = r_read(private_sock_fd,m,sizeof(int))) != sizeof(int))
		error = 1;
	else if((bytes_read = r_read(private_sock_fd,p,sizeof(int))) != sizeof(int))
		error = 1;

	return (error ? (bytes_read == 0 ? 0 : -1) : 1); /*if 0 bytes read return 0, if error return -1, else return 1*/
}

static int copy_received_args(void *args, int* private_sock_fd)
{
	int error = 0;
	if(pthread_mutex_lock(&mutex_args))
		return 1;

	*private_sock_fd = *(int*)args;
	*(int*)args = 0;

	if(pthread_cond_signal(&cond_args))
	{
		error = errno;
		pthread_mutex_unlock(&mutex_args);
		errno = error;
		return 1;
	}

	if(pthread_mutex_unlock(&mutex_args))
		return 1;

	return 0;
}

static int increment_currently_served(void)
{
	int error = 0;
	if(pthread_mutex_lock(&mutex_currently_served))
		return 1;
	++currently_served;
	if(pthread_cond_broadcast(&cond_currently_served))
	{
		error = errno;
		pthread_mutex_unlock(&mutex_currently_served);
		errno = error;
		return 1;
	}
	else if(pthread_mutex_unlock(&mutex_currently_served))
		return 1;

	return 0;
}

static int decrement_currently_served(void)
{
	int error = 0;
	if(pthread_mutex_lock(&mutex_currently_served))
		return 1;
	--currently_served;
	if(pthread_cond_broadcast(&cond_currently_served))
	{
		error = errno;
		pthread_mutex_unlock(&mutex_currently_served);
		errno = error;
		return 1;
	}
	else if(pthread_mutex_unlock(&mutex_currently_served))
		return 1;

	return 0;
}

int wait_args_delivery(int* private_sock_fd)
{
	int error = 0;
	if(pthread_mutex_lock(&mutex_args))
		return 1;
	while(*private_sock_fd != 0)
	{
		if(pthread_cond_wait(&cond_args,&mutex_args))
		{
			error = errno;
			pthread_mutex_unlock(&mutex_args);
			errno = error;
			return 1;
		}
	}
	if(pthread_mutex_unlock(&mutex_args))
		return 1;

	return 0;
}

void* write_to_screen(void* args)
{
	sigset_t mask_one;
	int error = 0;
	int previous_value = 0;
	if(sigemptyset(&mask_one) || sigaddset(&mask_one,SIGINT) || pthread_sigmask(SIG_BLOCK,&mask_one,NULL)) /* block SIGINT for this thread*/
	{
		err("In write_to_screen(): error while setting sigmask");
		return NULL;
	}

	if(pthread_mutex_lock(&mutex_currently_served))
	{
		err("In write_to_screen(): pthread_mutex_lock(");
		return NULL;
	}

	for( ;!error; )
	{
		if(pthread_cond_wait(&cond_currently_served,&mutex_currently_served)) /*cancelation point*/
		{
			err("In write_to_screen(): pthread_cond_wait()");
			error = 1;
		}
		else if(previous_value != currently_served)
		{
			previous_value = currently_served;
			fprintf(stdout,"Number of clients currently being served is: %d\n",currently_served);
		}
	}

	if(pthread_mutex_unlock(&mutex_currently_served))
		err("In write_to_screen(): pthread_mutex_unlock()");

	return NULL;
}

int wait_requests_being_served(void)
{
	int error = 0;
	if(pthread_mutex_lock(&mutex_currently_served))
		return 1;
	while(currently_served != 0)
	{
		if(pthread_cond_wait(&cond_currently_served,&mutex_currently_served))
		{
			error = errno;
			pthread_mutex_unlock(&mutex_currently_served);
			errno = error;
			return 1;
		}
	}
	if(pthread_mutex_unlock(&mutex_currently_served))
		return 1;

	return 0;
}
 
int signal_other_thread(void)
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