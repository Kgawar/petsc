
static char help[] = "Tests sequential and parallel MatMatMult() and MatPtAP(), sequential MatMatMultTranspose()\n\
Input arguments are:\n\
  -f0 <input_file> -f1 <input_file> -f2 <input_file> -f3 <input_file> : file to load\n\n";
/* e.g., ex94 -f0 $D/small -f1 $D/small -f2 $D/arco1 -f3 $D/arco1 */

#include "petscmat.h"

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **args)
{
  Mat            A,A_save,B,P,C;
  Vec            x,v1,v2;
  PetscViewer    viewer;
  PetscErrorCode ierr;
  PetscMPIInt    size,rank;
  PetscInt       i,m,n,j,idxn[10],M,N,nzp;
  PetscReal      norm,norm_tmp,tol=1.e-10,fill=4.0;
  PetscRandom    rdm;
  char           file[4][128];
  PetscTruth     flg,preload = PETSC_TRUE;
  PetscScalar    a[10],rval,alpha,none = -1.0;
  PetscTruth     Test_MatMatMult=PETSC_TRUE,Test_MatMatMultTr=PETSC_TRUE,Test_MatPtAP=PETSC_TRUE;
  Vec            v3,v4,v5;

  PetscInitialize(&argc,&args,(char *)0,help);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);

  /*  Load the matrices A and B */
  ierr = PetscOptionsGetString(PETSC_NULL,"-f0",file[0],127,&flg);CHKERRQ(ierr);
  if (!flg) SETERRQ(1,"Must indicate a file name for small matrix A with the -f0 option.");
  ierr = PetscOptionsGetString(PETSC_NULL,"-f1",file[1],127,&flg);CHKERRQ(ierr);
  if (!flg) SETERRQ(1,"Must indicate a file name for small matrix B with the -f1 option.");
  ierr = PetscOptionsGetString(PETSC_NULL,"-f2",file[2],127,&flg);CHKERRQ(ierr);
  if (!flg) {
    preload = PETSC_FALSE;
  } else {
    ierr = PetscOptionsGetString(PETSC_NULL,"-f3",file[3],127,&flg);CHKERRQ(ierr);
    if (!flg) SETERRQ(1,"Must indicate a file name for test matrix B with the -f3 option."); 
  }

  PreLoadBegin(preload,"Load system");
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,file[2*PreLoadIt],PETSC_FILE_RDONLY,&viewer);CHKERRQ(ierr);
  ierr = MatLoad(viewer,MATAIJ,&A_save);CHKERRQ(ierr); 
  ierr = PetscViewerDestroy(viewer);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,file[2*PreLoadIt+1],PETSC_FILE_RDONLY,&viewer);CHKERRQ(ierr);
  ierr = MatLoad(viewer,MATAIJ,&B);CHKERRQ(ierr); 
  ierr = PetscViewerDestroy(viewer);CHKERRQ(ierr);
 
  /* Create vectors v1 and v2 that are compatible with A_save */
  ierr = VecCreate(PETSC_COMM_WORLD,&v1);CHKERRQ(ierr);
  ierr = MatGetLocalSize(A_save,&m,PETSC_NULL);CHKERRQ(ierr);
  ierr = VecSetSizes(v1,m,PETSC_DECIDE);CHKERRQ(ierr);
  ierr = VecSetFromOptions(v1);CHKERRQ(ierr);
  ierr = VecDuplicate(v1,&v2);CHKERRQ(ierr);

  ierr = PetscRandomCreate(PETSC_COMM_WORLD,RANDOM_DEFAULT,&rdm);CHKERRQ(ierr);
  ierr = PetscOptionsGetReal(PETSC_NULL,"-fill",&fill,PETSC_NULL);CHKERRQ(ierr);

  /* Test MatMatMult() */
  /*-------------------*/
  if (Test_MatMatMult){
    ierr = MatDuplicate(A_save,MAT_COPY_VALUES,&A);CHKERRQ(ierr);
    ierr = MatMatMult(A,B,MAT_INITIAL_MATRIX,fill,&C);CHKERRQ(ierr);
   
    /* Test MAT_REUSE_MATRIX - reuse symbolic C */
    alpha=1.0;
    for (i=0; i<2; i++){
      alpha -=0.1;
      ierr = MatScale(&alpha,A);CHKERRQ(ierr);
      ierr = MatMatMult(A,B,MAT_REUSE_MATRIX,fill,&C);CHKERRQ(ierr);
    }
  
    /* Create vector x that is compatible with B */
    ierr = VecCreate(PETSC_COMM_WORLD,&x);CHKERRQ(ierr);
    ierr = MatGetLocalSize(B,PETSC_NULL,&n);CHKERRQ(ierr);
    ierr = VecSetSizes(x,n,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetFromOptions(x);CHKERRQ(ierr);

    norm = 0.0;
    for (i=0; i<10; i++) {
      ierr = VecSetRandom(rdm,x);CHKERRQ(ierr);
      ierr = MatMult(B,x,v1);CHKERRQ(ierr);  
      ierr = MatMult(A,v1,v2);CHKERRQ(ierr);  /* v2 = A*B*x */
      ierr = MatMult(C,x,v1);CHKERRQ(ierr);   /* v1 = C*x   */
      ierr = VecAXPY(&none,v2,v1);CHKERRQ(ierr);
      ierr = VecNorm(v1,NORM_2,&norm_tmp);CHKERRQ(ierr);
      if (norm_tmp > norm) norm = norm_tmp;
    }
    if (norm >= tol) {
      ierr = PetscPrintf(PETSC_COMM_SELF,"Error: MatMatMult(), |v1 - v2|: %g\n",norm);CHKERRQ(ierr);
    }
    ierr = MatDestroy(A);CHKERRQ(ierr);
    ierr = MatDestroy(C);CHKERRQ(ierr);
    ierr = VecDestroy(x);CHKERRQ(ierr);
  } /* if (Test_MatMatMult) */

  /* Test MatMatMultTranspose() */
  /*----------------------------*/
  if (size>1) Test_MatMatMultTr = PETSC_FALSE;
  if (Test_MatMatMultTr){
    PetscInt PN;
    ierr = MatGetSize(B,&M,&N);CHKERRQ(ierr);
    PN   = M/2;
    nzp  = 5;
    ierr = MatCreate(PETSC_COMM_WORLD,PETSC_DECIDE,PETSC_DECIDE,M,PN,&P);CHKERRQ(ierr); 
    ierr = MatSetType(P,MATAIJ);CHKERRQ(ierr);
    ierr = MatSeqAIJSetPreallocation(P,nzp,PETSC_NULL);CHKERRQ(ierr);
    ierr = MatMPIAIJSetPreallocation(P,nzp,PETSC_NULL,nzp,PETSC_NULL);CHKERRQ(ierr);
    for (i=0; i<nzp; i++){
      ierr = PetscRandomGetValue(rdm,&a[i]);CHKERRQ(ierr);
    }
    for (i=0; i<M; i++){
      for (j=0; j<nzp; j++){
        ierr = PetscRandomGetValue(rdm,&rval);CHKERRQ(ierr);
        idxn[j] = (PetscInt)(PetscRealPart(rval)*PN);
      }
      ierr = MatSetValues(P,1,&i,nzp,idxn,a,ADD_VALUES);CHKERRQ(ierr);
    }
    ierr = MatAssemblyBegin(P,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(P,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    
    ierr = MatMatMultTranspose(P,B,MAT_INITIAL_MATRIX,fill,&C);CHKERRQ(ierr);

    /* Test MAT_REUSE_MATRIX - reuse symbolic C */
    alpha=1.0;
    for (i=0; i<2; i++){
      alpha -=0.1;
      ierr = MatScale(&alpha,P);CHKERRQ(ierr);
      ierr = MatMatMultTranspose(P,B,MAT_REUSE_MATRIX,fill,&C);CHKERRQ(ierr);
    }

    /* Create vector x, v5 that are compatible with B */
    ierr = VecCreate(PETSC_COMM_WORLD,&x);CHKERRQ(ierr);
    ierr = MatGetLocalSize(B,&m,&n);CHKERRQ(ierr);
    ierr = VecSetSizes(x,n,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetFromOptions(x);CHKERRQ(ierr);

    ierr = VecCreate(PETSC_COMM_WORLD,&v5);CHKERRQ(ierr);
    ierr = VecSetSizes(v5,m,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetFromOptions(v5);CHKERRQ(ierr);
  
    ierr = MatGetLocalSize(P,PETSC_NULL,&n);CHKERRQ(ierr);
    ierr = VecCreate(PETSC_COMM_WORLD,&v3);CHKERRQ(ierr);
    ierr = VecSetSizes(v3,n,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetFromOptions(v3);CHKERRQ(ierr);
    ierr = VecDuplicate(v3,&v4);CHKERRQ(ierr);

    norm = 0.0; 
    for (i=0; i<10; i++) {
      ierr = VecSetRandom(rdm,x);CHKERRQ(ierr);
      ierr = MatMult(B,x,v5);CHKERRQ(ierr);            /* v5 = B*x   */
      ierr = MatMultTranspose(P,v5,v3);CHKERRQ(ierr);  /* v3 = Pt*B*x */
      ierr = MatMult(C,x,v4);CHKERRQ(ierr);            /* v4 = C*x   */
      ierr = VecAXPY(&none,v3,v4);CHKERRQ(ierr);
      ierr = VecNorm(v4,NORM_2,&norm_tmp);CHKERRQ(ierr);
      if (norm_tmp > norm) norm = norm_tmp;
    }
    if (norm >= tol) {
      ierr = PetscPrintf(PETSC_COMM_SELF,"Error: MatMatMultTr(), |v3 - v4|: %g\n",norm);CHKERRQ(ierr);
    }
    ierr = MatDestroy(P);CHKERRQ(ierr);
    ierr = MatDestroy(C);CHKERRQ(ierr);
    ierr = VecDestroy(v3);CHKERRQ(ierr);
    ierr = VecDestroy(v4);CHKERRQ(ierr);
    ierr = VecDestroy(v5);CHKERRQ(ierr);
    ierr = VecDestroy(x);CHKERRQ(ierr);
  }

  /* Test MatPtAP() */
  /*----------------------*/
  if (Test_MatPtAP){
    PetscInt PN;
    ierr = MatDuplicate(A_save,MAT_COPY_VALUES,&A);CHKERRQ(ierr);
    ierr = MatGetSize(A,&M,&N);CHKERRQ(ierr);
    PN   = M/2; 
    nzp  = 5; 
    ierr = MatCreate(PETSC_COMM_WORLD,PETSC_DECIDE,PETSC_DECIDE,N,PN,&P);CHKERRQ(ierr); 
    ierr = MatSetType(P,MATAIJ);CHKERRQ(ierr);
    ierr = MatSeqAIJSetPreallocation(P,nzp,PETSC_NULL);CHKERRQ(ierr);
    ierr = MatMPIAIJSetPreallocation(P,nzp,PETSC_NULL,nzp,PETSC_NULL);CHKERRQ(ierr);
    for (i=0; i<nzp; i++){
      ierr = PetscRandomGetValue(rdm,&a[i]);CHKERRQ(ierr);
    }
    for (i=0; i<M; i++){
      for (j=0; j<nzp; j++){
        ierr = PetscRandomGetValue(rdm,&rval);CHKERRQ(ierr);
        idxn[j] = (PetscInt)(PetscRealPart(rval)*PN);
      }
      ierr = MatSetValues(P,1,&i,nzp,idxn,a,ADD_VALUES);CHKERRQ(ierr);
    }
    ierr = MatAssemblyBegin(P,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(P,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
   
    ierr = MatPtAP(A,P,MAT_INITIAL_MATRIX,fill,&C);CHKERRQ(ierr); 

    /* Test MAT_REUSE_MATRIX - reuse symbolic C */
    alpha=1.0;
    for (i=0; i<2; i++){
      alpha -=0.1;
      ierr = MatScale(&alpha,A);CHKERRQ(ierr);
      ierr = MatPtAP(A,P,MAT_REUSE_MATRIX,fill,&C);CHKERRQ(ierr);
    }

    /* Create vector x that is compatible with P */
    ierr = VecCreate(PETSC_COMM_WORLD,&x);CHKERRQ(ierr);
    ierr = MatGetLocalSize(P,&m,&n);CHKERRQ(ierr);
    ierr = VecSetSizes(x,n,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetFromOptions(x);CHKERRQ(ierr);
  
    ierr = VecCreate(PETSC_COMM_WORLD,&v3);CHKERRQ(ierr);
    ierr = VecSetSizes(v3,n,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetFromOptions(v3);CHKERRQ(ierr);
    ierr = VecDuplicate(v3,&v4);CHKERRQ(ierr);

    norm = 0.0;
    for (i=0; i<10; i++) {
      ierr = VecSetRandom(rdm,x);CHKERRQ(ierr);
      ierr = MatMult(P,x,v1);CHKERRQ(ierr);  
      ierr = MatMult(A,v1,v2);CHKERRQ(ierr);  /* v2 = A*P*x */

      ierr = MatMultTranspose(P,v2,v3);CHKERRQ(ierr); /* v3 = Pt*A*P*x */
      ierr = MatMult(C,x,v4);CHKERRQ(ierr);           /* v3 = C*x   */
      ierr = VecAXPY(&none,v3,v4);CHKERRQ(ierr);
      ierr = VecNorm(v4,NORM_2,&norm_tmp);CHKERRQ(ierr);
      if (norm_tmp > norm) norm = norm_tmp;
    }
    if (norm >= tol) {
      ierr = PetscPrintf(PETSC_COMM_SELF,"Error: MatPtAP(), |v1 - v2|: %g\n",norm);CHKERRQ(ierr);
    }
  
    ierr = MatDestroy(A);CHKERRQ(ierr);
    ierr = MatDestroy(P);CHKERRQ(ierr);
    ierr = MatDestroy(C);CHKERRQ(ierr);
    ierr = VecDestroy(v3);CHKERRQ(ierr);
    ierr = VecDestroy(v4);CHKERRQ(ierr);
    ierr = VecDestroy(x);CHKERRQ(ierr);
  } /* if (Test_MatPtAP) */

  /* Destroy objects */
  ierr = VecDestroy(v1);CHKERRQ(ierr);
  ierr = VecDestroy(v2);CHKERRQ(ierr);
  ierr = PetscRandomDestroy(rdm);CHKERRQ(ierr);
  
  ierr = MatDestroy(A_save);CHKERRQ(ierr);
  ierr = MatDestroy(B);CHKERRQ(ierr);

  PreLoadEnd();
  ierr = PetscFinalize();CHKERRQ(ierr);

  return 0;
}

