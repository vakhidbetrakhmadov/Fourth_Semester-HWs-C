#include "linear.h"

#ifndef NAN
#define NAN (double) (0.0/0.0)
#endif


static void free_2d_matrix(double ** matrix,int rows);
static double** allocate_2d_matrix(int rows, int cols);
static void copy_from_1d_to_2d(const double* matrix_1d, double** matrix_2d, int rows, int cols);
static void get_transpose(double** matrix,double** transpose, int rows, int cols);
static void multiply(double** matrix, double* vector, int vector_size);
static double* find_unknowns_using_bws (double ** upper_triangular_matrix_a, double* vector_b, int n);
static double** matrix_mul(double** x, double** y, int x_rows, int x_cols, int y_rows, int y_cols);

/* ------------------ My interface and wrapper functions ------------------  */
int generate_random_matrix(double* matrix, int rows, int cols)
{
	int error = 0, i = 0;
	struct timespec current_time;

	for(i = 0;!error && i < rows*cols; ++i)
	{
		if(clock_gettime(CLOCK_REALTIME,&current_time))
			error = 1;
		else
			matrix[i] = current_time.tv_nsec % MAX_RANGE + 1;
	}

	return error;
}

void solve_for_x_using_svd(const double* _a, const double* _b, int rows, int cols, double* _x)
{
	int i = 0, j = 0, k = 0;
	double** a; /* rows x cols */
	double* w;  /* cols */
	double** v; /* cols x cols */
	double* b;  /* rows */
	double* x;  /* cols */

	if(rows < cols)
	{
		for(i = 0; i < cols; ++i) _x[i] = NAN;
		return;
	}

	a = dmatrix(1,rows,1,cols);
	w = dvector(1,cols);
	v = dmatrix(1,cols,1,cols);
	b = dvector(1,rows);
	x = dvector(1,cols);

	for(i = 1, k = 0; i <= rows; ++i)
		for(j = 1; j <= cols; ++j, ++k) 
			a[i][j] = _a[k];
	for(i = 1; i <= rows; ++i) b[i] = _b[i-1]; 

	if(svdcmp(a,rows,cols,w,v))
	{
		for(i = 0; i < cols; ++i) _x[i] = NAN;
	}
	else
	{
		svbksb(a,w,v,rows,cols,b,x);
		for(i = 1; i <= cols; ++i) _x[i-1] = x[i];
	}

	free_dmatrix(a,1,rows,1,cols);
	free_dmatrix(v,1,cols,1,cols);  
	free_dvector(w,1,cols);
	free_dvector(b,1,rows);
	free_dvector(x,1,cols);
}

void solve_for_x_using_qr(const double* _a, const double* _b, int rows, int cols, double* _x)
{
	int i = 0;
	double** a = NULL;
	double** QT = NULL; 
	double* b = NULL;
	double* x = NULL;
	matrix *A = NULL;
	matrix *Q = NULL;
	matrix *R = NULL;

	if(rows < cols)
	{
		for(i = 0; i < cols; ++i) _x[i] = NAN;
		return;
	}

	a = allocate_2d_matrix(rows,cols);
	copy_from_1d_to_2d(_a,a,rows,cols);

	A = create_matrix_from_array(rows,cols,a);    
    Q = create_matrix(rows,rows);
    R = create_matrix(rows,cols);

    QRdecompose(A,Q,R);  /* Decompose 'a' to Q and R (Q size = rows x rows, R = rows x cols) */

	QT = allocate_2d_matrix(rows,rows); 
	get_transpose(Q->array,QT,rows,rows); /* find Q transpose (QT) (QT size = Q size =  rows x rows) */

	b = calloc(rows,sizeof(double));
	for(i = 0; i < rows; ++i) b[i] = _b[i];
	multiply(QT,b,rows); /* multiply  QT and b (QT*b) */
	
	x = find_unknowns_using_bws(R->array,b,cols); /*solve "R*x = QT*b" */
	for(i = 0; i < cols; ++i) _x[i] = x[i];


	if(b) free(b);
	if(x) free(x);
	free_2d_matrix(a,rows);
 	free_2d_matrix(QT,rows);
    free_matrix(A);
    free_matrix(Q);
    free_matrix(R);
}

void solve_for_x_using_mpi(const double* _a, const double* _b, int rows, int cols, double* _x) /*  Moore-Penrose inverse  */
{
	int i = 0, j = 0, k = 0;
	double** u; /* rows x cols */
	double* w;  /* cols */
	double** v; /* cols x cols */

	double** U = NULL;
	double** UT = NULL; /* U transpose*/
	double** WI = NULL;
	double** V = NULL;
	double** VWI = NULL; /* V*WI */
	double** VWIUT = NULL; /* V*WI*UT  =  Moore-Penrose inverse */
	double** b = NULL;
	double** x = NULL;

	if(rows < cols)
	{
		for(i = 0; i < cols; ++i) _x[i] = NAN;
		return;
	}
	
	u = dmatrix(1,rows,1,cols);
	w = dvector(1,cols);
	v = dmatrix(1,cols,1,cols);

	U = allocate_2d_matrix(rows,cols);
	UT = allocate_2d_matrix(cols,rows); /* U transpose*/
	WI = allocate_2d_matrix(cols,cols);  /* Inverse of the diagonal matrix of singular values */
	V = allocate_2d_matrix(cols,cols); 
	b = allocate_2d_matrix(rows,1); 


	for(i = 1, k = 0; i <= rows; ++i)
		for(j = 1; j <= cols; ++j, ++k)
			u[i][j] = _a[k];

	if(svdcmp(u,rows,cols,w,v))
	{
		for(i = 0; i < cols; ++i) _x[i] = NAN;
	}
	else
	{	
		for(i = 1; i <= rows; ++i)
			for(j = 1; j <= cols; ++j)
				U[i-1][j-1] = u[i][j];

		for(i = 1; i <= cols; ++i)
			for(j = 1; j <= cols; ++j)
				V[i-1][j-1] = v[i][j];

		for(i = 1; i <= cols; ++i) WI[i-1][i-1] = 1/w[i];	
		for(i = 0; i < rows; ++i) b[i][0] = _b[i];

		get_transpose(U,UT,rows,cols);
		VWI = matrix_mul(V,WI,cols,cols,cols,cols);
		VWIUT = matrix_mul(VWI,UT,cols,cols,cols,rows); /* Pseudo inverse */
		#if 1
		x = matrix_mul(VWIUT,b,cols,rows,rows,1);
		#endif
		for(i = 0; i < cols; ++i) _x[i] = x[i][0];
	}

	free_2d_matrix(UT,cols);
	free_2d_matrix(U,rows);
	free_2d_matrix(V,cols);
	free_2d_matrix(VWI,cols);
	free_2d_matrix(WI,cols);
	free_2d_matrix(VWIUT,cols);
	free_2d_matrix(x,cols);
	free_2d_matrix(b,rows);
	free_dmatrix(u,1,rows,1,cols);
	free_dmatrix(v,1,cols,1,cols);  
	free_dvector(w,1,cols);
}

double verify_x(const double* _a, const double* _b, const double* _x, int rows, int cols)
{
	int i = 0, j = 0, k = 0;
	double err_norm = 0;
	double** e = NULL;
	double** x = NULL;
	double** a = NULL;

	if(rows < cols)
		return NAN;

	a = allocate_2d_matrix(rows,cols);
	x = allocate_2d_matrix(cols,1);

	for(i = 0, k = 0; i < rows; ++i)
		for( j = 0; j < cols; ++j, ++k) a[i][j] = _a[k];
	for( i = 0; i < cols; ++i) x[i][0] = _x[i];

	e = matrix_mul(a,x,rows,cols,cols,1);
	
	for(i = 0; i < rows; ++i) e[i][0] -= _b[i];

	for(i = 0; i < rows; ++i) err_norm += (e[i][0]*e[i][0]);


	free_2d_matrix(a,rows);
	free_2d_matrix(x,cols);
	free_2d_matrix(e,rows);

	return fabs(sqrt(err_norm));
}

/* -------------------- END ------------------------- */

static double** matrix_mul(double** x, double** y, int x_rows, int x_cols, int y_rows, int y_cols)
{
	int i = 0, j = 0, k = 0;
	double** r = NULL;

	if (x_cols != y_rows)
		return NULL;
	
	r = allocate_2d_matrix(x_rows,y_cols);
	for (i = 0; i < x_rows; i++)
		for (j = 0; j < y_cols; j++)
			for (k = 0; k < x_cols; k++)
				r[i][j] += x[i][k] * y[k][j];
	return r;
}

static double* find_unknowns_using_bws (double ** upper_triangular_matrix_a, double* vector_b, int n)
{
	int i, j;
	double sum = 0;
	double* roots = (double*) calloc(n,sizeof(double));
	if(roots == NULL)
		return NULL;

	for(i = n-1; i >= 0; --i )
	{
		sum = 0;
		for(j = i+1 ; j < n; ++j )
		{
			sum+= upper_triangular_matrix_a[i][j] * roots[j];
		}

		roots[i] = (vector_b[i] - sum) / upper_triangular_matrix_a[i][i];
	} 

	return roots;
}

static void multiply(double** matrix, double* vector, int vector_size)
{
	int i = 0, j = 0;
	double sum = 0;
	double* temp = NULL;
	temp = calloc(vector_size,sizeof(double));

	for(i = 0; i < vector_size; ++i)
	{
		sum = 0;
		for(j = 0; j < vector_size; ++j)
			sum += matrix[i][j] * vector[j];

		temp[i] = sum;
	}

	for(i = 0; i < vector_size; ++i)
		vector[i] = temp[i];

	if(temp) free(temp);
}

static void get_transpose(double** matrix,double** transpose, int rows, int cols)
{
	int i, j;
	for(i = 0; i < rows; ++i)
		for(j = 0; j < cols; ++j)
			transpose[j][i] = matrix[i][j];
}

static void copy_from_1d_to_2d(const double*  matrix_1d, double** matrix_2d, int rows, int cols)
{
	int i,j,k;
	for(i = 0, k = 0; i < rows; ++i)
		for(j = 0; j < cols; ++j, ++k)
			matrix_2d[i][j] = matrix_1d[k];
}

static double** allocate_2d_matrix(int rows, int cols)
{
	int i = 0;
	double** matrix = NULL;
	matrix = (double**) calloc(rows,sizeof(double*));
	for(i = 0; i < rows; ++i)
	{
		matrix[i] = calloc(cols,sizeof(double));
		memset(matrix[i],'\0',sizeof(double)*cols);
	}

	return matrix;
}

static void free_2d_matrix(double ** matrix, int rows)
{
	int i = 0;
	for (i = 0; i < rows; ++i)
		free(matrix[i]);
	free(matrix);
}