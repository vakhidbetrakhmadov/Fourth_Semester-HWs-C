#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include "clients.h"

#define DEFAULT_PORT 9898

static int is_valid_number(const char* str);

int main(int argc, const char* argv[])
{
	int clients_num = 0;
	sigset_t mask_all;

	if(argc > 6 || argc < 5 || !is_valid_number(argv[1]) ||     /* ---- Usage check ---- */
	  !is_valid_number(argv[2]) || !is_valid_number(argv[3]) ||
	  (argc == 6 && !is_valid_number(argv[5]))) 
	{
		fprintf(stderr,"Usage: %s <rows> <columns> <clients> <hostname> or %s <rows> <columns> <clients> <hostname> <port>\n",argv[0],argv[0]);
		return 1;
	}

	if( sigfillset(&mask_all) || sigprocmask(SIG_BLOCK,&mask_all,NULL) || setup_signal_handler(SIGINT)) /*block all signals, setup signal handler for SIGINT*/
	{
		err("sigfillset() || sigprocmask() || setup_signal_handler()");
		return 1;
	}

	clients_num = atoi(argv[3]);
	if(argc == 6 && setup_args(atoi(argv[1]),atoi(argv[2]),atoi(argv[5]),argv[4]))
	{
		err("setup_args()");
		return 1;
	}
	else if(argc == 5 && setup_args(atoi(argv[1]),atoi(argv[2]),DEFAULT_PORT,argv[4]))
	{
		err("setup_args()");
		return 1;
	}

	if(start_logger())
	{
		err("start_logger()");
		return 1;
	}

	if(start_clients(clients_num))
	{
		err("start_clients()");
		stop_logger();
		return 1;
	}

	if(join_clients())
	{
		err("start_clients()");
		stop_logger();
		return 1;
	}


	if(stop_logger())
	{
		err("stop_logger()");
		return 1;
	}

	return 0;
}

static int is_valid_number(const char* str)
{
	int result = 1;
	int i = 0;
	for(i = 0; str[i] != '\0' && result; ++i)
		if(!isdigit(str[i]))
			result = 0;

	return result;
}