#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: receivesparse.c,v 1.5 1996/12/18 17:06:13 balay Exp bsmith $";
#endif
/*
    Part of the MatlabSockettool Package. Receive a sparse matrix
  at a socket address, called by the receive.mex4 Matlab program.

        Written by Barry Smith, bsmith@mcs.anl.gov 4/14/92

   Since this is called from Matlab it cannot be compiled with C++.
*/
#include <stdio.h>
#include <math.h>
#include "sys.h"
#include "mex.h"

#define ERROR(a) {fprintf(stderr,"RECEIVE: %s \n",a); return -1;}
#undef __FUNC__  
#define __FUNC__ "ReceiveSparseMatrix"
int ReceiveSparseMatrix(Matrix *plhs[],int t)
{
  int    *tr,*tc, compx = 0;
  int    *r, *c;
  int    i,j,m,n, nnz, lnnz, jstart,jend,off = 0;
  double *tv, *v, *diag,*vi;

  /* get size of matrix */
  if (PetscBinaryRead(t,&m,1,PETSC_INT))   ERROR("reading number columns"); 
  if (PetscBinaryRead(t,&n,1,PETSC_INT))   ERROR("reading number rows"); 
  /* get number of nonzeros */
  if (PetscBinaryRead(t,&nnz,1,PETSC_INT))   ERROR("reading nnz"); 
  if (PetscBinaryRead(t,&compx,1,PETSC_INT))   ERROR("reading row lengths"); 
  /* Create a matrix for Matlab */
  /* since Matlab stores by columns not rows we actually will 
     create transpose of desired matrix */
  plhs[0] = mxCreateSparse(n,m, nnz, compx);
  r = mxGetIr(plhs[0]);
  c = mxGetJc(plhs[0]);
  v = mxGetPr(plhs[0]);
  if (!compx) {
    if (PetscBinaryRead(t,v,nnz,PETSC_DOUBLE)) ERROR("reading values");
  } else {
    for ( i=0; i<nnz; i++ ) {
      vi = mxGetPi(plhs[0]);
      if (PetscBinaryRead(t,v+i,1,PETSC_DOUBLE)) ERROR("reading values");
      if (PetscBinaryRead(t,vi+i,1,PETSC_DOUBLE)) ERROR("reading values");
    }
  }
  if (PetscBinaryRead(t,c,m+1,PETSC_INT)) ERROR("reading column pointers");
  if (PetscBinaryRead(t,r,nnz,PETSC_INT)) ERROR("reading row pointers");
  /* pointers start at 0 not 1 */
  for ( i=0; i<m+1; i++ ) {c[i]--;}
  for ( i=0; i<nnz; i++ ) {r[i]--;}
  return 0;
}







