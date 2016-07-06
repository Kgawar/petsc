
static char help[] = "Tests the use of MatZeroRows() and MatZeroRowsLocal() for parallel MATIS matrices.\n\
This example also tests the use of MatView(), MatDuplicate(), MatCopy(), MatGetSubMatrix(), MatGetLocalSubMatrix() and MatISGetMPIXAIJ() for MATIS";

#include <petscmat.h>

PetscErrorCode TestMatZeroRows(Mat,Mat,IS,PetscScalar);

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **args)
{
  Mat                    A,B,Asub,Bsub,Bcheck;
  ISLocalToGlobalMapping cmap,rmap;
  IS                     is,is2,rows,cols;
  PetscScalar            diag = 2.;
  PetscReal              error;
  PetscInt               n,m,i;
  PetscMPIInt            rank,size;
  PetscErrorCode         ierr;

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
  m = n = 2*size;
  ierr = PetscOptionsGetInt(NULL,NULL,"-m",&m,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(NULL,NULL,"-n",&n,NULL);CHKERRQ(ierr);
  if ((size > 1 && m < 4) || m < 2) SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_ARG_WRONG,"Number of rows should be more than two");
  if (n < 2) SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_ARG_WRONG,"Number of cols should be more than two");

  /* create a MATIS matrix */
  ierr = MatCreate(PETSC_COMM_WORLD,&A);CHKERRQ(ierr);
  ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,m,n);CHKERRQ(ierr);
  ierr = MatSetType(A,MATIS);CHKERRQ(ierr);
  /* each process has the same l2gmap for rows (same for cols) */
  ierr = ISCreateStride(PETSC_COMM_WORLD,n,0,1,&is);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingCreateIS(is,&cmap);CHKERRQ(ierr);
  ierr = ISDestroy(&is);CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_WORLD,m,0,1,&is);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingCreateIS(is,&rmap);CHKERRQ(ierr);
  ierr = ISDestroy(&is);CHKERRQ(ierr);
  ierr = MatSetLocalToGlobalMapping(A,rmap,cmap);CHKERRQ(ierr);
  ierr = MatSetUp(A);CHKERRQ(ierr);
  ierr = MatISSetPreallocation(A,3,NULL,0,NULL);CHKERRQ(ierr);
  for (i=0; i<m; i++) {
    PetscScalar v[3] = { -1.*(i+1),2.*(i+1),-1.*(i+1)};
    PetscInt    cols[3] = {(i-1+n)%n,i%n,(i+1)%n};
 
    ierr = MatSetValuesLocal(A,1,&i,3,cols,v,ADD_VALUES);CHKERRQ(ierr);
  }
  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  /* test MatView */
  ierr = MatView(A,NULL);CHKERRQ(ierr);

  /* test MatGetLocalSubMatrix */
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Test MatGetLocalSubMatrix\n");CHKERRQ(ierr);
  if (size != 1) {
    PetscInt st,len;
    st = PetscMax(rank-1,0);
    len = PetscMin(3,m-st);
    ierr = ISCreateStride(PETSC_COMM_WORLD,len,st,1,&rows);CHKERRQ(ierr);
    st = PetscMax(rank-1,0);
    len = PetscMin(2,m-st);
    ierr = ISCreateStride(PETSC_COMM_WORLD,len,st,1,&cols);CHKERRQ(ierr);
  } else {
    ierr = ISCreateStride(PETSC_COMM_WORLD,1,0,1,&rows);CHKERRQ(ierr);
    ierr = ISCreateStride(PETSC_COMM_WORLD,1,1,1,&cols);CHKERRQ(ierr);
  }
  ierr = ISView(rows,NULL);CHKERRQ(ierr);
  ierr = ISView(cols,NULL);CHKERRQ(ierr);
  ierr = MatGetLocalSubMatrix(A,rows,cols,&B);CHKERRQ(ierr);
  ierr = ISDestroy(&rows);CHKERRQ(ierr);
  ierr = ISDestroy(&cols);CHKERRQ(ierr);
  ierr = MatView(B,NULL);CHKERRQ(ierr);
  ierr = MatDestroy(&B);CHKERRQ(ierr);

  /* Create a MPIAIJ matrix, same as A */
  ierr = MatCreate(PETSC_COMM_WORLD,&B);CHKERRQ(ierr);
  ierr = MatSetSizes(B,PETSC_DECIDE,PETSC_DECIDE,m,n);CHKERRQ(ierr);
  ierr = MatSetType(B,MATAIJ);CHKERRQ(ierr);
  ierr = MatSetUp(B);CHKERRQ(ierr);
  ierr = MatSetLocalToGlobalMapping(B,rmap,cmap);CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(B,3,NULL,3,NULL);CHKERRQ(ierr);
  for (i=0; i<m; i++) {
    PetscScalar v[3] = { -1.*(i+1),2.*(i+1),-1.*(i+1)};
    PetscInt    cols[3] = {(i-1+n)%n,i%n,(i+1)%n};
 
    ierr = MatSetValuesLocal(B,1,&i,3,cols,v,ADD_VALUES);CHKERRQ(ierr);
  }
  ierr = MatAssemblyBegin(B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingDestroy(&cmap);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingDestroy(&rmap);CHKERRQ(ierr);

  /* test MatISGetMPIXAIJ */
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Test MatISGetMPIXAIJ\n");CHKERRQ(ierr);
  ierr = MatISGetMPIXAIJ(A,MAT_INITIAL_MATRIX,&Bcheck);CHKERRQ(ierr);
  ierr = MatAXPY(Bcheck,-1.,B,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
  ierr = MatNorm(Bcheck,NORM_INFINITY,&error);CHKERRQ(ierr);
  if (error > PETSC_SQRT_MACHINE_EPSILON) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR ON ASSEMBLY %g\n",error);CHKERRQ(ierr);
  }
  ierr = MatDestroy(&Bcheck);CHKERRQ(ierr);

  /* test MatGetSubMatrix */
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Test MatGetSubMatrix\n");CHKERRQ(ierr);
  if (!rank) {
    ierr = ISCreateStride(PETSC_COMM_WORLD,1,1,1,&is);CHKERRQ(ierr);
    ierr = ISCreateStride(PETSC_COMM_WORLD,2,0,1,&is2);CHKERRQ(ierr);
  } else if (rank == 1) {
    ierr = ISCreateStride(PETSC_COMM_WORLD,1,0,1,&is);CHKERRQ(ierr);
    ierr = ISCreateStride(PETSC_COMM_WORLD,1,3,1,&is2);CHKERRQ(ierr);
  } else if (rank == 2 && n > 4) {
    ierr = ISCreateStride(PETSC_COMM_WORLD,0,0,1,&is);CHKERRQ(ierr);
    ierr = ISCreateStride(PETSC_COMM_WORLD,n-4,4,1,&is2);CHKERRQ(ierr);
  } else {
    ierr = ISCreateStride(PETSC_COMM_WORLD,0,0,1,&is);CHKERRQ(ierr);
    ierr = ISCreateStride(PETSC_COMM_WORLD,0,0,1,&is2);CHKERRQ(ierr);
  }
  ierr = MatGetSubMatrix(A,is,is,MAT_INITIAL_MATRIX,&Asub);CHKERRQ(ierr);
  ierr = MatGetSubMatrix(B,is,is,MAT_INITIAL_MATRIX,&Bsub);CHKERRQ(ierr);
  ierr = MatISGetMPIXAIJ(Asub,MAT_INITIAL_MATRIX,&Bcheck);CHKERRQ(ierr);
  ierr = MatAXPY(Bcheck,-1.,Bsub,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
  ierr = MatNorm(Bcheck,NORM_INFINITY,&error);CHKERRQ(ierr);
  if (error > PETSC_SQRT_MACHINE_EPSILON) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR ON FIRST GetSubMatrix %g\n",error);CHKERRQ(ierr);
  }

  ierr = MatGetSubMatrix(A,is,is,MAT_REUSE_MATRIX,&Asub);CHKERRQ(ierr);
  ierr = MatISGetMPIXAIJ(Asub,MAT_REUSE_MATRIX,&Bcheck);CHKERRQ(ierr);
  ierr = MatAXPY(Bcheck,-1.,Bsub,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
  ierr = MatNorm(Bcheck,NORM_INFINITY,&error);CHKERRQ(ierr);
  if (error > PETSC_SQRT_MACHINE_EPSILON) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR ON REUSE GetSubMatrix %g\n",error);CHKERRQ(ierr);
  }
  ierr = MatDestroy(&Asub);CHKERRQ(ierr);
  ierr = MatDestroy(&Bsub);CHKERRQ(ierr);
  ierr = MatDestroy(&Bcheck);CHKERRQ(ierr);

  ierr = MatGetSubMatrix(A,is,is2,MAT_INITIAL_MATRIX,&Asub);CHKERRQ(ierr);
  ierr = MatGetSubMatrix(B,is,is2,MAT_INITIAL_MATRIX,&Bsub);CHKERRQ(ierr);
  ierr = MatISGetMPIXAIJ(Asub,MAT_INITIAL_MATRIX,&Bcheck);CHKERRQ(ierr);
  ierr = MatAXPY(Bcheck,-1.,Bsub,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
  ierr = MatNorm(Bcheck,NORM_INFINITY,&error);CHKERRQ(ierr);
  if (error > PETSC_SQRT_MACHINE_EPSILON) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR ON SECOND GetSubMatrix %g\n",error);CHKERRQ(ierr);
  }
  ierr = MatDestroy(&Asub);CHKERRQ(ierr);
  ierr = MatDestroy(&Bsub);CHKERRQ(ierr);
  ierr = MatDestroy(&Bcheck);CHKERRQ(ierr);
  ierr = ISDestroy(&is);CHKERRQ(ierr);
  ierr = ISDestroy(&is2);CHKERRQ(ierr);

  /* Create an IS required by MatZeroRows(): just rank zero provides the rows to be eliminated */
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Test MatZeroRows\n");CHKERRQ(ierr);
  if (!rank) {
    PetscInt st = (m+1)/2;
    PetscInt len = PetscMin(m/2,PetscMax(m-(m+1)/2-1,0));
    ierr = ISCreateStride(PETSC_COMM_WORLD,len,st,1,&is);CHKERRQ(ierr);
  } else {
    ierr = ISCreateStride(PETSC_COMM_WORLD,0,0,1,&is);CHKERRQ(ierr);
  }
  ierr = TestMatZeroRows(A,B,is,0.0);CHKERRQ(ierr);
  ierr = TestMatZeroRows(A,B,is,diag);CHKERRQ(ierr);

  ierr = MatDestroy(&A);CHKERRQ(ierr);
  ierr = MatDestroy(&B);CHKERRQ(ierr);
  ierr = ISDestroy(&is);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}

#undef __FUNCT__
#define __FUNCT__ "TestMatZeroRows"
PetscErrorCode TestMatZeroRows(Mat A,Mat Afull, IS is, PetscScalar diag)
{
  Mat                    B,Bcheck;
  Vec                    x = NULL, b = NULL, b2 = NULL;
  ISLocalToGlobalMapping l2gr,l2gc;
  PetscReal              error;
  char                   diagstr[16];
  const PetscInt         *idxs;
  PetscInt               rst,ren,i,n,M,N,d;
  PetscMPIInt            rank;
  PetscBool              miss,square;
  PetscErrorCode         ierr;

  PetscFunctionBeginUser;
  if (diag == 0.) {
    ierr = PetscStrcpy(diagstr,"zero");CHKERRQ(ierr);
  } else {
    ierr = PetscStrcpy(diagstr,"nonzero");CHKERRQ(ierr);
  }
  ierr = MatGetLocalToGlobalMapping(A,&l2gr,&l2gc);CHKERRQ(ierr);
  /* tests MatDuplicate and MatCopy */
  if (diag == 0.) {
    ierr = MatDuplicate(A,MAT_COPY_VALUES,&B);CHKERRQ(ierr);
  } else {
    ierr = MatDuplicate(A,MAT_DO_NOT_COPY_VALUES,&B);CHKERRQ(ierr);
    ierr = MatCopy(A,B,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
  }
  ierr = MatGetSize(A,&M,&N);CHKERRQ(ierr);
  square = M == N ? PETSC_TRUE : PETSC_FALSE;
  if (square) {
    ierr = MatCreateVecs(B,&x,&b);CHKERRQ(ierr);
    ierr = VecSetLocalToGlobalMapping(b,l2gr);CHKERRQ(ierr);
    ierr = VecSetLocalToGlobalMapping(x,l2gc);CHKERRQ(ierr);
    ierr = VecSetRandom(x,NULL);CHKERRQ(ierr);
    ierr = VecSetRandom(b,NULL);CHKERRQ(ierr);
    /* mimic b[is] = x[is] */
    ierr = VecDuplicate(b,&b2);CHKERRQ(ierr);
    ierr = VecSetLocalToGlobalMapping(b2,l2gr);CHKERRQ(ierr);
    ierr = VecCopy(b,b2);CHKERRQ(ierr);
    ierr = ISGetLocalSize(is,&n);CHKERRQ(ierr);
    ierr = ISGetIndices(is,&idxs);CHKERRQ(ierr);
    ierr = VecGetSize(x,&N);CHKERRQ(ierr);
    for (i=0;i<n;i++) {
      if (0 <= idxs[i] && idxs[i] < N) {
        ierr = VecSetValue(b2,idxs[i],diag,INSERT_VALUES);CHKERRQ(ierr);
        ierr = VecSetValue(x,idxs[i],1.,INSERT_VALUES);CHKERRQ(ierr);
      }
    }
    ierr = VecAssemblyBegin(b2);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(b2);CHKERRQ(ierr);
    ierr = VecAssemblyBegin(x);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(x);CHKERRQ(ierr);
    ierr = ISRestoreIndices(is,&idxs);CHKERRQ(ierr);
  } else {
    b = b2 = x = NULL;
  }
  /*  test ZeroRows on MATIS */
  ierr = MatZeroRowsIS(B,is,diag,x,b);CHKERRQ(ierr);
  if (square) {
    ierr = VecAXPY(b2,-1.,b);CHKERRQ(ierr);
    ierr = VecNorm(b2,NORM_INFINITY,&error);CHKERRQ(ierr);
    if (error > PETSC_SQRT_MACHINE_EPSILON) {
      ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR IN ZEROROWS ON B %g (diag %s)\n",error,diagstr);CHKERRQ(ierr);
    }
  }
  /* test MatMissingDiagonal */
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  ierr = MatMissingDiagonal(B,&miss,&d);CHKERRQ(ierr);
  ierr = MatGetOwnershipRange(B,&rst,&ren);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPushSynchronized(PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = PetscViewerASCIISynchronizedPrintf(PETSC_VIEWER_STDOUT_WORLD, "[%d] [%D,%D) Missing %D, row %D (diag %s)\n",rank,rst,ren,miss,d,diagstr);CHKERRQ(ierr);
  ierr = PetscViewerFlush(PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPopSynchronized(PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  ierr = VecDestroy(&x);CHKERRQ(ierr);
  ierr = VecDestroy(&b);CHKERRQ(ierr);
  ierr = VecDestroy(&b2);CHKERRQ(ierr);

  /* check the result of ZeroRows with that from MPIAIJ routines
     assuming that MatISGetMPIXAIJ and MatZeroRows_MPIAIJ work fine */
  ierr = MatISGetMPIXAIJ(B,MAT_INITIAL_MATRIX,&Bcheck);CHKERRQ(ierr);
  ierr = MatDestroy(&B);CHKERRQ(ierr);
  ierr = MatDuplicate(Afull,MAT_COPY_VALUES,&B);CHKERRQ(ierr);
  ierr = MatSetOption(B,MAT_NEW_NONZERO_ALLOCATION_ERR,PETSC_FALSE);CHKERRQ(ierr);
  ierr = MatZeroRowsIS(B,is,diag,NULL,NULL);CHKERRQ(ierr);
  ierr = MatAXPY(Bcheck,-1.,B,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
  ierr = MatNorm(Bcheck,NORM_INFINITY,&error);CHKERRQ(ierr);
  if (error > PETSC_SQRT_MACHINE_EPSILON) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR IN ZEROROWS %g (diag %s)\n",error,diagstr);CHKERRQ(ierr);
  }
  ierr = MatDestroy(&Bcheck);CHKERRQ(ierr);
  ierr = MatDestroy(&B);CHKERRQ(ierr);
  return 0;
}
