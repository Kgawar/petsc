/*
      This file deals with the BLAS/LAPACK naming convention on
      non-Microsoft systems, which may append an underscore, use
      upper or lower case, and/or use a configurable symbol suffix.
*/
#if !defined(_BLASLAPACK_MANGLE_H)
#define _BLASLAPACK_MANGLE_H

/****************************************************************************/
/* macros to mangle BLAS/LAPACK names as needed for linking */

/* token pasting, with an extra level of indirection so that we
   can paste the contents of other preprocessor #definitions */
#define PETSC_PASTE2_(a,b) a ## b
#define PETSC_PASTE2(a,b) PETSC_PASTE2_(a,b)
#define PETSC_PASTE3_(a,b,c) a ## b ## c
#define PETSC_PASTE3(a,b,c) PETSC_PASTE3_(a,b,c)

#if !defined(PETSC_BLASLAPACK_SUFFIX)
# if defined(PETSC_BLASLAPACK_UNDERSCORE)
#  define PETSC_BLASLAPACK_SUFFIX_ _
# else
#  define PETSC_BLASLAPACK_SUFFIX_
# endif
#else
# if defined(PETSC_BLASLAPACK_UNDERSCORE)
#  define PETSC_BLASLAPACK_SUFFIX_ PETSC_PASTE2(PETSC_BLASLAPACK_SUFFIX,_)
# else
#  define PETSC_BLASLAPACK_SUFFIX_ PETSC_BLASLAPACK_SUFFIX
# endif
#endif

/* complex/real and single/double/quad precision prefixes: */
#if !defined(PETSC_USE_COMPLEX)
# if defined(PETSC_BLASLAPACK_CAPS)
#  if defined(PETSC_USE_REAL_SINGLE)
#   define PETSC_BLASLAPACK_PREFIX_ S
#  elif defined(PETSC_USE_REAL_DOUBLE)
#   define PETSC_BLASLAPACK_PREFIX_ D
#  else
#   define PETSC_BLASLAPACK_PREFIX_ Q
#  endif
# else
#  if defined(PETSC_USE_REAL_SINGLE)
#   define PETSC_BLASLAPACK_PREFIX_ s
#  elif defined(PETSC_USE_REAL_DOUBLE)
#   define PETSC_BLASLAPACK_PREFIX_ d
#  else
#   define PETSC_BLASLAPACK_PREFIX_ q
#  endif
# endif
# define PETSC_BLASLAPACK_RPREFIX_ PETSC_BLASLAPACK_PREFIX_
#else
# if defined(PETSC_BLASLAPACK_CAPS)
#  if defined(PETSC_USE_REAL_SINGLE)
#   define PETSC_BLASLAPACK_PREFIX_ C
#   define PETSC_BLASLAPACK_RPREFIX_ SC
#  elif defined(PETSC_USE_REAL_DOUBLE)
#   define PETSC_BLASLAPACK_PREFIX_ Z
#   define PETSC_BLASLAPACK_RPREFIX_ DZ
#  else
#   define PETSC_BLASLAPACK_PREFIX_ W
#   define PETSC_BLASLAPACK_RPREFIX_ QW
#  endif
# else
#  if defined(PETSC_USE_REAL_SINGLE)
#   define PETSC_BLASLAPACK_PREFIX_ c
#   define PETSC_BLASLAPACK_RPREFIX_ sc
#  elif defined(PETSC_USE_REAL_DOUBLE)
#   define PETSC_BLASLAPACK_PREFIX_ z
#   define PETSC_BLASLAPACK_RPREFIX_ dz
#  else
#   define PETSC_BLASLAPACK_PREFIX_ w
#   define PETSC_BLASLAPACK_RPREFIX_ qw
#  endif
# endif
#endif

/* define macros PETSCBLAS to mangle BLAS/LAPACK subroutine names, and
   PETSCBLASR for functions returning real values */
#if defined(PETSC_BLASLAPACK_CAPS)
#  define PETSCBLAS(x,X) PETSC_PASTE3(PETSC_BLASLAPACK_PREFIX_, X, PETSC_BLASLAPACK_SUFFIX_)
#  define PETSCBLASR(x,X) PETSC_PASTE3(PETSC_BLASLAPACK_RPREFIX_, X, PETSC_BLASLAPACK_SUFFIX_)
#else
#  define PETSCBLAS(x,X) PETSC_PASTE3(PETSC_BLASLAPACK_PREFIX_, x, PETSC_BLASLAPACK_SUFFIX_)
#  define PETSCBLASR(x,X) PETSC_PASTE3(PETSC_BLASLAPACK_RPREFIX_, x, PETSC_BLASLAPACK_SUFFIX_)
#endif

/****************************************************************************/
/* definitions of BLAS and LAPACK symbols */

/* Subroutine names that are the same for real/complex data: */
/* no character-string arguments: */
#define LAPACKgeqrf_ PETSCBLAS(geqrf,GEQRF)
#define LAPACKgetrf_ PETSCBLAS(getrf,GETRF)
#define LAPACKgetri_ PETSCBLAS(getri,GETRI)
#define BLASnrm2_    PETSCBLASR(nrm2,NRM2)
#define BLASscal_    PETSCBLAS(scal,SCAL)
#define BLAScopy_    PETSCBLAS(copy,COPY)
#define BLASswap_    PETSCBLAS(swap,SWAP)
#define BLASaxpy_    PETSCBLAS(axpy,AXPY)
#define BLASasum_    PETSCBLASR(asum,ASUM)
#define LAPACKpttrf_ PETSCBLAS(pttrf,PTTRF) /* factorization of a spd tridiagonal matrix */
#define LAPACKpttrs_ PETSCBLAS(pttrs,PTTRS) /* solve a spd tridiagonal matrix system */
#define LAPACKstein_ PETSCBLAS(stein,STEIN) /* eigenvectors of real symm tridiagonal matrix */
#define LAPACKgesv_  PETSCBLAS(gesv,GESV)
#define LAPACKgelss_ PETSCBLAS(gelss,GELSS)
#define LAPACKgerfs_ PETSCBLAS(gerfs,GERFS)
#define LAPACKtgsen_ PETSCBLAS(tgsen,TGSEN)
/* character-string arguments: */
#define LAPACKpotrf_ PETSCBLAS(potrf,POTRF)
#define LAPACKpotri_ PETSCBLAS(potri,POTRI)
#define LAPACKpotrs_ PETSCBLAS(potrs,POTRS)
#define LAPACKsytrf_ PETSCBLAS(sytrf,SYTRF)
#define LAPACKsytrs_ PETSCBLAS(sytrs,SYTRS)
#define LAPACKsytri_ PETSCBLAS(sytri,SYTRI)
#define BLASgemv_    PETSCBLAS(gemv,GEMV)
#define LAPACKgetrs_ PETSCBLAS(getrs,GETRS)
#define BLAStrmv_    PETSCBLAS(trmv,TRMV)
#define BLASgemm_    PETSCBLAS(gemm,GEMM)
#define BLASsymm_    PETSCBLAS(symm,SYMM)
#define BLAStrsm_    PETSCBLAS(trsm,TRSM)
#define LAPACKgesvd_ PETSCBLAS(gesvd,GESVD)
#define LAPACKgeev_  PETSCBLAS(geev,GEEV)
#define LAPACKsteqr_ PETSCBLAS(steqr,STEQR)  /* eigenvalues and eigenvectors of symm tridiagonal */
#define LAPACKhseqr_ PETSCBLAS(hseqr,HSEQR)
#define LAPACKgges_  PETSCBLAS(gges,GGES)
#define LAPACKtrsen_ PETSCBLAS(trsen,TRSEN)
#define LAPACKormqr_ PETSCBLAS(ormqr,ORMQR)
#define LAPACKhgeqz_ PETSCBLAS(hgeqz,HGEQZ)
#define LAPACKtrtrs_ PETSCBLAS(trtrs,TRTRS)

/* Subroutine names that differ for real/complex data: */
#if !defined(PETSC_USE_COMPLEX)
# define LAPACKungqr_ PETSCBLAS(orgqr,ORGQR)
# define BLASdot_     PETSCBLAS(dot,DOT)
# define BLASdotu_    PETSCBLAS(dot,DOT)

# define LAPACKsyev_  PETSCBLAS(syev,SYEV)  /* eigenvalues and eigenvectors of a symm matrix */
# define LAPACKsyevx_ PETSCBLAS(syevx,SYEVX) /* selected eigenvalues and eigenvectors of a symm matrix */
# define LAPACKsygv_  PETSCBLAS(sygv,SYGV)
# define LAPACKsygvx_ PETSCBLAS(sygvx,SYGVX)

  /* stebz does not exist for complex data */
# define LAPACKstebz_ PETSCBLAS(stebz,STEBZ) /* eigenvalues of symm tridiagonal matrix */
#else
# define LAPACKungqr_ PETSCBLAS(ungqr,UNGQR)
   /* note: dot and dotu are handled separately for complex data */

# define LAPACKsyev_  PETSCBLAS(heev,HEEV)  /* eigenvalues and eigenvectors of a symm matrix */
# define LAPACKsyevx_ PETSCBLAS(heevx,HEEVX) /* selected eigenvalues and eigenvectors of a symm matrix */
# define LAPACKsygv_  PETSCBLAS(hegv,HEGV)
# define LAPACKsygvx_ PETSCBLAS(hegvx,HEGVX)
#endif

#endif
