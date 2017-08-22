#ifndef QR_H
#define QR_H

/* General QR Decomposition*/
/* Source: https://stackoverflow.com/questions/35834294/implementing-qr-decomposition-in-c*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define FLAG "%7.3f"

typedef struct {
  double **array;    /* Pointer to an array of double double */
  int rows;       /* Number of rows */
  int cols;       /* Number of columns */
} matrix;

void QRdecompose(matrix *A, matrix *Q, matrix *R);
matrix* create_matrix(int rows, int cols);
matrix* create_matrix_from_array(int rows, int cols, double** m);
void print_matrix(matrix *m);
void free_matrix(matrix* m);


#endif