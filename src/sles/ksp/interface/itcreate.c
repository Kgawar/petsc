/*$Id: itcreate.c,v 1.193 2000/08/23 19:28:03 bsmith Exp bsmith $*/
/*
     The basic KSP routines, Create, View etc. are here.
*/
#include "src/sles/ksp/kspimpl.h"      /*I "petscksp.h" I*/
#include "petscsys.h"

PetscTruth KSPRegisterAllCalled = PETSC_FALSE;

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPView"
/*@C 
   KSPView - Prints the KSP data structure.

   Collective on KSP

   Input Parameters:
+  ksp - the Krylov space context
-  viewer - visualization context

   Note:
   The available visualization contexts include
+     VIEWER_STDOUT_SELF - standard output (default)
-     VIEWER_STDOUT_WORLD - synchronized standard
         output where only the first processor opens
         the file.  All other processors send their 
         data to the first processor to print. 

   The user can open an alternative visualization context with
   ViewerASCIIOpen() - output to a specified file.

   Level: developer

.keywords: KSP, view

.seealso: PCView(), ViewerASCIIOpen()
@*/
int KSPView(KSP ksp,Viewer viewer)
{
  char        *type;
  int         ierr;
  PetscTruth  isascii;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(ksp,KSP_COOKIE);
  if (!viewer) viewer = VIEWER_STDOUT_(ksp->comm);
  PetscValidHeaderSpecific(viewer,VIEWER_COOKIE);
  PetscCheckSameComm(ksp,viewer);

  ierr = PetscTypeCompare((PetscObject)viewer,ASCII_VIEWER,&isascii);CHKERRQ(ierr);
  if (isascii) {
    ierr = KSPGetType(ksp,&type);CHKERRQ(ierr);
    ierr = ViewerASCIIPrintf(viewer,"KSP Object:\n");CHKERRQ(ierr);
    if (type) {
      ierr = ViewerASCIIPrintf(viewer,"  type: %s\n",type);CHKERRQ(ierr);
    } else {
      ierr = ViewerASCIIPrintf(viewer,"  type: not yet set\n");CHKERRQ(ierr);
    }
    if (ksp->ops->view) {
      ierr = ViewerASCIIPushTab(viewer);CHKERRQ(ierr);
      ierr = (*ksp->ops->view)(ksp,viewer);CHKERRQ(ierr);
      ierr = ViewerASCIIPopTab(viewer);CHKERRQ(ierr);
    }
    if (ksp->guess_zero) {ierr = ViewerASCIIPrintf(viewer,"  maximum iterations=%d, initial guess is zero\n",ksp->max_it);CHKERRQ(ierr);}
    else                 {ierr = ViewerASCIIPrintf(viewer,"  maximum iterations=%d\n", ksp->max_it);CHKERRQ(ierr);}
    ierr = ViewerASCIIPrintf(viewer,"  tolerances:  relative=%g, absolute=%g, divergence=%g\n",ksp->rtol,ksp->atol,ksp->divtol);CHKERRQ(ierr);
    if (ksp->pc_side == PC_RIGHT)          {ierr = ViewerASCIIPrintf(viewer,"  right preconditioning\n");CHKERRQ(ierr);}
    else if (ksp->pc_side == PC_SYMMETRIC) {ierr = ViewerASCIIPrintf(viewer,"  symmetric preconditioning\n");CHKERRQ(ierr);}
    else                                   {ierr = ViewerASCIIPrintf(viewer,"  left preconditioning\n");CHKERRQ(ierr);}
  } else {
    if (ksp->ops->view) {
      ierr = (*ksp->ops->view)(ksp,viewer);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

/*
   Contains the list of registered KSP routines
*/
FList KSPList = 0;

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPSetAvoidNorms"
/*@C
   KSPSetAvoidNorms - Sets the KSP solver to avoid computing the residual norm
   when possible.  This, for example, reduces the number of collective operations
   when using the Krylov method as a smoother.

   Collective on KSP

   Input Parameter:
.  ksp - Krylov solver context

   Notes: 
   One cannot use the default convergence test routines when this option is 
   set, since these are based on decreases in the residual norms.  Thus, this
   option automatically switches to activate the KSPSkipConverged() test function.

   Currently only works with the CG, Richardson, Bi-CG-stab, CR, and CGS methods.

   Level: advanced

.keywords: KSP, create, context, norms

.seealso: KSPSetUp(), KSPSolve(), KSPDestroy(), KSPSkipConverged()
@*/
int KSPSetAvoidNorms(KSP ksp)
{
  int ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(ksp,KSP_COOKIE);
  ksp->avoidnorms = PETSC_TRUE;
  ierr = KSPSetConvergenceTest(ksp,KSPSkipConverged,PETSC_NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPPublish_Petsc"
static int KSPPublish_Petsc(PetscObject obj)
{
#if defined(PETSC_HAVE_AMS)
  KSP          v = (KSP) obj;
  int          ierr;
#endif

  PetscFunctionBegin;

#if defined(PETSC_HAVE_AMS)
  /* if it is already published then return */
  if (v->amem >=0) PetscFunctionReturn(0);

  ierr = PetscObjectPublishBaseBegin(obj);CHKERRQ(ierr);
  ierr = AMS_Memory_add_field((AMS_Memory)v->amem,"Iteration",&v->its,1,AMS_INT,AMS_READ,
                                AMS_COMMON,AMS_REDUCT_UNDEF);CHKERRQ(ierr);
  ierr = AMS_Memory_add_field((AMS_Memory)v->amem,"Residual",&v->rnorm,1,AMS_DOUBLE,AMS_READ,
                                AMS_COMMON,AMS_REDUCT_UNDEF);CHKERRQ(ierr);
  ierr = PetscObjectPublishBaseEnd(obj);CHKERRQ(ierr);
#endif

  PetscFunctionReturn(0);
}


#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPCreate"
/*@C
   KSPCreate - Creates the default KSP context.

   Collective on MPI_Comm

   Input Parameter:
.  comm - MPI communicator

   Output Parameter:
.  ksp - location to put the KSP context

   Notes:
   The default KSP type is GMRES with a restart of 30, using modified Gram-Schmidt
   orthogonalization.

   Level: developer

.keywords: KSP, create, context

.seealso: KSPSetUp(), KSPSolve(), KSPDestroy()
@*/
int KSPCreate(MPI_Comm comm,KSP *inksp)
{
  KSP ksp;
  int ierr;

  PetscFunctionBegin;
  *inksp = 0;
  PetscHeaderCreate(ksp,_p_KSP,struct _KSPOps,KSP_COOKIE,-1,"KSP",comm,KSPDestroy,KSPView);
  PLogObjectCreate(ksp);
  *inksp             = ksp;
  ksp->bops->publish = KSPPublish_Petsc;

  ksp->type          = -1;
  ksp->max_it        = 10000;
  ksp->pc_side       = PC_LEFT;
  ksp->use_pres      = PETSC_FALSE;
  ksp->rtol          = 1.e-5;
  ksp->atol          = 1.e-50;
  ksp->divtol        = 1.e4;
  ksp->avoidnorms    = PETSC_FALSE;

  ksp->rnorm               = 0.0;
  ksp->its                 = 0;
  ksp->guess_zero          = PETSC_TRUE;
  ksp->calc_sings          = PETSC_FALSE;
  ksp->calc_res            = PETSC_FALSE;
  ksp->res_hist            = PETSC_NULL;
  ksp->res_hist_len        = 0;
  ksp->res_hist_max        = 0;
  ksp->res_hist_reset      = PETSC_TRUE;
  ksp->numbermonitors      = 0;
  ksp->converged           = KSPDefaultConverged;
  ksp->ops->buildsolution  = KSPDefaultBuildSolution;
  ksp->ops->buildresidual  = KSPDefaultBuildResidual;

  ksp->ops->setfromoptions = 0;

  ksp->vec_sol         = 0;
  ksp->vec_rhs         = 0;
  ksp->B               = 0;

  ksp->ops->solve      = 0;
  ksp->ops->setup      = 0;
  ksp->ops->destroy    = 0;

  ksp->data            = 0;
  ksp->nwork           = 0;
  ksp->work            = 0;

  ksp->cnvP            = 0;

  ksp->reason          = KSP_CONVERGED_ITERATING;

  ksp->setupcalled     = 0;
  ierr = PetscPublishAll(ksp);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
 
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPSetType"
/*@C
   KSPSetType - Builds KSP for a particular solver. 

   Collective on KSP

   Input Parameters:
+  ksp      - the Krylov space context
-  type - a known method

   Options Database Key:
.  -ksp_type  <method> - Sets the method; use -help for a list 
    of available methods (for instance, cg or gmres)

   Notes:  
   See "petsc/include/petscksp.h" for available methods (for instance,
   KSPCG or KSPGMRES).

  Normally, it is best to use the SLESSetFromOptions() command and
  then set the KSP type from the options database rather than by using
  this routine.  Using the options database provides the user with
  maximum flexibility in evaluating the many different Krylov methods.
  The KSPSetType() routine is provided for those situations where it
  is necessary to set the iterative solver independently of the command
  line or options database.  This might be the case, for example, when
  the choice of iterative solver changes during the execution of the
  program, and the user's application is taking responsibility for
  choosing the appropriate method.  In other words, this routine is
  not for beginners.

  Level: intermediate

.keywords: KSP, set, method

.seealso: PCSetType()
@*/
int KSPSetType(KSP ksp,KSPType type)
{
  int        ierr,(*r)(KSP);
  PetscTruth match;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(ksp,KSP_COOKIE);
  PetscValidCharPointer(type);

  ierr = PetscTypeCompare((PetscObject)ksp,type,&match);CHKERRQ(ierr);
  if (match) PetscFunctionReturn(0);

  if (ksp->data) {
    /* destroy the old private KSP context */
    ierr = (*ksp->ops->destroy)(ksp);CHKERRQ(ierr);
    ksp->data = 0;
  }
  /* Get the function pointers for the iterative method requested */
  if (!KSPRegisterAllCalled) {ierr = KSPRegisterAll(PETSC_NULL);CHKERRQ(ierr);}

  ierr =  FListFind(ksp->comm,KSPList,type,(int (**)(void *)) &r);CHKERRQ(ierr);

  if (!r) SETERRQ1(1,1,"Unknown KSP type given: %s",type);

  ksp->setupcalled = 0;
  ierr = (*r)(ksp);CHKERRQ(ierr);

  ierr = PetscObjectChangeTypeName((PetscObject)ksp,type);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPRegisterDestroy"
/*@C
   KSPRegisterDestroy - Frees the list of KSP methods that were
   registered by KSPRegisterDynamic().

   Not Collective

   Level: advanced

.keywords: KSP, register, destroy

.seealso: KSPRegisterDynamic(), KSPRegisterAll()
@*/
int KSPRegisterDestroy(void)
{
  int ierr;

  PetscFunctionBegin;
  if (KSPList) {
    ierr = FListDestroy(&KSPList);CHKERRQ(ierr);
    KSPList = 0;
  }
  KSPRegisterAllCalled = PETSC_FALSE;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPGetType"
/*@C
   KSPGetType - Gets the KSP type as a string from the KSP object.

   Not Collective

   Input Parameter:
.  ksp - Krylov context 

   Output Parameter:
.  name - name of KSP method 

   Level: intermediate

.keywords: KSP, get, method, name

.seealso: KSPSetType()
@*/
int KSPGetType(KSP ksp,KSPType *type)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(ksp,KSP_COOKIE);
  *type = ksp->type_name;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPSetFromOptions"
/*@
   KSPSetFromOptions - Sets KSP options from the options database.
   This routine must be called before KSPSetUp() if the user is to be 
   allowed to set the Krylov type. 

   Collective on KSP

   Input Parameters:
.  ksp - the Krylov space context

   Options Database Keys:
+   -ksp_max_it - maximum number of linear iterations
.   -ksp_rtol rtol - relative tolerance used in default determination of convergence, i.e.
                if residual norm decreases by this factor than convergence is declared
.   -ksp_atol atol - absolute tolerance used in default convergence test, i.e. if residual 
                norm is less than this then convergence is declared
.   -ksp_divtol tol - if residual norm increases by this factor than divergence is declared
.   -ksp_avoid_norms - skip norms used in convergence tests (useful only when not using 
                       convergence test (say you always want to run with 5 iterations) to 
                       save on communication overhead
.   -ksp_cancelmonitors - cancel all previous convergene monitor routines set
.   -ksp_monitor - print residual norm at each iteration
.   -ksp_xmonitor - plot residual norm at each iteration
.   -ksp_vecmonitor - plot solution at each iteration
-   -ksp_singmonitor - monitor extremem singular values at each iteration

   Notes:  
   To see all options, run your program with the -help option
   or consult the users manual.

   Level: developer

.keywords: KSP, set, from, options, database

.seealso: 
@*/
int KSPSetFromOptions(KSP ksp)
{
  int        ierr;
  char       type[256];
  PetscTruth flg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(ksp,KSP_COOKIE);
  ierr = OptionsBegin(ksp->comm,ksp->prefix,"Krylov Method (KSP) Options");CHKERRQ(ierr);

    if (!KSPRegisterAllCalled) {ierr = KSPRegisterAll(PETSC_NULL);CHKERRQ(ierr);}
    ierr = OptionsList("-ksp_type","Krylov method","KSPSetType",KSPList,ksp->type_name?ksp->type_name:KSPGMRES,type,256,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = KSPSetType(ksp,type);CHKERRQ(ierr);
    }
    /*
      Set the type if it was never set.
    */
    if (!ksp->type_name) {
      ierr = KSPSetType(ksp,KSPGMRES);CHKERRQ(ierr);
    }

    ierr = OptionsInt("-ksp_max_it","Maximum number of iterations","KSPSetTolerances",ksp->max_it,&ksp->max_it,PETSC_NULL);CHKERRQ(ierr);
    ierr = OptionsDouble("-ksp_rtol","Relative decrease in residual norm","KSPSetTolerances",ksp->rtol,&ksp->rtol,PETSC_NULL);CHKERRQ(ierr);
    ierr = OptionsDouble("-ksp_atol","Absolute value of residual norm","KSPSetTolerances",ksp->atol,&ksp->atol,PETSC_NULL);CHKERRQ(ierr);
    ierr = OptionsDouble("-ksp_divtol","Residual norm increase cause divergence","KSPSetTolerances",ksp->divtol,&ksp->divtol,PETSC_NULL);CHKERRQ(ierr);

    ierr = OptionsName("-ksp_avoid_norms","Do not compute norms for convergence tests","KSPSetAvoidNorms",&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = KSPSetAvoidNorms(ksp);CHKERRQ(ierr);
    }

    ierr = OptionsName("-ksp_cancelmonitors","Remove any hardwired monitor routines","KSPClearMonitor",&flg);CHKERRQ(ierr);
    /* -----------------------------------------------------------------------*/
    /*
      Cancels all monitors hardwired into code before call to KSPSetFromOptions()
    */
    if (flg) {
      ierr = KSPClearMonitor(ksp);CHKERRQ(ierr);
    }
    /*
      Prints preconditioned residual norm at each iteration
    */
    ierr = OptionsName("-ksp_monitor","Monitor preconditioned residual norm","KSPSetMonitor",&flg);CHKERRQ(ierr); 
    if (flg) {
      ierr = KSPSetMonitor(ksp,KSPDefaultMonitor,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }
    /*
      Plots the vector solution 
    */
    ierr = OptionsName("-ksp_vecmonitor","Monitor solution graphically","KSPSetMonitor",&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = KSPSetMonitor(ksp,KSPVecViewMonitor,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }
    /*
      Prints preconditioned and true residual norm at each iteration
    */
    ierr = OptionsName("-ksp_truemonitor","Monitor true (unpreconditioned) residual norm","KSPSetMonitor",&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = KSPSetMonitor(ksp,KSPTrueMonitor,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }
    /*
      Prints extreme eigenvalue estimates at each iteration
    */
    ierr = OptionsName("-ksp_singmonitor","Monitor singular values","KSPSetMonitor",&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = KSPSetComputeSingularValues(ksp);CHKERRQ(ierr);
      ierr = KSPSetMonitor(ksp,KSPSingularValueMonitor,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr); 
    }
    /*
      Prints preconditioned residual norm with fewer digits
    */
    ierr = OptionsName("-ksp_smonitor","Monitor preconditioned residual norm with fewer digitis","KSPSetMonitor",&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = KSPSetMonitor(ksp,KSPDefaultSMonitor,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }
    /*
      Graphically plots preconditioned residual norm
    */
    ierr = OptionsName("-ksp_xmonitor","Monitor graphically preconditioned residual norm","KSPSetMonitor",&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = KSPSetMonitor(ksp,KSPLGMonitor,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }
    /*
      Graphically plots preconditioned and true residual norm
    */
    ierr = OptionsName("-ksp_xtruemonitor","Monitor graphically true residual norm","KSPSetMonitor",&flg);CHKERRQ(ierr);
    if (flg){
      ierr = KSPSetMonitor(ksp,KSPLGTrueMonitor,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }

    /* -----------------------------------------------------------------------*/
    ierr = OptionsName("-ksp_preres","Use preconditioner residual norm in convergence tests","KSPSetUsePreconditionedResidual",&flg);CHKERRQ(ierr);
    if (flg) { ierr = KSPSetUsePreconditionedResidual(ksp);CHKERRQ(ierr);}

    ierr = OptionsLogicalGroupBegin("-ksp_left_pc","Use left preconditioning","KSPSetPreconditionerSide",&flg);CHKERRQ(ierr);
    if (flg) { ierr = KSPSetPreconditionerSide(ksp,PC_LEFT);CHKERRQ(ierr); }
    ierr = OptionsLogicalGroup("-ksp_right_pc","Use right preconditioning","KSPSetPreconditionerSide",&flg);CHKERRQ(ierr);
    if (flg) { ierr = KSPSetPreconditionerSide(ksp,PC_RIGHT);CHKERRQ(ierr);}
    ierr = OptionsLogicalGroupEnd("-ksp_symmetric_pc","Use symmetric (factorized) preconditioning","KSPSetPreconditionerSide",&flg);CHKERRQ(ierr);
    if (flg) { ierr = KSPSetPreconditionerSide(ksp,PC_SYMMETRIC);CHKERRQ(ierr);}

    ierr = OptionsName("-ksp_compute_singularvalues","Compute singular values of preconditioned operator","KSPSetComputeSingularValues",&flg);CHKERRQ(ierr);
    if (flg) { ierr = KSPSetComputeSingularValues(ksp);CHKERRQ(ierr); }
    ierr = OptionsName("-ksp_compute_eigenvalues","Compute eigenvalues of preconditioned operator","KSPSetComputeSingularValues",&flg);CHKERRQ(ierr);
    if (flg) { ierr = KSPSetComputeSingularValues(ksp);CHKERRQ(ierr); }
    ierr = OptionsName("-ksp_plot_eigenvalues","Scatter plot extreme eigenvalues","KSPSetComputeSingularValues",&flg);CHKERRQ(ierr);
    if (flg) { ierr = KSPSetComputeSingularValues(ksp);CHKERRQ(ierr); }

  ierr = OptionsEnd();CHKERRQ(ierr);

  if (ksp->ops->setfromoptions) {
    ierr = (*ksp->ops->setfromoptions)(ksp);CHKERRQ(ierr);
  }

  PetscFunctionReturn(0);
}

/*MC
   KSPRegisterDynamic - Adds a method to the Krylov subspace solver package.

   Synopsis:
   KSPRegisterDynamic(char *name_solver,char *path,char *name_create,int (*routine_create)(KSP))

   Not Collective

   Input Parameters:
+  name_solver - name of a new user-defined solver
.  path - path (either absolute or relative) the library containing this solver
.  name_create - name of routine to create method context
-  routine_create - routine to create method context

   Notes:
   KSPRegisterDynamic() may be called multiple times to add several user-defined solvers.

   If dynamic libraries are used, then the fourth input argument (routine_create)
   is ignored.

   Sample usage:
.vb
   KSPRegisterDynamic("my_solver",/home/username/my_lib/lib/libO/solaris/mylib.a,
               "MySolverCreate",MySolverCreate);
.ve

   Then, your solver can be chosen with the procedural interface via
$     KSPSetType(ksp,"my_solver")
   or at runtime via the option
$     -ksp_type my_solver

   Level: advanced

   Environmental variables such as ${PETSC_ARCH}, ${PETSC_DIR}, ${PETSC_LDIR}, ${BOPT},
   and others of the form ${any_environmental_variable} occuring in pathname will be 
   replaced with appropriate values.

.keywords: KSP, register

.seealso: KSPRegisterAll(), KSPRegisterDestroy()

M*/

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"KSPRegister"
int KSPRegister(char *sname,char *path,char *name,int (*function)(KSP))
{
  int  ierr;
  char fullname[256];

  PetscFunctionBegin;
  ierr = FListConcat(path,name,fullname);CHKERRQ(ierr);
  ierr = FListAdd(&KSPList,sname,fullname,(int (*)(void*))function);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
