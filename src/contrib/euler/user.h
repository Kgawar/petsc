/* Include file for app-defined SNES/SLES routines */

#if !defined(__APPCTX)
#define __APPCTX

#include "snes.h"
#include "da.h"
#include "dfvec.h"
#include "draw.h"
#include <math.h>

/*
   IMPLICIT ==> fully implicit treatment of boundary conditions
   EXPLICIT ==> explicit treatment of boundary conditions
*/
typedef enum {EXPLICIT=0, IMPLICIT_SIZE=1, IMPLICIT=2} BCType;

typedef enum {CONSTANT=0, ADVANCE_LOCAL=1, ADVANCE_GLOBAL=2} CFLAdvanceType;

typedef enum {DT_MULT=0, DT_DIV=1} ScaleType;

/* Application data structure for 3D Euler code */
typedef struct {
  /* ------------------- Fortran work arrays ------------------- */
    Scalar *dr, *dru, *drv, *drw, *de, *dt;  /* residual */
    Scalar *r, *ru, *rv, *rw, *e;            /* solution */
    Scalar *p;                               /* pressure */
    Scalar *r_bc, *ru_bc, *rv_bc;            /* boundary conditions */
    Scalar *rw_bc, *e_bc, *p_bc;
    Scalar *br, *bl, *be;                    /* eigen[vectors,values] */
    Scalar *sadai, *sadaj, *sadak;           /* mesh metrics */
    Scalar *aix, *ajx, *akx;
    Scalar *aiy, *ajy, *aky;
    Scalar *aiz, *ajz, *akz;
    Scalar *b1, *b2, *b3, *b4, *b5, *b6;     /* matrix elements (off-diagonal blocks) */
    Scalar *diag;                            /* diagonal block */
    int    diag_len;                         /* length of work array to store diagonal block */
    Scalar *b1bc, *b2bc, *b3bc, *b2bc_tmp;   /* matrix elements for implicit bcs */
    Scalar *xc, *yc, *zc;                    /* mesh geometry */
    Scalar *work_p;                          /* misc work space for Fortran */
    Scalar *f1, *g1, *h1;
    Scalar *sp, *sm, *sp1, *sm1, *sp2, *sm2;
    Scalar *fbcri1, *fbcrui1, *fbcrvi1, *fbcrwi1, *fbcei1;  /* boundary work arrays */
    Scalar *fbcri2, *fbcrui2, *fbcrvi2, *fbcrwi2, *fbcei2;
    Scalar *fbcrj1, *fbcruj1, *fbcrvj1, *fbcrwj1, *fbcej1;
    Scalar *fbcrj2, *fbcruj2, *fbcrvj2, *fbcrwj2, *fbcej2;
    Scalar *fbcrk1, *fbcruk1, *fbcrvk1, *fbcrwk1, *fbcek1;
    Scalar *fbcrk2, *fbcruk2, *fbcrvk2, *fbcrwk2, *fbcek2;

  /* ----------------- Parameters and flags ------------------- */
    int    ni, nj, nk;           /* sizes of the grid */
    int    ni1, nj1, nk1;	 /* ni-1, nj-1, nk-1 */
    int    nim, njm, nkm;	 /* ni+1, nj+1, nk+1 */
    int    itl, itu, ile, ktip;  /* wing parameters (i:lower, upper, leading edge, k:tip) */
    int    nc;		         /* DOF per node */
    int    nd;		         /* number of diagonals for interior of grid
                                    (are more for C-grid j=0 boundary condition) */
    char   **label;              /* labels for components */
    Scalar   angle;              /* flow parameter - angle of attack */
    BCType bctype;               /* flag - boundary condition type */
    ScaleType sctype;            /* flag - type of scaling */
    int    first_time_resid;     /* flag - first time computing residual */
    int    print_vecs;           /* flag - print vectors */
    int    print_grid;           /* flag - print grid info */
    int    print_debug;          /* flag - print debug info */
    int    dump_general;         /* flag - dump fields for later viewing */
    int    dump_vrml;            /* flag - dump fields directly into VRML format */
    int    dump_freq;            /* flag - dump fields every X iterations */
    int    dump_vrml_pressure;   /* flag - dump pressure field directly into VRML format */
    int    dump_vrml_residual;   /* flag - dump residual directly into VRML format */
    Vec    Fvrml;                /* work vector form dumping residual to VRML */
    int    bc_test;              /* flag - test boundary condition scatters */
    int    matrix_free;          /* flag - using matrix-free method */
    int    matrix_free_mult;     /* flag - doing matrix-free mult */
    int    mat_assemble_direct;  /* flag - indicates assembling matrix directly */
    int    mf_adaptive;          /* flag - are we doing adaptive CFL advancement? */
    int    no_output;            /* flag - indicates no runtime output (use when timing code) */
    int    iter;                 /* nonlinear iteration number */
    int    jfreq;		 /* frequency of computing Jacobian */
    Scalar jratio;		 /* ratio indicating when to form a new preconditioning Jacobian */
    int    use_jratio;           /* flag - are we using the jratio test? */
    Scalar fnorm_last_jac;       /* || F || - last iteration Jac precond was formed */
    int    iter_last_jac;        /* iteration at which last Jac precond was formed */

  /* ----------------- Basic data structures ------------------- */
    DA       da;                /* distributed array for X, F */ 
    DA       da1;               /* distributed array for pressure */ 
    Vec      X, Xbc, F;         /* global solution, residual vectors */
    Vec      P, Pbc, localP;    /* pressure work vectors */
    Vec      localX, localDX;   /* local solution, dx vectors */
    Mat      J;                 /* Jacobian (preconditioner) matrix */
    Mat      Jmf;               /* matrix-free Jacobian context */
    KSP      ksp;               /* Krylov context */

  /* ----------------- Parallel information ------------------- */
    MPI_Comm comm;              /* general communicator */
    VecScatter Xbcscatter;      /* scatter context for vector BCs */
    VecScatter Pbcscatter;      /* scatter context for pressure BCs */
    int      rank;              /* my processor number */
    int      size;              /* number of processors */
    int      ldim;              /* local dimension */
    int      lbkdim;            /* block local dimension */
    int      gdim;              /* global dimension */
    int      Nx, Ny, Nz;        /* number of procs in x-, y-, and z-directions */
    int      mx, my, mz;        /* global vector dimensions */
    int      nloc;              /* number of ghosted local grid points */
    int      *ltog;             /* local-to-global mapping */

  /* ----------------- Local grid information ------------------- */
    int      xs, ys, zs, xe, ye, ze;        /* local starting/ending grid points */
    int      xsi, ysi, zsi, xei, yei, zei;  /* local starting/ending grid points (interior) */
    int      gxs, gys, gzs, gxe, gye, gze;  /* local starting/ending ghost points */
    int      gxsi, gysi, gzsi, gxei, gyei, gzei; /* local starting/ending ghost points (interior) */
    int      xm, ym, zm, gxm, gym, gzm;     /* local grid/ghost widths */
    int      xsf, ysf, zsf;                 /* Fortran starting grid points */
    int      xefm1, yefm1, zefm1;           /* Fortran ending grid points */
    int      gxsf, gysf, gzsf;              /* Fortran starting ghost points */
    int      xef, yef, zef;                 /* Fortran ending points */
    int      xef01, yef01, zef01;           /* Fortran ending points ni,nj,nk */
    int      gxef, gyef, gzef;              /* ending ghost points */
    int      gxef01, gyef01, gzef01;        /* ending ghost points ni,nj,nk */
    int      xefp1, yefp1, zefp1;           /* Fortran ending points ni1,nj1,nk1 */
    int      gxefp1, gyefp1, gzefp1;        /* ending ghost points ni1,nj1,nk1 */
    int      xsf1, ysf1, zsf1;              /* Fortran starting points 1,1,1 */
    int      gxsf1, gysf1, gzsf1;           /* Fortran starting points 1,1,1 */
    int      xsf2, ysf2, zsf2;              /* Fortran starting points 2,2,2 */
    int      gxsf2, gysf2, gzsf2;           /* Fortran starting points 2,2,2 */
    int      gxsfw, gysfw, gzsfw;           /* Fortran starting ghost points + 1 */
    int      gxefw, gyefw, gzefw;           /* Fortran ending ghost points - 1 */
    int      gxmfp1, gymfp1, gzmfp1;        /* Julianne ghost width */
    int      xmfp1, ymfp1, zmfp1;           /* Julianne width */
    int      *is1;                          /* mapping from application to PETSc ordering */
    int      use_vecsetvalues;              /* flag - 1 indicates use of VecSetValues() */
    Scalar   eps_jac;                       /* differencing parameter for FD Jacobian approx */
    Scalar   eps_jac_inv;                   /* 1.0/eps_jac_inv */
    Scalar   eps_mf_default;                /* default differencing parameter for FD 
                                               matrix-vector product approx */
    Scalar   fnorm_init, fnorm_last;        /* || F || - initial and last iterations */
    Scalar   cfl, cfl_init, cfl_max;        /* CFL parameters */
    Scalar   cfl_switch;                    /* CFL at which to dump binary linear system */
    Scalar   cfl_begin_advancement;         /* flag - 1 indicates CFL advancement has begun */
    Scalar   f_reduction;                   /* reduce fnorm by this much before advancing CFL */
    CFLAdvanceType cfl_advance;             /* flag - indicates type of CFL advancement */
    Scalar   fstagnate_ratio;               /* counter for stagnation detection */
    double   time_init;                     /* initial time */
    Scalar   *farray;                       /* array for use with SNESSetConvergenceHistory() */
    Scalar   *favg;                         /* array of average fnorm for the past 10 iterations */

   /* --- output data --- */
    double   *flog, *ftime, *fcfl, *lin_rtol;
    int      *lin_its, last_its;
    FILE     *fp;                           /* file for stashing convergence info at each iteration */
    int      check_solution;
    } Euler;

/* Fortran routine declarations, needed for portablilty */
#ifdef HAVE_FORTRAN_CAPS
#define mdump_       MDUMP
#define cleanup_     CLEANUP
#define eigenv_      EIGENV
#define rscale_      RSCALE
#define resid_       RESID
#define residbc_     RESIDBC
#define bc_uni_      BC_UNI
#define rbuild_      RBUILD
#define rbuild_direct_ RBUILD_DIRECT
#define jsetup_      JSETUP

#define jstep_       JSTEP
#define jfinish_     JFINISH
#define jmonitor_    JMONITOR
#define jform_       JFORM
#define jform2_      JFORM2
#define jformdt_     JFORMDT
#define jformdt2_    JFORMDT2
#define buildmat_    BUILDMAT
#define buildbdmat_  BUILDBDMAT
#define nzmat_       NZMAT
#define printvec_    PRINTVEC
#define copyvec_     COPYVEC
#define setvec_      SETVEC
#define scalenorm_   SCALENORM
#define pvar_        PVAR
#define julianne_    JULIANNE
#define printjul_    PRINTJUL
#define printgjul_   PRINTGJUL
#define printbjul_   PRINTBJUL
#define parsetup_    PARSETUP
#define jpressure_   JPRESSURE
#define bc_          BC
#define bcpart_j1_   BCPART_J1
#define readmesh_    READMESH
#define jcfl_update_ JCFL_UPDATE

#elif !defined(HAVE_FORTRAN_UNDERSCORE)
#define mdump_       mdump
#define cleanup_     cleanup
#define eigenv_      eigenv
#define rscale_      rscale
#define resid_       resid
#define bc_uni_      bc_uni
#define residbc_     residbc
#define rbuild_      rbuild
#define rbuild_direct_ rbuild_direct
#define jsetup_      jsetup
#define jstep_       jstep
#define jfinish_     jfinish
#define jmonitor_    jmonitor
#define jform_       jform
#define jform2_      jform2
#define jformdt_     jformdt
#define jformdt2_    jformdt2
#define buildmat_    buildmat
#define buildbdmat_  buildbdmat
#define nzmat_       nzmat
#define printvec_    printvec
#define copyvec_     copyvec
#define setvec_      setvec
#define scalenorm_   scalenorm
#define pvar_        pvar
#define julianne_    julianne
#define printjul_    printjul
#define printgjul_   printgjul
#define printbjul_   printbjul
#define parsetup_    parsetup
#define jpressure_   jpressure
#define bc_          bc
#define bcpart_j1_   bcpart_j1
#define readmesh_    readmesh
#define jcfl_update_ jcfl_update
#endif

/* Basic routines */
int UserCreateEuler(MPI_Comm,int,int,Euler**);
int UserDestroyEuler(Euler*);
int InitialGuess(SNES,Euler*,Vec);
int ComputeFunction(SNES,Vec,Vec,void*);
int ComputeJacobian(SNES,Vec,Mat*,Mat*,MatStructure*,void*);
int UserSetJacobian(SNES,Euler*);
int UserMatrixFreeMatCreate(SNES,Euler*,Vec,Mat*);
int UserMatrixFreeMatDestroy(Mat);
int UserSetMatrixFreeParameters(SNES,double,double);
int UserSetGridParameters(Euler*);
int UserSetGrid(Euler*);
int BoundaryConditionsExplicit(Euler*,Vec);
int BoundaryConditionsImplicit(Euler*,Vec);
int BCScatterSetUp(Euler*);

/* Utility routines */
int UnpackWork(Euler*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Vec);
int UnpackWorkComponent(Euler*,Scalar*,Vec);
int PackWork(Euler*,Vec,Vec,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
int PackWorkComponent(Euler*,Vec,Vec,Scalar*);
int MatViewDFVec_MPIAIJ(Mat,DFVec,Viewer);
int GridTest(Euler*);

/* Monitoring routines */
int CheckSolution(Euler*,Vec);
int MonitorEuler(SNES,int,double,void*);
int ConvergenceTestEuler(SNES,double,double,double,void*);
int TECPLOTMonitor(SNES,Vec,Euler*);
int MonitorDumpGeneral(SNES,Vec,Euler*);
int MonitorDumpGeneralJulianne(Euler*);
int MonitorDumpVRML(SNES,Vec,Vec,Euler*);
int ComputeNodalResiduals(Euler*,Vec,Vec);
int DumpField(Euler*,Draw,Scalar*);
void dump_angle_vrml(float);
void MonitorDumpIter(int);

/* Fortran routines */
extern void cleanup_();
extern int readmesh_(int*,int*,int*,int*,Scalar*,Scalar*,Scalar*);
extern int mdump_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int printvec_(double*,int*,FILE*);
extern int printjul_(double*,double*,double*,double*,double*,double*,int*);
extern int printgjul_(double*,double*,double*,double*,double*,double*,int*);
extern int printbjul_(double*,double*,double*,double*,double*,double*,int*);
extern int julianne_(Scalar*,int*,int*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,int*);
extern int parsetup_(int*,int*,int*,BCType*,int*,int*,int*,int*,int*,int*,int*,
                      int*,int*,int*,int*,int*,int*,int*,int*,int*,int*,int*,
                      int*,int*,int*,int*,int*,int*,int*,int*,int*,int*,int*,
                      int*,int*,int*,int*,int*,int*,int*,int*,int*,int*,int*,
                      int*,int*,int*,int*,int*,int*,int*,int*,int*,int*,int*);
extern int resid_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*);
extern int jformdt_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
		      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int jformdt2_(Scalar*,Scalar*,int*,int*,int*,int*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,int*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int jform_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int jform2_(Scalar*,Scalar*,int*,int*,int*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*);
extern int jmonitor_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int buildmat_(int*,ScaleType*,int*,int*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,int*,int*,
                      Scalar*,Scalar*,Scalar*,Scalar*);
extern int buildbdmat_(int*,ScaleType*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,int*,int*);
extern int nzmat_(MatType*,int*,int*,int*,int*,int*,int*,int*,int*,int*,int*);
extern int jpressure_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int bc_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*);
extern int bcpart_j1_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern void eigenv_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*);
extern int  residbc_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int  pvar_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int rbuild_(int*,ScaleType*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,int*,int*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int rbuild_direct_(Scalar*,ScaleType*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,
                      Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);
extern int rscale_(Scalar*,Scalar*,Scalar*,Scalar*,Scalar*,Scalar*);

/* Fortran interface definitions */

#ifdef HAVE_64BITS
extern void *MPIR_ToPointer();
extern int MPIR_FromPointer();
extern void MPIR_RmPointer();
#else
#define MPIR_ToPointer(a) (a)
#define MPIR_FromPointer(a) (int)(a)
#define MPIR_RmPointer(a)
#endif

#endif
