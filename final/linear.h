#ifndef LINEAR_H
#define LINEAR_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

#include "svd.h"
#include "qr.h"

#define MAX_RANGE 50 /* max matrix value*/

int generate_random_matrix(double* matrix, int rows, int cols);
void solve_for_x_using_svd(const double* _a, const double* _b, int rows, int cols, double* _x);
void solve_for_x_using_qr(const double* _a, const double* _b, int rows, int cols, double* _x);
void solve_for_x_using_mpi(const double* _a, const double* _b, int rows, int cols, double* _x); /* Moore-Penrose inverse */
double verify_x(const double* _a, const double* _b, const double* _x, int rows, int cols);

#endif