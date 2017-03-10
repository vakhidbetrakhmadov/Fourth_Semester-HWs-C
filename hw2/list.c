#include "list.h"

/* helper function,prints coordinate to the given output file*/
static int outputTo(FILE* outputp, const coord_t* coordinate,const char* fileName,const char* string);

/* helper function, reads n chars from file(ignoring white spaces) to the given buffer starting at the given offset and saves every chars coordinates in the coordinatesArr*/
/*increments row and sets col to 1 on \n,increments col on every char read*/
/* -1 on error, 0 on EOF, > 0 (char read) on success*/
/* puts '\0' at the end, bytes2Read must be <= (bufferSize-1) , size of coordinatesArr must be >= (bufferSize-1), startAt <= (bufferSize-1)*/
static int readChNotWSpace(int fd,char* buffer,int bufferSize,coord_t* coordinatesArr,int startAt,int bytes2Read,int* row,int* col);

/*type == 1 > char* , type == 2 > coord_t* */
/*makes leftsift on the given buffer*/
static void leftShift(void* buffer, int size, int type);


/*finds coordinates of all strings in given file and prints them into output file*/
/* returns -1 on error, >= 0 on success */
int findAllStringsInFile(const char* fileName, const char* string,FILE* outputp)
{
	int inputfd;
	int closeValue;
	char* buffer = NULL;
	int bufferSize = 0;
	coord_t* coordinatesArr = NULL;
	int coordinatesArrSize = 0;
	int charsRead = 0;
	int foundNumber = 0;
	int flag = 0;
	int row = 1;
	int col = 1;

	if(fileName == NULL || string == NULL || outputp == NULL)
		return -1;

	while((inputfd = open(fileName,O_RDONLY)) == -1 && errno == EINTR);
	if(inputfd == -1)
		return -1;

	bufferSize = strlen(string)+1;
	coordinatesArrSize = bufferSize-1;

	buffer = (char*) calloc(bufferSize,sizeof(char));
	if(buffer == NULL)
	{
		while ((closeValue = close(inputfd)) == -1 && errno == EINTR);
		return -1;
	}
	coordinatesArr = (coord_t*) calloc(coordinatesArrSize,sizeof(coord_t));
	if(coordinatesArr == NULL)
	{
		free(buffer);
		while ((closeValue = close(inputfd)) == -1 && errno == EINTR);
		return -1;
	}

	charsRead = readChNotWSpace(inputfd,buffer,bufferSize,coordinatesArr,1,bufferSize-1,&row,&col);
	if(charsRead == -1)
	{
		foundNumber = -1;
	}
	else if(charsRead == bufferSize-1)
	{
		while(!flag)
		{
			if(strcmp(string,buffer) == 0)
			{	
				++foundNumber;
				if(outputTo(outputp,coordinatesArr,fileName,string) == -1)
				{	
					flag = 1;
					foundNumber = -1;
				}
			}
			leftShift(buffer,bufferSize-1,1);
			leftShift(coordinatesArr,coordinatesArrSize,2);
			charsRead = readChNotWSpace(inputfd,buffer,bufferSize,coordinatesArr,bufferSize-1,1,&row,&col);

			if(charsRead == -1)
			{
				foundNumber = -1;
				flag = 1;
			}
			else if(!charsRead)/*EOF*/
			{
				flag = 1;
			}
			else
			{
				/*to keep compiler happy*/
			}
		}
	}

	if(buffer != NULL)
		free(buffer);
	if(coordinatesArr != NULL)
		free(coordinatesArr);

	while ((closeValue = close(inputfd)) == -1 && errno == EINTR);
	if(closeValue == -1)
		return -1;

	return foundNumber;
}

/*type == 1 > char* , type == 2 > coord_t* */
void leftShift(void* buffer, int size, int type)
{
	int i;

	for (i = 0; i < size -1 ; ++i)
	{
		if(type == 1)
			((char*)buffer)[i] = ((char*)buffer)[i+1];
		else
			((coord_t*)buffer)[i] = ((coord_t*)buffer)[i+1]; 
	}
}

/* -1 on error, 0 on EOF, > 0 (char read) on success*/
/* puts '\0' at the end, bytes2Read must be <= (bufferSize-1) , size of coordinatesArr must be >= (bufferSize-1), startAt <= (bufferSize-1)*/
int readChNotWSpace(int fd,char* buffer,int bufferSize,coord_t* coordinatesArr,int startAt,int bytes2Read,int* row,int* col)
{
	int offSet; /*check if needed*/
	char* bp = NULL;
	coord_t* cp = NULL;
	int bytesRead;
	ssize_t readValue;
	int flag = 0;
	offSet = startAt-1;
	bp = buffer + offSet;
	cp = coordinatesArr + offSet;

	if(buffer == NULL || coordinatesArr == NULL || row == NULL || col == NULL || bufferSize <= 0 || startAt < 1 || bytes2Read < 0 
		|| bytes2Read >= bufferSize || startAt >= bufferSize || bytes2Read > (bufferSize-startAt))
	{
		return -1;
	}	

	for(bytesRead = 0;bytesRead < bufferSize-1 && bytesRead < bytes2Read && !flag;/*nothing*/)
	{
		readValue = read(fd,bp+bytesRead,1);
		if(readValue == -1 && errno == EINTR)
		{
			/*INTERRUPT SIGNAL continue...*/
		}
		else if(readValue == -1)
		{
			flag = 1;
			bytesRead = -1;
		}
		else if(!readValue)
		{
			flag = 1;
		}
		else
		{
			if(isspace(bp[bytesRead]) && bp[bytesRead] == '\n')
			{
				*col = 1;
				++(*row);
			}
			else if(isspace(bp[bytesRead]))
			{
				++(*col);
			}
			else
			{
				cp[bytesRead].row = *row;
				cp[bytesRead].col = *col;
				++bytesRead;
				++(*col);
			}
		}
	}

	if(bytesRead != -1)
		bp[bytesRead] = '\0';

	return bytesRead;
}

int outputTo(FILE* outputp, const coord_t* coordinate,const char* fileName,const char* string)
{
	if(coordinate == NULL || outputp == NULL || fileName == NULL || string == NULL)
		return -1;

	fprintf(outputp, "%s: [%d, %d] %s first character is found. \n",fileName,coordinate->row,coordinate->col,string);

	if(fflush(outputp))
		return -1;

	return 0;
}