#ifndef SVD_H
#define SVD_H

/*SVD Decomposition
*
* Source: "Numerical Recipes in C"
* (Cambridge Univ. Press) by W.H. Press, S.A. Teukolsky, W.T. Vetterling,
* and B.P. Flannery) 
*
*******************************************************************************
Given a matrix a[1..m][1..n], this routine computes its singular value
decomposition, A = U.W.VT.  The matrix U replaces a on output.  The diagonal
matrix of singular values W is output as a vector w[1..n].  The matrix V (not
the transpose VT) is output as v[1..n][1..n]. */

#include <math.h>
#include <stdlib.h>

/* a = mxn size matrix ( u on output)
*  w = n size vector
*  v = nxn size matrix
*  m = row dimension of a
*  n = column dimension of a
*/
int svdcmp(double **a, int m, int n, double* w, double **v);

/* u = mxn size matrix
*  w = n size vector
*  v = nxn size matrix
*  b = m size vector
*  x = n size vector
*/
void svbksb(double **u, double* w, double **v, int m, int n, double* b, double* x);

void free_dvector(double *v, int nl, int nh);
double *dvector(int nl, int nh);
double **dmatrix(int nrl, int nrh, int ncl, int nch);
void free_dmatrix(double **m, long nrl, long nrh, long ncl, long nch);
void free_dvector(double *v, int nl, int nh);

#endif