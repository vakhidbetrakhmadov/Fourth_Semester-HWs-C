#ifndef MATRIX_H
#define MATRIX_H

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define KERNEL_SIZE 3

int findDeterminant(const double* matrix,int size,double *determinant);
int generateInvertableMatrix(double * buffer,int size);
int getShiftedInverse(double* matrix,int size);
int getShifted2DConvolution(double* matrix,int size);

int get1dIndex(int row,int col,int size);
void printMatrix1D(double* matrix,int size);
void printMatrix2D(double ** matrix,int numberOfRows,int numberOfColumns);


#endif