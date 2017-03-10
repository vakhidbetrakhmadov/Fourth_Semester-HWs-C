#include <stdio.h>
#include "list.h"

int main(int argc, char const *argv[])
{
	int stringsFound = 0;

	if(argc!=3)
	{
		fprintf(stderr,"Usage: %s 'string' <file>\n",argv[0]);
		return 1;
	}

	fprintf(stdout, "\n");
	
	stringsFound = findAllStringsInFile(argv[2],argv[1],stdout);
	if(stringsFound == -1)
	{
		fprintf(stderr, "%s\n","Error" );
		return 1;
	}

	fprintf(stdout, "\n%d adet %s bulundu.\n",stringsFound,argv[1]); 

	return 0;
}