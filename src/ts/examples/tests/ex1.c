/*
       Formatted test for TS routines.

          Solves U_t = U_xx 
     F(t,u) = (u_i+1 - 2u_i + u_i-1)/h^2
       using several different schemes. 
*/

static char help[] = "Solves 1D heat equation.\n\n";

#include "petscda.h"
#include "petscsys.h"
#include "petscts.h"

#define PETSC_NEAR(a,b,c) (!(PetscAbsReal((a)-(b)) > (c)*PetscMax(PetscAbsReal(a),PetscAbsReal(b))))

typedef struct {
  Vec         global,local,localwork,solution;    /* location for local work (with ghost points) vector */
  DA          da;                    /* manages ghost point communication */
  PetscViewer viewer1,viewer2;
  PetscInt    M;                     /* total number of grid points */
  PetscReal   h;                     /* mesh width h = 1/(M-1) */
  PetscReal   norm_2,norm_max;
  PetscTruth  nox;                   /* indicates problem is to be run without graphics */ 
} AppCtx;

extern PetscErrorCode Monitor(TS,PetscInt,PetscReal,Vec,void *);
extern PetscErrorCode RHSFunctionHeat(TS,PetscReal,Vec,Vec,void*);
extern PetscErrorCode RHSMatrixFree(Mat,Vec,Vec);
extern PetscErrorCode Initial(Vec,void*);
extern PetscErrorCode RHSMatrixHeat(TS,PetscReal,Mat *,Mat *,MatStructure *,void *);
extern PetscErrorCode RHSJacobianHeat(TS,PetscReal,Vec,Mat*,Mat*,MatStructure *,void*);

#define linear_no_matrix       0
#define linear_no_time         1
#define linear                 2
#define nonlinear_no_jacobian  3
#define nonlinear              4

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **argv)
{
  PetscErrorCode ierr;
  PetscInt       time_steps = 100,steps,m;
  PetscMPIInt    size;
  PetscInt       problem = linear_no_matrix;
  PetscTruth     flg;
  AppCtx         appctx;
  PetscReal      dt,ftime;
  TS             ts;
  Mat            A = 0;
  MatStructure   A_structure;
  TSProblemType  tsproblem = TS_LINEAR;
  PetscDraw      draw;
  PetscViewer    viewer;
  char           tsinfo[120];
 
  ierr = PetscInitialize(&argc,&argv,(char*)0,help);CHKERRQ(ierr); 
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);

  appctx.M = 60;
  ierr = PetscOptionsGetInt(PETSC_NULL,"-M",&appctx.M,PETSC_NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(PETSC_NULL,"-time",&time_steps,PETSC_NULL);CHKERRQ(ierr);
    
  ierr = PetscOptionsHasName(PETSC_NULL,"-nox",&appctx.nox);CHKERRQ(ierr); 
  appctx.norm_2 = 0.0; appctx.norm_max = 0.0;

  /* Set up the ghost point communication pattern */ 
  ierr = DACreate1d(PETSC_COMM_WORLD,DA_NONPERIODIC,appctx.M,1,1,PETSC_NULL,&appctx.da);CHKERRQ(ierr);
  ierr = DACreateGlobalVector(appctx.da,&appctx.global);CHKERRQ(ierr);
  ierr = VecGetLocalSize(appctx.global,&m);CHKERRQ(ierr);
  ierr = DACreateLocalVector(appctx.da,&appctx.local);CHKERRQ(ierr);

  /* Set up display to show wave graph */

  ierr = PetscViewerDrawOpen(PETSC_COMM_WORLD,0,"",80,380,400,160,&appctx.viewer1);CHKERRQ(ierr);
  ierr = PetscViewerDrawGetDraw(appctx.viewer1,0,&draw);CHKERRQ(ierr);
  ierr = PetscDrawSetDoubleBuffer(draw);CHKERRQ(ierr);   
  ierr = PetscViewerDrawOpen(PETSC_COMM_WORLD,0,"",80,0,400,160,&appctx.viewer2);CHKERRQ(ierr);
  ierr = PetscViewerDrawGetDraw(appctx.viewer2,0,&draw);CHKERRQ(ierr);
  ierr = PetscDrawSetDoubleBuffer(draw);CHKERRQ(ierr);   


  /* make work array for evaluating right hand side function */
  ierr = VecDuplicate(appctx.local,&appctx.localwork);CHKERRQ(ierr);

  /* make work array for storing exact solution */
  ierr = VecDuplicate(appctx.global,&appctx.solution);CHKERRQ(ierr);

  appctx.h = 1.0/(appctx.M-1.0);

  /* set initial conditions */
  ierr = Initial(appctx.global,&appctx);CHKERRQ(ierr);
 
  /*
     This example is written to allow one to easily test parts 
    of TS, we do not expect users to generally need to use more
    then a single TSProblemType
  */
  ierr = PetscOptionsHasName(PETSC_NULL,"-linear_no_matrix",&flg);CHKERRQ(ierr);
  if (flg) {
    tsproblem = TS_LINEAR;
    problem   = linear_no_matrix;
  }
  ierr = PetscOptionsHasName(PETSC_NULL,"-linear_constant_matrix",&flg);CHKERRQ(ierr);
  if (flg) {
    tsproblem = TS_LINEAR;
    problem   = linear_no_time;
  }
  ierr = PetscOptionsHasName(PETSC_NULL,"-linear_variable_matrix",&flg);CHKERRQ(ierr);
  if (flg) {
    tsproblem = TS_LINEAR;
    problem   = linear;
  }
  ierr = PetscOptionsHasName(PETSC_NULL,"-nonlinear_no_jacobian",&flg);CHKERRQ(ierr);
  if (flg) {
    tsproblem = TS_NONLINEAR;
    problem   = nonlinear_no_jacobian;
  }
  ierr = PetscOptionsHasName(PETSC_NULL,"-nonlinear_jacobian",&flg);CHKERRQ(ierr);
  if (flg) {
    tsproblem = TS_NONLINEAR;
    problem   = nonlinear;
  }
    
  /* make timestep context */
  ierr = TSCreate(PETSC_COMM_WORLD,&ts);CHKERRQ(ierr);
  ierr = TSSetProblemType(ts,tsproblem);CHKERRQ(ierr);
  ierr = TSSetMonitor(ts,Monitor,&appctx,PETSC_NULL);CHKERRQ(ierr);

  dt = appctx.h*appctx.h/2.01;

  if (problem == linear_no_matrix) {
    /*
         The user provides the RHS as a Shell matrix.
    */
    ierr = MatCreateShell(PETSC_COMM_WORLD,m,appctx.M,appctx.M,appctx.M,&appctx,&A);CHKERRQ(ierr);
    ierr = MatShellSetOperation(A,MATOP_MULT,(void(*)(void))RHSMatrixFree);CHKERRQ(ierr);
    ierr = TSSetRHSMatrix(ts,A,A,PETSC_NULL,&appctx);CHKERRQ(ierr);
  } else if (problem == linear_no_time) {
    /*
         The user provides the RHS as a matrix
    */
    ierr = MatCreate(PETSC_COMM_WORLD,&A);CHKERRQ(ierr);
    ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,appctx.M,appctx.M);CHKERRQ(ierr);
    ierr = MatSetFromOptions(A);CHKERRQ(ierr);
    ierr = RHSMatrixHeat(ts,0.0,&A,&A,&A_structure,&appctx);CHKERRQ(ierr);
    ierr = TSSetRHSMatrix(ts,A,A,PETSC_NULL,&appctx);CHKERRQ(ierr);
  } else if (problem == linear) {
    /*
         The user provides the RHS as a time dependent matrix
    */
    ierr = MatCreate(PETSC_COMM_WORLD,&A);CHKERRQ(ierr);
    ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,appctx.M,appctx.M);CHKERRQ(ierr);
    ierr = MatSetFromOptions(A);CHKERRQ(ierr);
    ierr = RHSMatrixHeat(ts,0.0,&A,&A,&A_structure,&appctx);CHKERRQ(ierr);
    ierr = TSSetRHSMatrix(ts,A,A,RHSMatrixHeat,&appctx);CHKERRQ(ierr);
  } else if (problem == nonlinear_no_jacobian) {
    /*
         The user provides the RHS and a Shell Jacobian
    */
    ierr = TSSetRHSFunction(ts,RHSFunctionHeat,&appctx);CHKERRQ(ierr);
    ierr = MatCreateShell(PETSC_COMM_WORLD,m,appctx.M,appctx.M,appctx.M,&appctx,&A);CHKERRQ(ierr);
    ierr = MatShellSetOperation(A,MATOP_MULT,(void(*)(void))RHSMatrixFree);CHKERRQ(ierr);
    ierr = TSSetRHSJacobian(ts,A,A,PETSC_NULL,&appctx);CHKERRQ(ierr);  
  } else if (problem == nonlinear) {
    /*
         The user provides the RHS and Jacobian
    */
    ierr = TSSetRHSFunction(ts,RHSFunctionHeat,&appctx);CHKERRQ(ierr);
    ierr = MatCreate(PETSC_COMM_WORLD,&A);CHKERRQ(ierr);
    ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,appctx.M,appctx.M);CHKERRQ(ierr);
    ierr = MatSetFromOptions(A);CHKERRQ(ierr);
    ierr = RHSMatrixHeat(ts,0.0,&A,&A,&A_structure,&appctx);CHKERRQ(ierr);
    ierr = TSSetRHSJacobian(ts,A,A,RHSJacobianHeat,&appctx);CHKERRQ(ierr);  
  }

  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);

  ierr = TSSetInitialTimeStep(ts,0.0,dt);CHKERRQ(ierr);
  ierr = TSSetDuration(ts,time_steps,100.);CHKERRQ(ierr);
  ierr = TSSetSolution(ts,appctx.global);CHKERRQ(ierr);


  ierr = TSSetUp(ts);CHKERRQ(ierr);
  ierr = TSStep(ts,&steps,&ftime);CHKERRQ(ierr);
  ierr = PetscViewerStringOpen(PETSC_COMM_WORLD,tsinfo,120,&viewer);CHKERRQ(ierr);
  ierr = TSView(ts,viewer);CHKERRQ(ierr);

  ierr = PetscOptionsHasName(PETSC_NULL,"-test",&flg);CHKERRQ(ierr);
  if (flg) {
    PetscTruth iseuler;
    ierr = PetscTypeCompare((PetscObject)ts,"euler",&iseuler);CHKERRQ(ierr);
    if (iseuler) {
      if (!PETSC_NEAR(appctx.norm_2/steps,0.00257244,1.e-4)) {
        fprintf(stdout,"Error in Euler method: 2-norm %G expecting: 0.00257244\n",appctx.norm_2/steps);
      }
    } else {
      if (!PETSC_NEAR(appctx.norm_2/steps,0.00506174,1.e-4)) {
        fprintf(stdout,"Error in %s method: 2-norm %G expecting: 0.00506174\n",tsinfo,appctx.norm_2/steps);
      }
    }
  } else {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"%D Procs Avg. error 2 norm %G max norm %G %s\n",
                size,appctx.norm_2/steps,appctx.norm_max/steps,tsinfo);CHKERRQ(ierr);
  }

  ierr = PetscViewerDestroy(viewer);CHKERRQ(ierr);
  ierr = TSDestroy(ts);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(appctx.viewer1);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(appctx.viewer2);CHKERRQ(ierr);
  ierr = VecDestroy(appctx.localwork);CHKERRQ(ierr);
  ierr = VecDestroy(appctx.solution);CHKERRQ(ierr);
  ierr = VecDestroy(appctx.local);CHKERRQ(ierr);
  ierr = VecDestroy(appctx.global);CHKERRQ(ierr);
  ierr = DADestroy(appctx.da);CHKERRQ(ierr);
  if (A) {ierr= MatDestroy(A);CHKERRQ(ierr);}

  ierr = PetscFinalize();CHKERRQ(ierr);
  return 0;
}

/* -------------------------------------------------------------------*/
#undef __FUNCT__
#define __FUNCT__ "Initial" 
PetscErrorCode Initial(Vec global,void *ctx)
{
  AppCtx         *appctx = (AppCtx*) ctx;
  PetscScalar    *localptr,h = appctx->h;
  PetscInt       i,mybase,myend;
  PetscErrorCode ierr;

  /* determine starting point of each processor */
  ierr = VecGetOwnershipRange(global,&mybase,&myend);CHKERRQ(ierr);

  /* Initialize the array */
  ierr = VecGetArray(global,&localptr);CHKERRQ(ierr);
  for (i=mybase; i<myend; i++) {
    localptr[i-mybase] = PetscSinScalar(PETSC_PI*i*6.*h) + 3.*PetscSinScalar(PETSC_PI*i*2.*h);
  }
  ierr = VecRestoreArray(global,&localptr);CHKERRQ(ierr);
  return 0;
}

#undef __FUNCT__
#define __FUNCT__ "Solution"
/*
       Exact solution 
*/
PetscErrorCode Solution(PetscReal t,Vec solution,void *ctx)
{
  AppCtx *       appctx = (AppCtx*) ctx;
  PetscScalar    *localptr,h = appctx->h,ex1,ex2,sc1,sc2;
  PetscInt       i,mybase,myend;
  PetscErrorCode ierr;

  /* determine starting point of each processor */
  ierr = VecGetOwnershipRange(solution,&mybase,&myend);CHKERRQ(ierr);

  ex1 = exp(-36.*PETSC_PI*PETSC_PI*t); 
  ex2 = exp(-4.*PETSC_PI*PETSC_PI*t);
  sc1 = PETSC_PI*6.*h;                 sc2 = PETSC_PI*2.*h;
  ierr = VecGetArray(solution,&localptr);CHKERRQ(ierr);
  for (i=mybase; i<myend; i++) {
    localptr[i-mybase] = PetscSinScalar(sc1*(PetscReal)i)*ex1 + 3.*PetscSinScalar(sc2*(PetscReal)i)*ex2;
  }
  ierr = VecRestoreArray(solution,&localptr);CHKERRQ(ierr);
  return 0;
}

#undef __FUNCT__
#define __FUNCT__ "Monitor"
PetscErrorCode Monitor(TS ts,PetscInt step,PetscReal ltime,Vec global,void *ctx)
{
  AppCtx         *appctx = (AppCtx*) ctx;
  PetscErrorCode ierr;
  PetscReal      norm_2,norm_max;
  MPI_Comm       comm;

  ierr = PetscObjectGetComm((PetscObject)ts,&comm);CHKERRQ(ierr);

  ierr = VecView(global,appctx->viewer2);CHKERRQ(ierr);

  ierr = Solution(ltime,appctx->solution,ctx);CHKERRQ(ierr);
  ierr = VecAXPY(appctx->solution,-1.0,global);CHKERRQ(ierr);
  ierr = VecNorm(appctx->solution,NORM_2,&norm_2);CHKERRQ(ierr);
  norm_2 = sqrt(appctx->h)*norm_2;
  ierr = VecNorm(appctx->solution,NORM_MAX,&norm_max);CHKERRQ(ierr);

  if (!appctx->nox) {
    ierr = PetscPrintf(comm,"timestep %D time %G norm of error %G %G\n",step,ltime,norm_2,norm_max);CHKERRQ(ierr);
  }

  appctx->norm_2   += norm_2;
  appctx->norm_max += norm_max;

  ierr = VecView(appctx->solution,appctx->viewer1);CHKERRQ(ierr);

  return 0;
}

/* -----------------------------------------------------------------------*/
#undef __FUNCT__
#define __FUNCT__ "RHSMatrixFree"
PetscErrorCode RHSMatrixFree(Mat mat,Vec x,Vec y)
{
  PetscErrorCode  ierr;
  void            *ctx;

  MatShellGetContext(mat,(void **)&ctx);
  ierr = RHSFunctionHeat(0,0.0,x,y,ctx);CHKERRQ(ierr);
  return 0;
}

#undef __FUNCT__
#define __FUNCT__ "RHSFunctionHeat"
PetscErrorCode RHSFunctionHeat(TS ts,PetscReal t,Vec globalin,Vec globalout,void *ctx)
{
  AppCtx         *appctx = (AppCtx*) ctx;
  DA             da = appctx->da;
  Vec            local = appctx->local,localwork = appctx->localwork;
  PetscErrorCode ierr;
  PetscInt       i,localsize; 
  PetscScalar    *copyptr,*localptr,sc;

  /*Extract local array */ 
  ierr = DAGlobalToLocalBegin(da,globalin,INSERT_VALUES,local);CHKERRQ(ierr);
  ierr = DAGlobalToLocalEnd(da,globalin,INSERT_VALUES,local);CHKERRQ(ierr);
  ierr = VecGetArray(local,&localptr);CHKERRQ(ierr);

  /* Extract work vector */
  ierr = VecGetArray(localwork,&copyptr);CHKERRQ(ierr);

  /* Update Locally - Make array of new values */
  /* Note: For the first and last entry I copy the value */
  /* if this is an interior node it is irrelevant */
  sc = 1.0/(appctx->h*appctx->h);
  ierr = VecGetLocalSize(local,&localsize);CHKERRQ(ierr);
  copyptr[0] = localptr[0];
  for (i=1; i<localsize-1; i++) {
    copyptr[i] = sc * (localptr[i+1] + localptr[i-1] - 2.0*localptr[i]);
  }
  copyptr[localsize-1] = localptr[localsize-1];
  ierr = VecRestoreArray(local,&localptr);CHKERRQ(ierr);
  ierr = VecRestoreArray(localwork,&copyptr);CHKERRQ(ierr);

  /* Local to Global */
  ierr = DALocalToGlobal(da,localwork,INSERT_VALUES,globalout);CHKERRQ(ierr);
  return 0;
}

/* ---------------------------------------------------------------------*/
#undef __FUNCT__
#define __FUNCT__ "RHSMatrixHeat"
PetscErrorCode RHSMatrixHeat(TS ts,PetscReal t,Mat *AA,Mat *BB,MatStructure *str,void *ctx)
{
  Mat            A = *AA;
  AppCtx         *appctx = (AppCtx*) ctx;
  PetscErrorCode ierr;
  PetscInt       i,mstart,mend,idx[3];
  PetscMPIInt    size,rank;
  PetscScalar    v[3],stwo = -2./(appctx->h*appctx->h),sone = -.5*stwo;

  *str = SAME_NONZERO_PATTERN;

  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);

  ierr = MatGetOwnershipRange(A,&mstart,&mend);CHKERRQ(ierr);
  if (mstart == 0) {
    v[0] = 1.0;
    ierr = MatSetValues(A,1,&mstart,1,&mstart,v,INSERT_VALUES);CHKERRQ(ierr);
    mstart++;
  }
  if (mend == appctx->M) {
    mend--;
    v[0] = 1.0;
    ierr = MatSetValues(A,1,&mend,1,&mend,v,INSERT_VALUES);CHKERRQ(ierr);
  }

  /*
     Construct matrice one row at a time
  */
  v[0] = sone; v[1] = stwo; v[2] = sone;  
  for (i=mstart; i<mend; i++) {
    idx[0] = i-1; idx[1] = i; idx[2] = i+1;
    ierr = MatSetValues(A,1,&i,3,idx,v,INSERT_VALUES);CHKERRQ(ierr);
  }

  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  return 0;
}

#undef __FUNCT__
#define __FUNCT__ "RHSJacobianHeat"
PetscErrorCode RHSJacobianHeat(TS ts,PetscReal t,Vec x,Mat *AA,Mat *BB,MatStructure *str,void *ctx)
{
  return RHSMatrixHeat(ts,t,AA,BB,str,ctx);
}





