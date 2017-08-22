#include "matrix.h"

static int invertMatrix(double* matrix,int size);
static int convolve2DMatrix(double *matrix,int size);
static double LUPDeterminant(double **A, int *P, int N);
static int LUPDecompose(double **A, int N, double Tol, int *P);
static void LUPInvert(double **A, int *P, int N, double **IA);
static int convolve2D(double* in, double* out, int dataSizeX, int dataSizeY, 
                double* kernel, int kernelSizeX, int kernelSizeY);
static void convertFrom1Dto2D(const double * from,double** to,int size);
static void convertFrom2Dto1D(double ** from,double* to,int size);
static void fillInPartOfMatrix1D(int rstart,int rend,int cstart,int cend,const double* from,int fromSize,double* to,int toSize);
static void getPartOfMatrix1D(int rstart,int rend,int cstart,int cend,const double* from,int fromSize,double* to,int toSize);
static void getMatrixsFourth(const double* matrix,int size,double * matrixFourth,int which);
static void fillInMatrixsFourth(double* matrix,int size,const double * matrixFourth,int which);

int generateInvertableMatrix(double * buffer,int size)
{
	int i,j,matricesGenerated = 0;
	int error = 0;
	struct timespec currentTime;
	double *subMatrixNxN = NULL;
	double subMatrixDeterminant = 0;
	int N = size/2;
	double determinant = 0;

	if(!(subMatrixNxN = calloc(N*N,sizeof(double))))
		error =1;
	
	while(!error && !determinant)
	{
		matricesGenerated = 0;
		while(!error && matricesGenerated < 4)
		{
			for(i = 0;!error && i < N; ++i)
			{
				for(j = 0;!error && j < N; ++j)
				{
					if(clock_gettime(CLOCK_REALTIME,&currentTime))
					{
						error = 1;
					}
					else
					{
						subMatrixNxN[get1dIndex(i,j,N)] = currentTime.tv_nsec % 10 + 1;
					}
				}
			}

			if(!error && findDeterminant(subMatrixNxN,N,&subMatrixDeterminant))
			{
				error = 1;
			}
			else if(!error && subMatrixDeterminant)
			{
				fillInMatrixsFourth(buffer,size,subMatrixNxN,++matricesGenerated);
			}
		}

		if(!error && findDeterminant(buffer,size,&determinant))
			error = 1;
	}
	

	if(subMatrixNxN)
		free(subMatrixNxN);

	return error;
}

int findDeterminant(const double* matrix,int size,double *determinant)
{
	int i,*P;
	double** matrix2D = NULL;
	double * matrixRows2D = NULL;
	int error = 0;
	int degenerate = 0;

	if(!(matrix2D = calloc(size,sizeof(double*))))
		error = 1;
	if(!error && !(matrixRows2D = calloc(size*size,sizeof(double))))
		error = 1;
	if(!error && !(P = calloc(size+1,sizeof(int))))
		error =1;
	for (i = 0;!error && i < size; ++i)
		matrix2D[i] = &matrixRows2D[size*i];
	
	if(!error)
		convertFrom1Dto2D(matrix,matrix2D,size);
	if(!error && !LUPDecompose(matrix2D,size,0.00001,P)) /*failure, matrix is degenerate*/
	{
		degenerate = 1;
	}

	if(!error && !degenerate)
		*determinant = LUPDeterminant(matrix2D,P,size);
	else if(!error && degenerate)
		*determinant = 0;

	if(matrixRows2D)
		free(matrixRows2D);
	if(matrix2D)
		free(matrix2D);
	if(P)
		free(P);

	return error;
}

static int invertMatrix(double* matrix,int size)
{
	int i,*P;
	double** matrix2D = NULL;
	double* matrixRows2D = NULL;
	double** inverse2D = NULL;
	double* inverseRows2D = NULL;
	int error = 0;

	if(!(matrix2D = calloc(size,sizeof(double*))) || !(inverse2D = calloc(size,sizeof(double*))))
		error = 1;
	if(!error && (!(matrixRows2D = calloc(size*size,sizeof(double))) || !(inverseRows2D = calloc(size*size,sizeof(double)))))
		error = 1;
	if(!error && !(P = calloc(size+1,sizeof(int))))
		error =1;
	for (i = 0;!error && i < size; ++i)
	{
		matrix2D[i] = &matrixRows2D[size*i];
		inverse2D[i] = &inverseRows2D[size*i];
	}

	if(!error)
		convertFrom1Dto2D(matrix,matrix2D,size);
	if(!error && !LUPDecompose(matrix2D,size,0.00001,P)) /*failure, matrix is degenerate*/
		error = 1; 

	if(!error)
	{
		LUPInvert(matrix2D,P,size,inverse2D);
		convertFrom2Dto1D(inverse2D,matrix,size);
	}
	

	if(matrixRows2D)
		free(matrixRows2D);
	if(matrix2D)
		free(matrix2D);
	if(inverse2D)
		free(inverse2D);
	if(inverseRows2D)
		free(inverseRows2D);
	if(P)
		free(P);

	return error;
}

static int convolve2DMatrix(double *matrix,int size)
{
	int i,error = 0;
	double* convolvedMatrix = NULL;
	double kernel[KERNEL_SIZE*KERNEL_SIZE]; /*identity kernel*/
	memset(kernel,0,KERNEL_SIZE*KERNEL_SIZE*sizeof(double));
	kernel[get1dIndex(KERNEL_SIZE/2,KERNEL_SIZE/2,KERNEL_SIZE)] = 1;

	if(!(convolvedMatrix = calloc(size*size,sizeof(double))))
		error = 1;

	if(!error && !convolve2D(matrix,convolvedMatrix,size,size,kernel,KERNEL_SIZE,KERNEL_SIZE))
		error = 1;

	for(i = 0;!error && i < size*size; ++i)
		matrix[i] = convolvedMatrix[i];

	if(convolvedMatrix)
		free(convolvedMatrix);
	return error;
}

int getShiftedInverse(double* matrix,int size)
{
	int i;
	double* matrixFourth = NULL;
	int error = 0;
	int N = size/2;
	
	if(!(matrixFourth = calloc(N*N,sizeof(double))))
		error = 1;
	
	for(i = 0 ;!error && i < 4; ++i)
	{
		getMatrixsFourth(matrix,size,matrixFourth,i+1);
		if(invertMatrix(matrixFourth,size/2))
		{
			error = 1;
		}	
		else
		{
			fillInMatrixsFourth(matrix,size,matrixFourth,i+1);
		}
	}

	if(matrixFourth)
		free(matrixFourth);
	return error;
}

int getShifted2DConvolution(double* matrix,int size)
{
	int i;
	double* matrixFourth = NULL;
	int error = 0;
	int N = size/2;
	
	if(!(matrixFourth = calloc(N*N,sizeof(double))))
		error = 1;
	
	for(i = 0 ;!error && i < 4; ++i)
	{
		getMatrixsFourth(matrix,size,matrixFourth,i+1);
		if(convolve2DMatrix(matrixFourth,size/2))
		{
			error = 1;
		}	
		else
		{
			fillInMatrixsFourth(matrix,size,matrixFourth,i+1);
		}
	}

	if(matrixFourth)
		free(matrixFourth);
	return error;
}

/*******************************************************************************************************/
/*******************************************************************************************************/
/*******************************************************************************************************/

///////////////////////////////////////////////////////////////////////////////
// double float precision version:
///////////////////////////////////////////////////////////////////////////////
static int convolve2D(double* in, double* out, int dataSizeX, int dataSizeY, 
                double* kernel, int kernelSizeX, int kernelSizeY)
{
    int i, j, m, n;
    double *inPtr, *inPtr2, *outPtr, *kPtr;
    int kCenterX, kCenterY;
    int rowMin, rowMax;                             // to check boundary of input array
    int colMin, colMax;                             //

    // check validity of params
    if(!in || !out || !kernel) return 0;
    if(dataSizeX <= 0 || kernelSizeX <= 0) return 0;

    // find center position of kernel (half of kernel size)
    kCenterX = kernelSizeX >> 1;
    kCenterY = kernelSizeY >> 1;

    // init working  pointers
    inPtr = inPtr2 = &in[dataSizeX * kCenterY + kCenterX];  // note that  it is shifted (kCenterX, kCenterY),
    outPtr = out;
    kPtr = kernel;

    // start convolution
    for(i= 0; i < dataSizeY; ++i)                   // number of rows
    {
        // compute the range of convolution, the current row of kernel should be between these
        rowMax = i + kCenterY;
        rowMin = i - dataSizeY + kCenterY;

        for(j = 0; j < dataSizeX; ++j)              // number of columns
        {
            // compute the range of convolution, the current column of kernel should be between these
            colMax = j + kCenterX;
            colMin = j - dataSizeX + kCenterX;

            *outPtr = 0;                            // set to 0 before accumulate

            // flip the kernel and traverse all the kernel values
            // multiply each kernel value with underlying input data
            for(m = 0; m < kernelSizeY; ++m)        // kernel rows
            {
                // check if the index is out of bound of input array
                if(m <= rowMax && m > rowMin)
                {
                    for(n = 0; n < kernelSizeX; ++n)
                    {
                        // check the boundary of array
                        if(n <= colMax && n > colMin)
                            *outPtr += *(inPtr - n) * *kPtr;
                        ++kPtr;                     // next kernel
                    }
                }
                else
                    kPtr += kernelSizeX;            // out of bound, move to next row of kernel

                inPtr -= dataSizeX;                 // move input data 1 raw up
            }

            kPtr = kernel;                          // reset kernel to (0,0)
            inPtr = ++inPtr2;                       // next input
            ++outPtr;                               // next output
        }
    }

    return 1;
}


/* INPUT: A - array of pointers to rows of a square matrix having dimension N
 *        Tol - small tolerance number to detect failure when the matrix is near degenerate
 * OUTPUT: Matrix A is changed, it contains both matrices L-E and U as A=(L-E)+U such that P*A=L*U.
 *        The permutation matrix is not stored as a matrix, but in an integer vector P of size N+1 
 *        containing column indexes where the permutation matrix has "1". The last element P[N]=S+N, 
 *        where S is the number of row exchanges needed for determinant computation, det(P)=(-1)^S    
 */
static int LUPDecompose(double **A, int N, double Tol, int *P) 
{
    int i, j, k, imax; 
    double maxA, *ptr, absA;

    for (i = 0; i <= N; i++)
        P[i] = i; //Unit permutation matrix, P[N] initialized with N

    for (i = 0; i < N; i++) {
        maxA = 0.0;
        imax = i;

        for (k = i; k < N; k++)
            if ((absA = fabs(A[k][i])) > maxA) { 
                maxA = absA;
                imax = k;
            }

        if (maxA < Tol) 
        {
        	return 0; //failure, matrix is degenerate
        }
        if (imax != i) {
            //pivoting P
            j = P[i];
            P[i] = P[imax];
            P[imax] = j;

            //pivoting rows of A
            ptr = A[i];
            A[i] = A[imax];
            A[imax] = ptr;

            //counting pivots starting from N (for determinant)
            P[N]++;
        }

        for (j = i + 1; j < N; j++) {
            A[j][i] /= A[i][i];

            for (k = i + 1; k < N; k++)
                A[j][k] -= A[j][i] * A[i][k];
        }
    }

    return 1;  //decomposition done 
}

/* INPUT: A,P filled in LUPDecompose; N - dimension
 * OUTPUT: IA is the inverse of the initial matrix
 */
static void LUPInvert(double **A, int *P, int N, double **IA) 
{
  
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            if (P[i] == j) 
                IA[i][j] = 1.0;
            else
                IA[i][j] = 0.0;

            for (int k = 0; k < i; k++)
                IA[i][j] -= A[i][k] * IA[k][j];
        }

        for (int i = N - 1; i >= 0; i--) {
            for (int k = i + 1; k < N; k++)
                IA[i][j] -= A[i][k] * IA[k][j];

            IA[i][j] = IA[i][j] / A[i][i];
        }
    }
}

/* INPUT: A,P filled in LUPDecompose; N - dimension. 
 * OUTPUT: Function returns the determinant of the initial matrix
 */
static double LUPDeterminant(double **A, int *P, int N) 
{
    double det = A[0][0];

    for (int i = 1; i < N; i++)
        det *= A[i][i];

    if ((P[N] - N) % 2 == 0)
        return det; 
    else
        return -det;
}

/*******************************************************************************************************/
/*******************************************************************************************************/
/*******************************************************************************************************/
static void printMatrix1D(double * matrix,int size)
{
	int i,j;
	for(i = 0; i < size; ++i)
	{
		for(j = 0; j < size; ++j)
		{
			fprintf(stdout, "%.5lf ",matrix[get1dIndex(i,j,size)]);	
		}
		fprintf(stdout, "\n");
	}
}


static void printMatrix2D(double ** matrix,int numberOfRows,int numberOfColumns)
{
	for(int i = 0; i < numberOfRows; ++i)
	{
		for(int j = 0; j < numberOfColumns; ++j)
			fprintf(stdout, "%.5lf ",matrix[i][j]);
		
		fprintf(stdout, "\n");
	}
}

static void getMatrixsFourth(const double* matrix,int size,double * matrixFourth,int which)
{
	switch(which)
	{
		case 1: getPartOfMatrix1D(0,size/2,0,size/2,matrix,size,matrixFourth,size/2);break;
		case 2: getPartOfMatrix1D(0,size/2,size/2,size,matrix,size,matrixFourth,size/2);break;
		case 3: getPartOfMatrix1D(size/2,size,0,size/2,matrix,size,matrixFourth,size/2);break;
		case 4: getPartOfMatrix1D(size/2,size,size/2,size,matrix,size,matrixFourth,size/2);break;
		default:break;
	}
}

static void fillInMatrixsFourth(double* matrix,int size,const double * matrixFourth,int which)
{
	switch(which)
	{
		case 1: fillInPartOfMatrix1D(0,size/2,0,size/2,matrixFourth,size/2,matrix,size);break;
		case 2: fillInPartOfMatrix1D(0,size/2,size/2,size,matrixFourth,size/2,matrix,size);break;
		case 3: fillInPartOfMatrix1D(size/2,size,0,size/2,matrixFourth,size/2,matrix,size);break;
		case 4: fillInPartOfMatrix1D(size/2,size,size/2,size,matrixFourth,size/2,matrix,size);break;
		default:break;
	}
}

static void fillInPartOfMatrix1D(int rstart,int rend,int cstart,int cend,const double* from,int fromSize,double* to,int toSize)
{
	int i,j,k,m;
	for(i = rstart,k = 0; i < rend; ++i, ++k)
	{
		for(j = cstart,m = 0; j < cend; ++j, ++m)
		{
			to[get1dIndex(i,j,toSize)] = from[get1dIndex(k,m,fromSize)];
		}
	}
}

static void getPartOfMatrix1D(int rstart,int rend,int cstart,int cend,const double* from,int fromSize,double* to,int toSize)
{
	int i,j,k,m;
	for(i = rstart,k = 0; i < rend; ++i, ++k)
	{
		for(j = cstart,m = 0; j < cend; ++j, ++m)
		{
			to[get1dIndex(k,m,toSize)] = from[get1dIndex(i,j,fromSize)];
		}
	}
}

static int get1dIndex(int row,int col,int size)
{
	return (size * row + col);
}

static void convertFrom2Dto1D(double ** from,double* to,int size)
{
	int i,j;
	for (i = 0; i < size; ++i)
	{
		for (j = 0; j < size; ++j)
		{
			to[get1dIndex(i,j,size)] = from[i][j];
		}
	}
}

static void convertFrom1Dto2D(const double * from,double** to,int size)
{
	int i,j;
	for (i = 0; i < size; ++i)
	{
		for (j = 0; j < size; ++j)
		{
			to[i][j] = from[get1dIndex(i,j,size)];
		}
	}
}

/*******************************************************************************************************/
/*******************************************************************************************************/
/*******************************************************************************************************/