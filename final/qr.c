#include "qr.h"

static void matrix_copy_column(matrix *msrc, int col1, matrix *mdst,int col2);
static matrix* matrix_column_subtract(matrix *m1, int c1, matrix *m2, int c2);
static double vector_length(matrix *m,int column);
static matrix* matrix_column_divide(matrix *m, int c, double k);
static matrix* matrix_column_multiply(matrix *m, int c, double k);

void free_matrix(matrix* m)
{
    int i;
    for( i = 0; i < m->rows; ++i)
        free(m->array[i]);
    free(m->array);
    free(m);
}

/* Decomposes the matrix A into QR */
void QRdecompose(matrix *A, matrix *Q, matrix *R) {

  int i = 0, j = 0, k = 0;
  double r = 0;
  /* Using the Gram-Schmidt process */

  /* Temporary vector T and S used in calculations */
  matrix *T = create_matrix(A->rows, 1);
  matrix *S = create_matrix(A->rows, 1);

  for (i = 0; i < A->cols; i++) {

    matrix_copy_column(A,i,Q,i);

    for (j = 0; j < i; j++) {

      matrix_copy_column(Q,j,T,0);
      matrix_copy_column(A,i,S,0);
      r = 0;
      for (k=0; k<A->rows; k++) {
        r += T->array[k][0] * S->array[k][0];
      }

      R->array[j][i] = r;
      matrix_column_subtract(Q,i,matrix_column_multiply(T,0,r),0);

    }

    R->array[i][i] = vector_length(Q,i);
    matrix_column_divide(Q,i,R->array[i][i]);

  }

  free_matrix(T);
  free_matrix(S);
}

/* Creates a matrix and returns a pointer to the struct */
matrix* create_matrix(int rows, int cols) {

  int i = 0;
  /* Allocate memory for the matrix struct */
  matrix *array = malloc(sizeof(matrix));

  array->rows = rows;
  array->cols = cols;

  /* Allocate enough memory for all the rows in the first matrix */
  array->array = calloc(rows, sizeof(double*));

  /* Enough memory for all the columns */
  for (i=0; i<rows; i++) {
    array->array[i] = calloc(cols,sizeof(double));
  }

  return array;
}

/* Creates a matrix from a stack based array and returns a pointer to the struct */
matrix* create_matrix_from_array(int rows, int cols, double** m) {

  int i = 0, row = 0, col = 0;
  /* Allocate memory for the matrix struct */
  matrix *array = malloc(sizeof(matrix));
  array->rows = rows;
  array->cols = cols;

  /* Allocate memory for the matrix */
  array->array = malloc(sizeof(double*) * rows);

  /* Allocate memory for each array inside the matrix */
  for ( i=0; i<rows; i++) {
    array->array[i] = malloc(sizeof(double) * cols);
  }

  /* Populate the matrix with m's values */
  for ( row = 0; row < rows; row++) {
    for ( col = 0; col < cols; col++) {
      array->array[row][col] = m[row][col];
    }
  }

  return array;
}

/* Debugging purposes only */
void print_matrix(matrix *m) {
  int row = 0, col = 0;
  for ( row = 0; row < m->rows; row++) {
    printf("[");
    for ( col = 0; col < m->cols - 1; col++) {
      printf(FLAG", ", m->array[row][col]);
    }
    printf(FLAG, m->array[row][m->cols-1]);
    printf("]\n");
  }
  printf("\n");
}

/* Copies a matrix column from msrc at column col1 to mdst at column col2 */
static void matrix_copy_column(matrix *msrc, int col1, matrix *mdst,int col2) {
  int i = 0;
  for ( i=0; i<msrc->rows; i++) {
    mdst->array[i][col2] = msrc->array[i][col1];
  }
}

/* Subtracts m2's column c2 from m1's column c1 */
static matrix* matrix_column_subtract(matrix *m1, int c1, matrix *m2, int c2) {
  int i = 0;
  for ( i=0; i<m1->rows; i++) {
      m1->array[i][c1] -= m2->array[i][c2];
  }
  return m1;
}

/* Returns the length of the vector column in m */
static double vector_length(matrix *m,int column) {
  double length = 0;
  int row = 0;
  for ( row=0; row<m->rows; row++) {
    length += m->array[row][column] * m->array[row][column];
  }
  return sqrt(length);
}

/* Divides the matrix column c in m by k */
static matrix* matrix_column_divide(matrix *m, int c, double k) {
  int i = 0;
  for ( i=0; i<m->rows; i++) {
    m->array[i][c] /= k;
  }
  return m;
}

/* Multiplies the matrix column c in m by k */
static matrix* matrix_column_multiply(matrix *m, int c, double k) {
  int i = 0;
  for ( i=0; i<m->rows; i++) {
    m->array[i][c] *= k;
  }
  return m;
}