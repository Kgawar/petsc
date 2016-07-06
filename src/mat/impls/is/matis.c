
/*
    Creates a matrix class for using the Neumann-Neumann type preconditioners.
    This stores the matrices in globally unassembled form. Each processor
    assembles only its local Neumann problem and the parallel matrix vector
    product is handled "implicitly".

    Currently this allows for only one subdomain per processor.
*/

#include <../src/mat/impls/is/matis.h>      /*I "petscmat.h" I*/

static PetscErrorCode MatISComputeSF_Private(Mat);
static PetscErrorCode MatISZeroRowsLocal_Private(Mat,PetscInt,const PetscInt[],PetscScalar);

#undef __FUNCT__
#define __FUNCT__ "PetscLayoutMapLocal_Private"
static PetscErrorCode PetscLayoutMapLocal_Private(PetscLayout map,PetscInt N,const PetscInt idxs[],const PetscInt gidxs[], PetscInt *on,PetscInt **oidxs,PetscInt **ogidxs)
{
  PetscInt      *owners = map->range;
  PetscInt       n      = map->n;
  PetscSF        sf;
  PetscInt      *lidxs,*work = NULL;
  PetscSFNode   *ridxs;
  PetscMPIInt    rank;
  PetscInt       r, p = 0, len = 0;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* Create SF where leaves are input idxs and roots are owned idxs (code adapted from MatZeroRowsMapLocal_Private) */
  ierr = MPI_Comm_rank(map->comm,&rank);CHKERRQ(ierr);
  ierr = PetscMalloc1(n,&lidxs);CHKERRQ(ierr);
  for (r = 0; r < n; ++r) lidxs[r] = -1;
  ierr = PetscMalloc1(N,&ridxs);CHKERRQ(ierr);
  for (r = 0; r < N; ++r) {
    const PetscInt idx = idxs[r];
    if (idx < 0 || map->N <= idx) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"Index %D out of range [0,%D)",idx,map->N);
    if (idx < owners[p] || owners[p+1] <= idx) { /* short-circuit the search if the last p owns this idx too */
      ierr = PetscLayoutFindOwner(map,idx,&p);CHKERRQ(ierr);
    }
    ridxs[r].rank = p;
    ridxs[r].index = idxs[r] - owners[p];
  }
  ierr = PetscSFCreate(map->comm,&sf);CHKERRQ(ierr);
  ierr = PetscSFSetGraph(sf,n,N,NULL,PETSC_OWN_POINTER,ridxs,PETSC_OWN_POINTER);CHKERRQ(ierr);
  ierr = PetscSFReduceBegin(sf,MPIU_INT,(PetscInt*)idxs,lidxs,MPI_LOR);CHKERRQ(ierr);
  ierr = PetscSFReduceEnd(sf,MPIU_INT,(PetscInt*)idxs,lidxs,MPI_LOR);CHKERRQ(ierr);
  if ((gidxs && ogidxs) || ogidxs) {
    ierr = PetscMalloc1(n,&work);CHKERRQ(ierr);
    if (!gidxs) { /* if gidxs is not provided, global indices are inferred */
      PetscInt cum = 0,start,*work2;
      ierr = PetscCalloc1(N,&work2);CHKERRQ(ierr);
      for (r = 0; r < N; ++r) if (idxs[r] >=0) cum++;
      ierr = MPI_Scan(&cum,&start,1,MPIU_INT,MPI_SUM,map->comm);CHKERRQ(ierr);
      start -= cum;
      cum = 0;
      for (r = 0; r < N; ++r) if (idxs[r] >=0) work2[r] = start+cum++;
      ierr = PetscSFReduceBegin(sf,MPIU_INT,work2,work,MPIU_REPLACE);CHKERRQ(ierr);
      ierr = PetscSFReduceEnd(sf,MPIU_INT,work2,work,MPIU_REPLACE);CHKERRQ(ierr);
      ierr = PetscFree(work2);CHKERRQ(ierr);
    } else {
      ierr = PetscSFReduceBegin(sf,MPIU_INT,(PetscInt*)gidxs,work,MPIU_REPLACE);CHKERRQ(ierr);
      ierr = PetscSFReduceEnd(sf,MPIU_INT,(PetscInt*)gidxs,work,MPIU_REPLACE);CHKERRQ(ierr);
    }
  }
  ierr = PetscSFDestroy(&sf);CHKERRQ(ierr);
  /* Compress and put in indices */
  for (r = 0; r < n; ++r)
    if (lidxs[r] >= 0) {
      if (work) work[len] = work[r];
      lidxs[len++] = r;
    }
  if (on) *on = len;
  if (oidxs) *oidxs = lidxs;
  if (ogidxs) *ogidxs = work;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatGetSubMatrix_IS"
static PetscErrorCode MatGetSubMatrix_IS(Mat mat,IS irow,IS icol,MatReuse scall,Mat *newmat)
{
  Mat               locmat,newlocmat;
  Mat_IS            *newmatis;
#if defined(PETSC_USE_DEBUG)
  Vec               rtest,ltest;
  const PetscScalar *array;
#endif
  const PetscInt    *idxs;
  PetscInt          i,m,n;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  if (scall == MAT_REUSE_MATRIX) {
    PetscBool ismatis;

    ierr = PetscObjectTypeCompare((PetscObject)*newmat,MATIS,&ismatis);CHKERRQ(ierr);
    if (!ismatis) SETERRQ(PetscObjectComm((PetscObject)*newmat),PETSC_ERR_ARG_WRONG,"Cannot reuse matrix! Not of MATIS type");
    newmatis = (Mat_IS*)(*newmat)->data;
    if (!newmatis->getsub_ris) SETERRQ(PetscObjectComm((PetscObject)*newmat),PETSC_ERR_ARG_WRONG,"Cannot reuse matrix! Misses local row IS");
    if (!newmatis->getsub_cis) SETERRQ(PetscObjectComm((PetscObject)*newmat),PETSC_ERR_ARG_WRONG,"Cannot reuse matrix! Misses local col IS");
  }
  /* irow and icol may not have duplicate entries */
#if defined(PETSC_USE_DEBUG)
  ierr = MatCreateVecs(mat,&ltest,&rtest);CHKERRQ(ierr);
  ierr = ISGetLocalSize(irow,&n);CHKERRQ(ierr);
  ierr = ISGetIndices(irow,&idxs);CHKERRQ(ierr);
  for (i=0;i<n;i++) {
    ierr = VecSetValue(rtest,idxs[i],1.0,ADD_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(rtest);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(rtest);CHKERRQ(ierr);
  ierr = VecGetLocalSize(rtest,&n);CHKERRQ(ierr);
  ierr = VecGetOwnershipRange(rtest,&m,NULL);CHKERRQ(ierr);
  ierr = VecGetArrayRead(rtest,&array);CHKERRQ(ierr);
  for (i=0;i<n;i++) if (array[i] != 0. && array[i] != 1.) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"Index %D counted %D times! Irow may not have duplicate entries",i+m,(PetscInt)array[i]);
  ierr = VecRestoreArrayRead(rtest,&array);CHKERRQ(ierr);
  ierr = ISRestoreIndices(irow,&idxs);CHKERRQ(ierr);
  ierr = ISGetLocalSize(icol,&n);CHKERRQ(ierr);
  ierr = ISGetIndices(icol,&idxs);CHKERRQ(ierr);
  for (i=0;i<n;i++) {
    ierr = VecSetValue(ltest,idxs[i],1.0,ADD_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(ltest);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(ltest);CHKERRQ(ierr);
  ierr = VecGetLocalSize(ltest,&n);CHKERRQ(ierr);
  ierr = VecGetOwnershipRange(ltest,&m,NULL);CHKERRQ(ierr);
  ierr = VecGetArrayRead(ltest,&array);CHKERRQ(ierr);
  for (i=0;i<n;i++) if (array[i] != 0. && array[i] != 1.) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"Index %D counted %D times! Icol may not have duplicate entries",i+m,(PetscInt)array[i]);
  ierr = VecRestoreArrayRead(ltest,&array);CHKERRQ(ierr);
  ierr = ISRestoreIndices(icol,&idxs);CHKERRQ(ierr);
  ierr = VecDestroy(&rtest);CHKERRQ(ierr);
  ierr = VecDestroy(&ltest);CHKERRQ(ierr);
#endif
  if (scall == MAT_INITIAL_MATRIX) {
    Mat_IS                 *matis = (Mat_IS*)mat->data;
    ISLocalToGlobalMapping rl2g;
    IS                     is;
    PetscInt               *lidxs,*lgidxs,*newgidxs;
    PetscInt               i,m,n,ll,newloc;
    MPI_Comm               comm;

    ierr = PetscObjectGetComm((PetscObject)mat,&comm);CHKERRQ(ierr);
    ierr = ISGetLocalSize(irow,&m);CHKERRQ(ierr);
    ierr = ISGetLocalSize(icol,&n);CHKERRQ(ierr);
    ierr = MatCreate(comm,newmat);CHKERRQ(ierr);
    ierr = MatSetType(*newmat,MATIS);CHKERRQ(ierr);
    ierr = MatSetSizes(*newmat,m,n,PETSC_DECIDE,PETSC_DECIDE);CHKERRQ(ierr);
    /* communicate irow to their owners in the layout */
    ierr = ISGetIndices(irow,&idxs);CHKERRQ(ierr);
    ierr = PetscLayoutMapLocal_Private(mat->rmap,m,idxs,NULL,&ll,&lidxs,&lgidxs);CHKERRQ(ierr);
    ierr = ISRestoreIndices(irow,&idxs);CHKERRQ(ierr);
    if (!matis->sf) { /* setup SF if not yet created and allocate rootdata and leafdata */
      ierr = MatISComputeSF_Private(mat);CHKERRQ(ierr);
    }
    ierr = PetscMemzero(matis->sf_rootdata,matis->sf_nroots*sizeof(PetscInt));CHKERRQ(ierr);
    for (i=0;i<ll;i++) matis->sf_rootdata[lidxs[i]] = lgidxs[i]+1;
    ierr = PetscFree(lidxs);CHKERRQ(ierr);
    ierr = PetscFree(lgidxs);CHKERRQ(ierr);
    ierr = PetscSFBcastBegin(matis->sf,MPIU_INT,matis->sf_rootdata,matis->sf_leafdata);CHKERRQ(ierr);
    ierr = PetscSFBcastEnd(matis->sf,MPIU_INT,matis->sf_rootdata,matis->sf_leafdata);CHKERRQ(ierr);
    for (i=0,newloc=0;i<matis->sf_nleaves;i++) if (matis->sf_leafdata[i]) newloc++;
    ierr = PetscMalloc1(newloc,&newgidxs);CHKERRQ(ierr);
    ierr = PetscMalloc1(newloc,&lidxs);CHKERRQ(ierr);
    for (i=0,newloc=0;i<matis->sf_nleaves;i++)
      if (matis->sf_leafdata[i]) {
        lidxs[newloc] = i;
        newgidxs[newloc++] = matis->sf_leafdata[i]-1;
      }
    ierr = ISCreateGeneral(comm,newloc,newgidxs,PETSC_OWN_POINTER,&is);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingCreateIS(is,&rl2g);CHKERRQ(ierr);
    ierr = ISDestroy(&is);CHKERRQ(ierr);
    /* local is to extract local submatrix */
    newmatis = (Mat_IS*)(*newmat)->data;
    ierr = ISCreateGeneral(comm,newloc,lidxs,PETSC_OWN_POINTER,&newmatis->getsub_ris);CHKERRQ(ierr);
    if (mat->congruentlayouts == -1) { /* first time we compare rows and cols layouts */
      PetscBool cong;
      ierr = PetscLayoutCompare(mat->rmap,mat->cmap,&cong);CHKERRQ(ierr);
      if (cong) mat->congruentlayouts = 1;
      else      mat->congruentlayouts = 0;
    }
    if (mat->congruentlayouts && irow == icol) {
      ierr = MatSetLocalToGlobalMapping(*newmat,rl2g,rl2g);CHKERRQ(ierr);
      ierr = PetscObjectReference((PetscObject)newmatis->getsub_ris);CHKERRQ(ierr);
      newmatis->getsub_cis = newmatis->getsub_ris;
    } else {
      ISLocalToGlobalMapping cl2g;

      /* communicate icol to their owners in the layout */
      ierr = ISGetIndices(icol,&idxs);CHKERRQ(ierr);
      ierr = PetscLayoutMapLocal_Private(mat->cmap,n,idxs,NULL,&ll,&lidxs,&lgidxs);CHKERRQ(ierr);
      ierr = ISRestoreIndices(icol,&idxs);CHKERRQ(ierr);
      ierr = PetscMemzero(matis->csf_rootdata,matis->csf_nroots*sizeof(PetscInt));CHKERRQ(ierr);
      for (i=0;i<ll;i++) matis->csf_rootdata[lidxs[i]] = lgidxs[i]+1;
      ierr = PetscFree(lidxs);CHKERRQ(ierr);
      ierr = PetscFree(lgidxs);CHKERRQ(ierr);
      ierr = PetscSFBcastBegin(matis->csf,MPIU_INT,matis->csf_rootdata,matis->csf_leafdata);CHKERRQ(ierr);
      ierr = PetscSFBcastEnd(matis->csf,MPIU_INT,matis->csf_rootdata,matis->csf_leafdata);CHKERRQ(ierr);
      for (i=0,newloc=0;i<matis->csf_nleaves;i++) if (matis->csf_leafdata[i]) newloc++;
      ierr = PetscMalloc1(newloc,&newgidxs);CHKERRQ(ierr);
      ierr = PetscMalloc1(newloc,&lidxs);CHKERRQ(ierr);
      for (i=0,newloc=0;i<matis->csf_nleaves;i++)
        if (matis->csf_leafdata[i]) {
          lidxs[newloc] = i;
          newgidxs[newloc++] = matis->csf_leafdata[i]-1;
        }
      ierr = ISCreateGeneral(comm,newloc,newgidxs,PETSC_OWN_POINTER,&is);CHKERRQ(ierr);
      ierr = ISLocalToGlobalMappingCreateIS(is,&cl2g);CHKERRQ(ierr);
      ierr = ISDestroy(&is);CHKERRQ(ierr);
      /* local is to extract local submatrix */
      ierr = ISCreateGeneral(comm,newloc,lidxs,PETSC_OWN_POINTER,&newmatis->getsub_cis);CHKERRQ(ierr);
      ierr = MatSetLocalToGlobalMapping(*newmat,rl2g,cl2g);CHKERRQ(ierr);
      ierr = ISLocalToGlobalMappingDestroy(&cl2g);CHKERRQ(ierr);
    }
    ierr = ISLocalToGlobalMappingDestroy(&rl2g);CHKERRQ(ierr);
  } else {
    ierr = MatISGetLocalMat(*newmat,&newlocmat);CHKERRQ(ierr);
  }
  ierr = MatISGetLocalMat(mat,&locmat);CHKERRQ(ierr);
  newmatis = (Mat_IS*)(*newmat)->data;
  ierr = MatGetSubMatrix(locmat,newmatis->getsub_ris,newmatis->getsub_cis,scall,&newlocmat);CHKERRQ(ierr);
  if (scall == MAT_INITIAL_MATRIX) {
    ierr = MatISSetLocalMat(*newmat,newlocmat);CHKERRQ(ierr);
    ierr = MatDestroy(&newlocmat);CHKERRQ(ierr);
  }
  ierr = MatAssemblyBegin(*newmat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(*newmat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatGetLocalSubMatrix_IS"
static PetscErrorCode MatGetLocalSubMatrix_IS(Mat A,IS row,IS col,Mat *submat)
{
  Mat                    lA,lsubmat;
  ISLocalToGlobalMapping rl2g,cl2g;
  IS                     is,isn;
  const PetscInt         *rg,*rl;
#if defined(PETSC_USE_DEBUG)
  PetscInt               nrg;
#endif
  PetscInt               N,M,nrl,i,*idxs;
  PetscErrorCode         ierr;

  PetscFunctionBegin;
  /* extract local submatrix (it will complain if row and col exceed the local sizes) */
  ierr = MatISGetLocalMat(A,&lA);CHKERRQ(ierr);
  ierr = MatGetSubMatrix(lA,row,col,MAT_INITIAL_MATRIX,&lsubmat);CHKERRQ(ierr);
  /* compute new l2g map for rows */
  ierr = ISLocalToGlobalMappingGetIndices(A->rmap->mapping,&rg);CHKERRQ(ierr);
  ierr = ISGetLocalSize(row,&nrl);CHKERRQ(ierr);
  ierr = ISGetIndices(row,&rl);CHKERRQ(ierr);
#if defined(PETSC_USE_DEBUG)
  ierr = ISLocalToGlobalMappingGetSize(A->rmap->mapping,&nrg);CHKERRQ(ierr);
  for (i=0;i<nrl;i++) if (rl[i]>=nrg) SETERRQ3(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Local row index %d -> %d greater than maximum possible %d\n",i,rl[i],nrg);
#endif
  ierr = PetscMalloc1(nrl,&idxs);CHKERRQ(ierr);
  for (i=0;i<nrl;i++) idxs[i] = rg[rl[i]];
  ierr = ISRestoreIndices(row,&rl);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingRestoreIndices(A->rmap->mapping,&rg);CHKERRQ(ierr);
  ierr = ISCreateGeneral(PetscObjectComm((PetscObject)A),nrl,idxs,PETSC_COPY_VALUES,&is);CHKERRQ(ierr);
  ierr = PetscFree(idxs);CHKERRQ(ierr);
  ierr = ISRenumber(is,NULL,&M,&isn);CHKERRQ(ierr);
  ierr = ISDestroy(&is);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingCreateIS(isn,&rl2g);CHKERRQ(ierr);
  ierr = ISDestroy(&isn);CHKERRQ(ierr);
  /* compute new l2g map for columns */
  if (col != row || A->rmap->mapping != A->cmap->mapping) {
    const PetscInt *cg,*cl;
#if defined(PETSC_USE_DEBUG)
    PetscInt       ncg;
#endif
    PetscInt       ncl;

    ierr = ISLocalToGlobalMappingGetIndices(A->cmap->mapping,&cg);CHKERRQ(ierr);
    ierr = ISGetLocalSize(col,&ncl);CHKERRQ(ierr);
    ierr = ISGetIndices(col,&cl);CHKERRQ(ierr);
#if defined(PETSC_USE_DEBUG)
    ierr = ISLocalToGlobalMappingGetSize(A->cmap->mapping,&ncg);CHKERRQ(ierr);
    for (i=0;i<ncl;i++) if (cl[i]>=ncg) SETERRQ3(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Local column index %d -> %d greater than maximum possible %d\n",i,cl[i],ncg);
#endif
    ierr = PetscMalloc1(ncl,&idxs);CHKERRQ(ierr);
    for (i=0;i<ncl;i++) idxs[i] = cg[cl[i]];
    ierr = ISRestoreIndices(col,&cl);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingRestoreIndices(A->cmap->mapping,&cg);CHKERRQ(ierr);
    ierr = ISCreateGeneral(PetscObjectComm((PetscObject)A),ncl,idxs,PETSC_COPY_VALUES,&is);CHKERRQ(ierr);
    ierr = PetscFree(idxs);CHKERRQ(ierr);
    ierr = ISRenumber(is,NULL,&N,&isn);CHKERRQ(ierr);
    ierr = ISDestroy(&is);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingCreateIS(isn,&cl2g);CHKERRQ(ierr);
    ierr = ISDestroy(&isn);CHKERRQ(ierr);
  } else {
    ierr = PetscObjectReference((PetscObject)rl2g);CHKERRQ(ierr);
    cl2g = rl2g;
    N = M;
  }
  /* create the MATIS submatrix */
  ierr = MatCreate(PetscObjectComm((PetscObject)A),submat);CHKERRQ(ierr);
  ierr = MatSetSizes(*submat,PETSC_DECIDE,PETSC_DECIDE,M,N);CHKERRQ(ierr);
  ierr = MatSetType(*submat,MATIS);CHKERRQ(ierr);
  ierr = MatSetLocalToGlobalMapping(*submat,rl2g,cl2g);CHKERRQ(ierr);
  ierr = MatISSetLocalMat(*submat,lsubmat);CHKERRQ(ierr);
  ierr = MatDestroy(&lsubmat);CHKERRQ(ierr);
  ierr = MatSetUp(*submat);CHKERRQ(ierr);
  ierr = MatAssemblyBegin(*submat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(*submat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingDestroy(&rl2g);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingDestroy(&cl2g);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatCopy_IS"
static PetscErrorCode MatCopy_IS(Mat A,Mat B,MatStructure str)
{
  Mat_IS         *a = (Mat_IS*)A->data,*b;
  PetscBool      ismatis;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscObjectTypeCompare((PetscObject)B,MATIS,&ismatis);CHKERRQ(ierr);
  if (!ismatis) SETERRQ(PetscObjectComm((PetscObject)B),PETSC_ERR_SUP,"Need to be implemented");
  b = (Mat_IS*)B->data;
  ierr = MatCopy(a->A,b->A,str);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatMissingDiagonal_IS"
static PetscErrorCode MatMissingDiagonal_IS(Mat A,PetscBool  *missing,PetscInt *d)
{
  Vec               v;
  const PetscScalar *array;
  PetscInt          i,n;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  *missing = PETSC_FALSE;
  ierr = MatCreateVecs(A,NULL,&v);CHKERRQ(ierr);
  ierr = MatGetDiagonal(A,v);CHKERRQ(ierr);
  ierr = VecGetLocalSize(v,&n);CHKERRQ(ierr);
  ierr = VecGetArrayRead(v,&array);CHKERRQ(ierr);
  for (i=0;i<n;i++) if (array[i] == 0.) break;
  ierr = VecRestoreArrayRead(v,&array);CHKERRQ(ierr);
  ierr = VecDestroy(&v);CHKERRQ(ierr);
  if (i != n) *missing = PETSC_TRUE;
  if (d) {
    *d = -1;
    if (*missing) {
      PetscInt rstart;
      ierr = MatGetOwnershipRange(A,&rstart,NULL);CHKERRQ(ierr);
      *d = i+rstart;
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISComputeSF_Private"
static PetscErrorCode MatISComputeSF_Private(Mat B)
{
  Mat_IS         *matis = (Mat_IS*)(B->data);
  const PetscInt *gidxs;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatGetSize(matis->A,&matis->sf_nleaves,NULL);CHKERRQ(ierr);
  ierr = MatGetLocalSize(B,&matis->sf_nroots,NULL);CHKERRQ(ierr);
  ierr = PetscSFCreate(PetscObjectComm((PetscObject)B),&matis->sf);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingGetIndices(B->rmap->mapping,&gidxs);CHKERRQ(ierr);
  ierr = PetscSFSetGraphLayout(matis->sf,B->rmap,matis->sf_nleaves,NULL,PETSC_OWN_POINTER,gidxs);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingRestoreIndices(B->rmap->mapping,&gidxs);CHKERRQ(ierr);
  ierr = PetscMalloc2(matis->sf_nroots,&matis->sf_rootdata,matis->sf_nleaves,&matis->sf_leafdata);CHKERRQ(ierr);
  if (B->rmap->mapping != B->cmap->mapping) { /* setup SF for columns */
    ierr = MatGetSize(matis->A,NULL,&matis->csf_nleaves);CHKERRQ(ierr);
    ierr = MatGetLocalSize(B,NULL,&matis->csf_nroots);CHKERRQ(ierr);
    ierr = PetscSFCreate(PetscObjectComm((PetscObject)B),&matis->csf);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingGetIndices(B->cmap->mapping,&gidxs);CHKERRQ(ierr);
    ierr = PetscSFSetGraphLayout(matis->csf,B->cmap,matis->csf_nleaves,NULL,PETSC_OWN_POINTER,gidxs);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingRestoreIndices(B->cmap->mapping,&gidxs);CHKERRQ(ierr);
    ierr = PetscMalloc2(matis->csf_nroots,&matis->csf_rootdata,matis->csf_nleaves,&matis->csf_leafdata);CHKERRQ(ierr);
  } else {
    matis->csf = matis->sf;
    matis->csf_nleaves = matis->sf_nleaves;
    matis->csf_nroots = matis->sf_nroots;
    matis->csf_leafdata = matis->sf_leafdata;
    matis->csf_rootdata = matis->sf_rootdata;
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISSetPreallocation"
/*@
   MatISSetPreallocation - Preallocates memory for a MATIS parallel matrix.

   Collective on MPI_Comm

   Input Parameters:
+  B - the matrix
.  d_nz  - number of nonzeros per row in DIAGONAL portion of local submatrix
           (same value is used for all local rows)
.  d_nnz - array containing the number of nonzeros in the various rows of the
           DIAGONAL portion of the local submatrix (possibly different for each row)
           or NULL, if d_nz is used to specify the nonzero structure.
           The size of this array is equal to the number of local rows, i.e 'm'.
           For matrices that will be factored, you must leave room for (and set)
           the diagonal entry even if it is zero.
.  o_nz  - number of nonzeros per row in the OFF-DIAGONAL portion of local
           submatrix (same value is used for all local rows).
-  o_nnz - array containing the number of nonzeros in the various rows of the
           OFF-DIAGONAL portion of the local submatrix (possibly different for
           each row) or NULL, if o_nz is used to specify the nonzero
           structure. The size of this array is equal to the number
           of local rows, i.e 'm'.

   If the *_nnz parameter is given then the *_nz parameter is ignored

   Level: intermediate

   Notes: This function has the same interface as the MPIAIJ preallocation routine in order to simplify the transition
          from the asssembled format to the unassembled one. It overestimates the preallocation of MATIS local
          matrices; for exact preallocation, the user should set the preallocation directly on local matrix objects.

.keywords: matrix

.seealso: MatCreate(), MatCreateIS(), MatMPIAIJSetPreallocation(), MatISGetLocalMat(), MATIS
@*/
PetscErrorCode  MatISSetPreallocation(Mat B,PetscInt d_nz,const PetscInt d_nnz[],PetscInt o_nz,const PetscInt o_nnz[])
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(B,MAT_CLASSID,1);
  PetscValidType(B,1);
  ierr = PetscTryMethod(B,"MatISSetPreallocation_C",(Mat,PetscInt,const PetscInt[],PetscInt,const PetscInt[]),(B,d_nz,d_nnz,o_nz,o_nnz));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISSetPreallocation_IS"
static PetscErrorCode  MatISSetPreallocation_IS(Mat B,PetscInt d_nz,const PetscInt d_nnz[],PetscInt o_nz,const PetscInt o_nnz[])
{
  Mat_IS         *matis = (Mat_IS*)(B->data);
  PetscInt       bs,i,nlocalcols;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (!matis->A) SETERRQ(PetscObjectComm((PetscObject)B),PETSC_ERR_SUP,"You should first call MatSetLocalToGlobalMapping");
  if (!matis->sf) { /* setup SF if not yet created and allocate rootdata and leafdata */
    ierr = MatISComputeSF_Private(B);CHKERRQ(ierr);
  }
  if (!d_nnz) {
    for (i=0;i<matis->sf_nroots;i++) matis->sf_rootdata[i] = d_nz;
  } else {
    for (i=0;i<matis->sf_nroots;i++) matis->sf_rootdata[i] = d_nnz[i];
  }
  if (!o_nnz) {
    for (i=0;i<matis->sf_nroots;i++) matis->sf_rootdata[i] += o_nz;
  } else {
    for (i=0;i<matis->sf_nroots;i++) matis->sf_rootdata[i] += o_nnz[i];
  }
  ierr = PetscSFBcastBegin(matis->sf,MPIU_INT,matis->sf_rootdata,matis->sf_leafdata);CHKERRQ(ierr);
  ierr = MatGetSize(matis->A,NULL,&nlocalcols);CHKERRQ(ierr);
  ierr = MatGetBlockSize(matis->A,&bs);CHKERRQ(ierr);
  ierr = PetscSFBcastEnd(matis->sf,MPIU_INT,matis->sf_rootdata,matis->sf_leafdata);CHKERRQ(ierr);
  for (i=0;i<matis->sf_nleaves;i++) {
    matis->sf_leafdata[i] = PetscMin(matis->sf_leafdata[i],nlocalcols);
  }
  ierr = MatSeqAIJSetPreallocation(matis->A,0,matis->sf_leafdata);CHKERRQ(ierr);
  for (i=0;i<matis->sf_nleaves/bs;i++) {
    matis->sf_leafdata[i] = matis->sf_leafdata[i*bs]/bs;
  }
  ierr = MatSeqBAIJSetPreallocation(matis->A,bs,0,matis->sf_leafdata);CHKERRQ(ierr);
  for (i=0;i<matis->sf_nleaves/bs;i++) {
    matis->sf_leafdata[i] = matis->sf_leafdata[i]-i;
  }
  ierr = MatSeqSBAIJSetPreallocation(matis->A,bs,0,matis->sf_leafdata);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISSetMPIXAIJPreallocation_Private"
PETSC_EXTERN PetscErrorCode MatISSetMPIXAIJPreallocation_Private(Mat A, Mat B, PetscBool maxreduce)
{
  Mat_IS          *matis = (Mat_IS*)(A->data);
  PetscInt        *my_dnz,*my_onz,*dnz,*onz,*mat_ranges,*row_ownership;
  const PetscInt  *global_indices_r,*global_indices_c;
  PetscInt        i,j,bs,rows,cols;
  PetscInt        lrows,lcols;
  PetscInt        local_rows,local_cols;
  PetscMPIInt     nsubdomains;
  PetscBool       isdense,issbaij;
  PetscErrorCode  ierr;

  PetscFunctionBegin;
  ierr = MPI_Comm_size(PetscObjectComm((PetscObject)A),&nsubdomains);CHKERRQ(ierr);
  ierr = MatGetSize(A,&rows,&cols);CHKERRQ(ierr);
  ierr = MatGetBlockSize(A,&bs);CHKERRQ(ierr);
  ierr = MatGetSize(matis->A,&local_rows,&local_cols);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)matis->A,MATSEQDENSE,&isdense);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)matis->A,MATSEQSBAIJ,&issbaij);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingGetIndices(A->rmap->mapping,&global_indices_r);CHKERRQ(ierr);
  if (A->rmap->mapping != A->cmap->mapping) {
    ierr = ISLocalToGlobalMappingGetIndices(A->cmap->mapping,&global_indices_c);CHKERRQ(ierr);
  } else {
    global_indices_c = global_indices_r;
  }

  if (issbaij) {
    ierr = MatGetRowUpperTriangular(matis->A);CHKERRQ(ierr);
  }
  /*
     An SF reduce is needed to sum up properly on shared rows.
     Note that generally preallocation is not exact, since it overestimates nonzeros
  */
  if (!matis->sf) { /* setup SF if not yet created and allocate rootdata and leafdata */
    ierr = MatISComputeSF_Private(A);CHKERRQ(ierr);
  }
  ierr = MatGetLocalSize(A,&lrows,&lcols);CHKERRQ(ierr);
  ierr = MatPreallocateInitialize(PetscObjectComm((PetscObject)A),lrows,lcols,dnz,onz);CHKERRQ(ierr);
  /* All processes need to compute entire row ownership */
  ierr = PetscMalloc1(rows,&row_ownership);CHKERRQ(ierr);
  ierr = MatGetOwnershipRanges(A,(const PetscInt**)&mat_ranges);CHKERRQ(ierr);
  for (i=0;i<nsubdomains;i++) {
    for (j=mat_ranges[i];j<mat_ranges[i+1];j++) {
      row_ownership[j] = i;
    }
  }
  ierr = MatGetOwnershipRangesColumn(A,(const PetscInt**)&mat_ranges);CHKERRQ(ierr);

  /*
     my_dnz and my_onz contains exact contribution to preallocation from each local mat
     then, they will be summed up properly. This way, preallocation is always sufficient
  */
  ierr = PetscCalloc2(local_rows,&my_dnz,local_rows,&my_onz);CHKERRQ(ierr);
  /* preallocation as a MATAIJ */
  if (isdense) { /* special case for dense local matrices */
    for (i=0;i<local_rows;i++) {
      PetscInt index_row = global_indices_r[i];
      for (j=i;j<local_rows;j++) {
        PetscInt owner = row_ownership[index_row];
        PetscInt index_col = global_indices_c[j];
        if (index_col > mat_ranges[owner]-1 && index_col < mat_ranges[owner+1] ) { /* diag block */
          my_dnz[i] += 1;
        } else { /* offdiag block */
          my_onz[i] += 1;
        }
        /* same as before, interchanging rows and cols */
        if (i != j) {
          owner = row_ownership[index_col];
          if (index_row > mat_ranges[owner]-1 && index_row < mat_ranges[owner+1] ) {
            my_dnz[j] += 1;
          } else {
            my_onz[j] += 1;
          }
        }
      }
    }
  } else { /* TODO: this could be optimized using MatGetRowIJ */
    for (i=0;i<local_rows;i++) {
      const PetscInt *cols;
      PetscInt       ncols,index_row = global_indices_r[i];
      ierr = MatGetRow(matis->A,i,&ncols,&cols,NULL);CHKERRQ(ierr);
      for (j=0;j<ncols;j++) {
        PetscInt owner = row_ownership[index_row];
        PetscInt index_col = global_indices_c[cols[j]];
        if (index_col > mat_ranges[owner]-1 && index_col < mat_ranges[owner+1] ) { /* diag block */
          my_dnz[i] += 1;
        } else { /* offdiag block */
          my_onz[i] += 1;
        }
        /* same as before, interchanging rows and cols */
        if (issbaij && index_col != index_row) {
          owner = row_ownership[index_col];
          if (index_row > mat_ranges[owner]-1 && index_row < mat_ranges[owner+1] ) {
            my_dnz[cols[j]] += 1;
          } else {
            my_onz[cols[j]] += 1;
          }
        }
      }
      ierr = MatRestoreRow(matis->A,i,&ncols,&cols,NULL);CHKERRQ(ierr);
    }
  }
  ierr = ISLocalToGlobalMappingRestoreIndices(A->rmap->mapping,&global_indices_r);CHKERRQ(ierr);
  if (global_indices_c != global_indices_r) {
    ierr = ISLocalToGlobalMappingRestoreIndices(A->cmap->mapping,&global_indices_c);CHKERRQ(ierr);
  }
  ierr = PetscFree(row_ownership);CHKERRQ(ierr);

  /* Reduce my_dnz and my_onz */
  if (maxreduce) {
    ierr = PetscSFReduceBegin(matis->sf,MPIU_INT,my_dnz,dnz,MPI_MAX);CHKERRQ(ierr);
    ierr = PetscSFReduceEnd(matis->sf,MPIU_INT,my_dnz,dnz,MPI_MAX);CHKERRQ(ierr);
    ierr = PetscSFReduceBegin(matis->sf,MPIU_INT,my_onz,onz,MPI_MAX);CHKERRQ(ierr);
    ierr = PetscSFReduceEnd(matis->sf,MPIU_INT,my_onz,onz,MPI_MAX);CHKERRQ(ierr);
  } else {
    ierr = PetscSFReduceBegin(matis->sf,MPIU_INT,my_dnz,dnz,MPI_SUM);CHKERRQ(ierr);
    ierr = PetscSFReduceEnd(matis->sf,MPIU_INT,my_dnz,dnz,MPI_SUM);CHKERRQ(ierr);
    ierr = PetscSFReduceBegin(matis->sf,MPIU_INT,my_onz,onz,MPI_SUM);CHKERRQ(ierr);
    ierr = PetscSFReduceEnd(matis->sf,MPIU_INT,my_onz,onz,MPI_SUM);CHKERRQ(ierr);
  }
  ierr = PetscFree2(my_dnz,my_onz);CHKERRQ(ierr);

  /* Resize preallocation if overestimated */
  for (i=0;i<lrows;i++) {
    dnz[i] = PetscMin(dnz[i],lcols);
    onz[i] = PetscMin(onz[i],cols-lcols);
  }
  /* set preallocation */
  ierr = MatMPIAIJSetPreallocation(B,0,dnz,0,onz);CHKERRQ(ierr);
  for (i=0;i<lrows/bs;i++) {
    dnz[i] = dnz[i*bs]/bs;
    onz[i] = onz[i*bs]/bs;
  }
  ierr = MatMPIBAIJSetPreallocation(B,bs,0,dnz,0,onz);CHKERRQ(ierr);
  ierr = MatMPISBAIJSetPreallocation(B,bs,0,dnz,0,onz);CHKERRQ(ierr);
  ierr = MatPreallocateFinalize(dnz,onz);CHKERRQ(ierr);
  if (issbaij) {
    ierr = MatRestoreRowUpperTriangular(matis->A);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISGetMPIXAIJ_IS"
static PetscErrorCode MatISGetMPIXAIJ_IS(Mat mat, MatReuse reuse, Mat *M)
{
  Mat_IS         *matis = (Mat_IS*)(mat->data);
  Mat            local_mat;
  /* info on mat */
  PetscInt       bs,rows,cols,lrows,lcols;
  PetscInt       local_rows,local_cols;
  PetscBool      isdense,issbaij,isseqaij;
  PetscMPIInt    nsubdomains;
  /* values insertion */
  PetscScalar    *array;
  /* work */
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* get info from mat */
  ierr = MPI_Comm_size(PetscObjectComm((PetscObject)mat),&nsubdomains);CHKERRQ(ierr);
  if (nsubdomains == 1) {
    if (reuse == MAT_INITIAL_MATRIX) {
      ierr = MatDuplicate(matis->A,MAT_COPY_VALUES,&(*M));CHKERRQ(ierr);
    } else {
      ierr = MatCopy(matis->A,*M,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
    }
    PetscFunctionReturn(0);
  }
  ierr = MatGetSize(mat,&rows,&cols);CHKERRQ(ierr);
  ierr = MatGetBlockSize(mat,&bs);CHKERRQ(ierr);
  ierr = MatGetLocalSize(mat,&lrows,&lcols);CHKERRQ(ierr);
  ierr = MatGetSize(matis->A,&local_rows,&local_cols);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)matis->A,MATSEQDENSE,&isdense);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)matis->A,MATSEQAIJ,&isseqaij);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)matis->A,MATSEQSBAIJ,&issbaij);CHKERRQ(ierr);

  if (reuse == MAT_INITIAL_MATRIX) {
    MatType     new_mat_type;
    PetscBool   issbaij_red;

    /* determining new matrix type */
    ierr = MPIU_Allreduce(&issbaij,&issbaij_red,1,MPIU_BOOL,MPI_LAND,PetscObjectComm((PetscObject)mat));CHKERRQ(ierr);
    if (issbaij_red) {
      new_mat_type = MATSBAIJ;
    } else {
      if (bs>1) {
        new_mat_type = MATBAIJ;
      } else {
        new_mat_type = MATAIJ;
      }
    }

    ierr = MatCreate(PetscObjectComm((PetscObject)mat),M);CHKERRQ(ierr);
    ierr = MatSetSizes(*M,lrows,lcols,rows,cols);CHKERRQ(ierr);
    ierr = MatSetBlockSize(*M,bs);CHKERRQ(ierr);
    ierr = MatSetType(*M,new_mat_type);CHKERRQ(ierr);
    ierr = MatISSetMPIXAIJPreallocation_Private(mat,*M,PETSC_FALSE);CHKERRQ(ierr);
  } else {
    PetscInt mbs,mrows,mcols,mlrows,mlcols;
    /* some checks */
    ierr = MatGetBlockSize(*M,&mbs);CHKERRQ(ierr);
    ierr = MatGetSize(*M,&mrows,&mcols);CHKERRQ(ierr);
    ierr = MatGetLocalSize(*M,&mlrows,&mlcols);CHKERRQ(ierr);
    if (mrows != rows) SETERRQ2(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse matrix. Wrong number of rows (%d != %d)",rows,mrows);
    if (mcols != cols) SETERRQ2(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse matrix. Wrong number of cols (%d != %d)",cols,mcols);
    if (mlrows != lrows) SETERRQ2(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse matrix. Wrong number of local rows (%d != %d)",lrows,mlrows);
    if (mlcols != lcols) SETERRQ2(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse matrix. Wrong number of local cols (%d != %d)",lcols,mlcols);
    if (mbs != bs) SETERRQ2(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse matrix. Wrong block size (%d != %d)",bs,mbs);
    ierr = MatZeroEntries(*M);CHKERRQ(ierr);
  }

  if (issbaij) {
    ierr = MatConvert(matis->A,MATSEQBAIJ,MAT_INITIAL_MATRIX,&local_mat);CHKERRQ(ierr);
  } else {
    ierr = PetscObjectReference((PetscObject)matis->A);CHKERRQ(ierr);
    local_mat = matis->A;
  }

  /* Set values */
  ierr = MatSetLocalToGlobalMapping(*M,mat->rmap->mapping,mat->cmap->mapping);CHKERRQ(ierr);
  if (isdense) { /* special case for dense local matrices */
    PetscInt i,*dummy_rows,*dummy_cols;

    if (local_rows != local_cols) {
      ierr = PetscMalloc2(local_rows,&dummy_rows,local_cols,&dummy_cols);CHKERRQ(ierr);
      for (i=0;i<local_rows;i++) dummy_rows[i] = i;
      for (i=0;i<local_cols;i++) dummy_cols[i] = i;
    } else {
      ierr = PetscMalloc1(local_rows,&dummy_rows);CHKERRQ(ierr);
      for (i=0;i<local_rows;i++) dummy_rows[i] = i;
      dummy_cols = dummy_rows;
    }
    ierr = MatSetOption(*M,MAT_ROW_ORIENTED,PETSC_FALSE);CHKERRQ(ierr);
    ierr = MatDenseGetArray(local_mat,&array);CHKERRQ(ierr);
    ierr = MatSetValuesLocal(*M,local_rows,dummy_rows,local_cols,dummy_cols,array,ADD_VALUES);CHKERRQ(ierr);
    ierr = MatDenseRestoreArray(local_mat,&array);CHKERRQ(ierr);
    if (dummy_rows != dummy_cols) {
      ierr = PetscFree2(dummy_rows,dummy_cols);CHKERRQ(ierr);
    } else {
      ierr = PetscFree(dummy_rows);CHKERRQ(ierr);
    }
  } else if (isseqaij) {
    PetscInt  i,nvtxs,*xadj,*adjncy;
    PetscBool done;

    ierr = MatGetRowIJ(local_mat,0,PETSC_FALSE,PETSC_FALSE,&nvtxs,(const PetscInt**)&xadj,(const PetscInt**)&adjncy,&done);CHKERRQ(ierr);
    if (!done) SETERRQ1(PetscObjectComm((PetscObject)local_mat),PETSC_ERR_PLIB,"Error in MatRestoreRowIJ called in %s\n",__FUNCT__);
    ierr = MatSeqAIJGetArray(local_mat,&array);CHKERRQ(ierr);
    for (i=0;i<nvtxs;i++) {
      ierr = MatSetValuesLocal(*M,1,&i,xadj[i+1]-xadj[i],adjncy+xadj[i],array+xadj[i],ADD_VALUES);CHKERRQ(ierr);
    }
    ierr = MatRestoreRowIJ(local_mat,0,PETSC_FALSE,PETSC_FALSE,&nvtxs,(const PetscInt**)&xadj,(const PetscInt**)&adjncy,&done);CHKERRQ(ierr);
    if (!done) SETERRQ1(PetscObjectComm((PetscObject)local_mat),PETSC_ERR_PLIB,"Error in MatRestoreRowIJ called in %s\n",__FUNCT__);
    ierr = MatSeqAIJRestoreArray(local_mat,&array);CHKERRQ(ierr);
  } else { /* very basic values insertion for all other matrix types */
    PetscInt i;

    for (i=0;i<local_rows;i++) {
      PetscInt       j;
      const PetscInt *local_indices_cols;

      ierr = MatGetRow(local_mat,i,&j,&local_indices_cols,(const PetscScalar**)&array);CHKERRQ(ierr);
      ierr = MatSetValuesLocal(*M,1,&i,j,local_indices_cols,array,ADD_VALUES);CHKERRQ(ierr);
      ierr = MatRestoreRow(local_mat,i,&j,&local_indices_cols,(const PetscScalar**)&array);CHKERRQ(ierr);
    }
  }
  ierr = MatAssemblyBegin(*M,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatDestroy(&local_mat);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(*M,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  if (isdense) {
    ierr = MatSetOption(*M,MAT_ROW_ORIENTED,PETSC_TRUE);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISGetMPIXAIJ"
/*@
    MatISGetMPIXAIJ - Converts MATIS matrix into a parallel AIJ format

  Input Parameter:
.  mat - the matrix (should be of type MATIS)
.  reuse - either MAT_INITIAL_MATRIX or MAT_REUSE_MATRIX

  Output Parameter:
.  newmat - the matrix in AIJ format

  Level: developer

  Notes: mat and *newmat cannot be the same object when MAT_REUSE_MATRIX is requested.

.seealso: MATIS
@*/
PetscErrorCode MatISGetMPIXAIJ(Mat mat, MatReuse reuse, Mat *newmat)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(mat,MAT_CLASSID,1);
  PetscValidLogicalCollectiveEnum(mat,reuse,2);
  PetscValidPointer(newmat,3);
  if (reuse != MAT_INITIAL_MATRIX) {
    PetscValidHeaderSpecific(*newmat,MAT_CLASSID,3);
    PetscCheckSameComm(mat,1,*newmat,3);
    if (mat == *newmat) SETERRQ(PetscObjectComm((PetscObject)mat),PETSC_ERR_SUP,"Cannot reuse the same matrix");
  }
  ierr = PetscUseMethod(mat,"MatISGetMPIXAIJ_C",(Mat,MatReuse,Mat*),(mat,reuse,newmat));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatDuplicate_IS"
PetscErrorCode MatDuplicate_IS(Mat mat,MatDuplicateOption op,Mat *newmat)
{
  PetscErrorCode ierr;
  Mat_IS         *matis = (Mat_IS*)(mat->data);
  PetscInt       bs,m,n,M,N;
  Mat            B,localmat;

  PetscFunctionBegin;
  ierr = MatGetBlockSize(mat,&bs);CHKERRQ(ierr);
  ierr = MatGetSize(mat,&M,&N);CHKERRQ(ierr);
  ierr = MatGetLocalSize(mat,&m,&n);CHKERRQ(ierr);
  ierr = MatCreateIS(PetscObjectComm((PetscObject)mat),bs,m,n,M,N,mat->rmap->mapping,mat->cmap->mapping,&B);CHKERRQ(ierr);
  ierr = MatDuplicate(matis->A,op,&localmat);CHKERRQ(ierr);
  ierr = MatISSetLocalMat(B,localmat);CHKERRQ(ierr);
  ierr = MatDestroy(&localmat);CHKERRQ(ierr);
  ierr = MatAssemblyBegin(B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  *newmat = B;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatIsHermitian_IS"
static PetscErrorCode MatIsHermitian_IS(Mat A,PetscReal tol,PetscBool  *flg)
{
  PetscErrorCode ierr;
  Mat_IS         *matis = (Mat_IS*)A->data;
  PetscBool      local_sym;

  PetscFunctionBegin;
  ierr = MatIsHermitian(matis->A,tol,&local_sym);CHKERRQ(ierr);
  ierr = MPIU_Allreduce(&local_sym,flg,1,MPIU_BOOL,MPI_LAND,PetscObjectComm((PetscObject)A));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatIsSymmetric_IS"
static PetscErrorCode MatIsSymmetric_IS(Mat A,PetscReal tol,PetscBool  *flg)
{
  PetscErrorCode ierr;
  Mat_IS         *matis = (Mat_IS*)A->data;
  PetscBool      local_sym;

  PetscFunctionBegin;
  ierr = MatIsSymmetric(matis->A,tol,&local_sym);CHKERRQ(ierr);
  ierr = MPIU_Allreduce(&local_sym,flg,1,MPIU_BOOL,MPI_LAND,PetscObjectComm((PetscObject)A));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatDestroy_IS"
static PetscErrorCode MatDestroy_IS(Mat A)
{
  PetscErrorCode ierr;
  Mat_IS         *b = (Mat_IS*)A->data;

  PetscFunctionBegin;
  ierr = MatDestroy(&b->A);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&b->cctx);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&b->rctx);CHKERRQ(ierr);
  ierr = VecDestroy(&b->x);CHKERRQ(ierr);
  ierr = VecDestroy(&b->y);CHKERRQ(ierr);
  ierr = ISDestroy(&b->getsub_ris);CHKERRQ(ierr);
  ierr = ISDestroy(&b->getsub_cis);CHKERRQ(ierr);
  if (b->sf != b->csf) {
    ierr = PetscSFDestroy(&b->csf);CHKERRQ(ierr);
    ierr = PetscFree2(b->csf_rootdata,b->csf_leafdata);CHKERRQ(ierr);
  }
  ierr = PetscSFDestroy(&b->sf);CHKERRQ(ierr);
  ierr = PetscFree2(b->sf_rootdata,b->sf_leafdata);CHKERRQ(ierr);
  ierr = PetscFree(A->data);CHKERRQ(ierr);
  ierr = PetscObjectChangeTypeName((PetscObject)A,0);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISGetLocalMat_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISSetLocalMat_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISGetMPIXAIJ_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISSetPreallocation_C",NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatMult_IS"
static PetscErrorCode MatMult_IS(Mat A,Vec x,Vec y)
{
  PetscErrorCode ierr;
  Mat_IS         *is  = (Mat_IS*)A->data;
  PetscScalar    zero = 0.0;

  PetscFunctionBegin;
  /*  scatter the global vector x into the local work vector */
  ierr = VecScatterBegin(is->cctx,x,is->x,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(is->cctx,x,is->x,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  /* multiply the local matrix */
  ierr = MatMult(is->A,is->x,is->y);CHKERRQ(ierr);

  /* scatter product back into global memory */
  ierr = VecSet(y,zero);CHKERRQ(ierr);
  ierr = VecScatterBegin(is->rctx,is->y,y,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  ierr = VecScatterEnd(is->rctx,is->y,y,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatMultAdd_IS"
static PetscErrorCode MatMultAdd_IS(Mat A,Vec v1,Vec v2,Vec v3)
{
  Vec            temp_vec;
  PetscErrorCode ierr;

  PetscFunctionBegin; /*  v3 = v2 + A * v1.*/
  if (v3 != v2) {
    ierr = MatMult(A,v1,v3);CHKERRQ(ierr);
    ierr = VecAXPY(v3,1.0,v2);CHKERRQ(ierr);
  } else {
    ierr = VecDuplicate(v2,&temp_vec);CHKERRQ(ierr);
    ierr = MatMult(A,v1,temp_vec);CHKERRQ(ierr);
    ierr = VecAXPY(temp_vec,1.0,v2);CHKERRQ(ierr);
    ierr = VecCopy(temp_vec,v3);CHKERRQ(ierr);
    ierr = VecDestroy(&temp_vec);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatMultTranspose_IS"
static PetscErrorCode MatMultTranspose_IS(Mat A,Vec y,Vec x)
{
  Mat_IS         *is = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /*  scatter the global vector x into the local work vector */
  ierr = VecScatterBegin(is->rctx,y,is->y,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(is->rctx,y,is->y,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  /* multiply the local matrix */
  ierr = MatMultTranspose(is->A,is->y,is->x);CHKERRQ(ierr);

  /* scatter product back into global vector */
  ierr = VecSet(x,0);CHKERRQ(ierr);
  ierr = VecScatterBegin(is->cctx,is->x,x,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  ierr = VecScatterEnd(is->cctx,is->x,x,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatMultTransposeAdd_IS"
static PetscErrorCode MatMultTransposeAdd_IS(Mat A,Vec v1,Vec v2,Vec v3)
{
  Vec            temp_vec;
  PetscErrorCode ierr;

  PetscFunctionBegin; /*  v3 = v2 + A' * v1.*/
  if (v3 != v2) {
    ierr = MatMultTranspose(A,v1,v3);CHKERRQ(ierr);
    ierr = VecAXPY(v3,1.0,v2);CHKERRQ(ierr);
  } else {
    ierr = VecDuplicate(v2,&temp_vec);CHKERRQ(ierr);
    ierr = MatMultTranspose(A,v1,temp_vec);CHKERRQ(ierr);
    ierr = VecAXPY(temp_vec,1.0,v2);CHKERRQ(ierr);
    ierr = VecCopy(temp_vec,v3);CHKERRQ(ierr);
    ierr = VecDestroy(&temp_vec);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatView_IS"
static PetscErrorCode MatView_IS(Mat A,PetscViewer viewer)
{
  Mat_IS         *a = (Mat_IS*)A->data;
  PetscErrorCode ierr;
  PetscViewer    sviewer;

  PetscFunctionBegin;
  ierr = PetscViewerGetSubViewer(viewer,PETSC_COMM_SELF,&sviewer);CHKERRQ(ierr);
  ierr = MatView(a->A,sviewer);CHKERRQ(ierr);
  ierr = PetscViewerRestoreSubViewer(viewer,PETSC_COMM_SELF,&sviewer);CHKERRQ(ierr);
  ierr = PetscViewerFlush(viewer);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatSetLocalToGlobalMapping_IS"
static PetscErrorCode MatSetLocalToGlobalMapping_IS(Mat A,ISLocalToGlobalMapping rmapping,ISLocalToGlobalMapping cmapping)
{
  PetscErrorCode ierr;
  PetscInt       nr,rbs,nc,cbs;
  Mat_IS         *is = (Mat_IS*)A->data;
  IS             from,to;
  Vec            cglobal,rglobal;

  PetscFunctionBegin;
  PetscCheckSameComm(A,1,rmapping,2);
  PetscCheckSameComm(A,1,cmapping,3);
  /* Destroy any previous data */
  ierr = VecDestroy(&is->x);CHKERRQ(ierr);
  ierr = VecDestroy(&is->y);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&is->rctx);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&is->cctx);CHKERRQ(ierr);
  ierr = MatDestroy(&is->A);CHKERRQ(ierr);
  ierr = PetscSFDestroy(&is->sf);CHKERRQ(ierr);
  ierr = PetscFree2(is->sf_rootdata,is->sf_leafdata);CHKERRQ(ierr);

  /* Setup Layout and set local to global maps */
  ierr = PetscLayoutSetUp(A->rmap);CHKERRQ(ierr);
  ierr = PetscLayoutSetUp(A->cmap);CHKERRQ(ierr);
  ierr = PetscLayoutSetISLocalToGlobalMapping(A->rmap,rmapping);CHKERRQ(ierr);
  ierr = PetscLayoutSetISLocalToGlobalMapping(A->cmap,cmapping);CHKERRQ(ierr);

  /* Create the local matrix A */
  ierr = ISLocalToGlobalMappingGetSize(rmapping,&nr);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingGetBlockSize(rmapping,&rbs);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingGetSize(cmapping,&nc);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingGetBlockSize(cmapping,&cbs);CHKERRQ(ierr);
  ierr = MatCreate(PETSC_COMM_SELF,&is->A);CHKERRQ(ierr);
  ierr = MatSetType(is->A,MATAIJ);CHKERRQ(ierr);
  ierr = MatSetSizes(is->A,nr,nc,nr,nc);CHKERRQ(ierr);
  ierr = MatSetBlockSizes(is->A,rbs,cbs);CHKERRQ(ierr);
  ierr = MatSetOptionsPrefix(is->A,((PetscObject)A)->prefix);CHKERRQ(ierr);
  ierr = MatAppendOptionsPrefix(is->A,"is_");CHKERRQ(ierr);
  ierr = MatSetFromOptions(is->A);CHKERRQ(ierr);

  /* Create the local work vectors */
  ierr = MatCreateVecs(is->A,&is->x,&is->y);CHKERRQ(ierr);

  /* setup the global to local scatters */
  ierr = MatCreateVecs(A,&cglobal,&rglobal);CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_SELF,nr,0,1,&to);CHKERRQ(ierr);
  ierr = ISLocalToGlobalMappingApplyIS(rmapping,to,&from);CHKERRQ(ierr);
  ierr = VecScatterCreate(rglobal,from,is->y,to,&is->rctx);CHKERRQ(ierr);
  if (rmapping != cmapping) {
    ierr = ISDestroy(&to);CHKERRQ(ierr);
    ierr = ISDestroy(&from);CHKERRQ(ierr);
    ierr = ISCreateStride(PETSC_COMM_SELF,nc,0,1,&to);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingApplyIS(cmapping,to,&from);CHKERRQ(ierr);
    ierr = VecScatterCreate(cglobal,from,is->x,to,&is->cctx);CHKERRQ(ierr);
  } else {
    ierr = PetscObjectReference((PetscObject)is->rctx);CHKERRQ(ierr);
    is->cctx = is->rctx;
  }
  ierr = VecDestroy(&rglobal);CHKERRQ(ierr);
  ierr = VecDestroy(&cglobal);CHKERRQ(ierr);
  ierr = ISDestroy(&to);CHKERRQ(ierr);
  ierr = ISDestroy(&from);CHKERRQ(ierr);
  ierr = MatSetUp(A);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatSetValues_IS"
static PetscErrorCode MatSetValues_IS(Mat mat, PetscInt m,const PetscInt *rows, PetscInt n,const PetscInt *cols, const PetscScalar *values, InsertMode addv)
{
  Mat_IS         *is = (Mat_IS*)mat->data;
  PetscErrorCode ierr;
#if defined(PETSC_USE_DEBUG)
  PetscInt       i,zm,zn;
#endif
  PetscInt       rows_l[2048],cols_l[2048];

  PetscFunctionBegin;
#if defined(PETSC_USE_DEBUG)
  if (m > 2048 || n > 2048) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"Number of row/column indices must be <= 2048: they are %D %D",m,n);
  /* count negative indices */
  for (i=0,zm=0;i<m;i++) if (rows[i] < 0) zm++;
  for (i=0,zn=0;i<n;i++) if (cols[i] < 0) zn++;
#endif
  ierr = ISGlobalToLocalMappingApply(mat->rmap->mapping,IS_GTOLM_MASK,m,rows,&m,rows_l);CHKERRQ(ierr);
  ierr = ISGlobalToLocalMappingApply(mat->cmap->mapping,IS_GTOLM_MASK,n,cols,&n,cols_l);CHKERRQ(ierr);
#if defined(PETSC_USE_DEBUG)
  /* count negative indices (should be the same as before) */
  for (i=0;i<m;i++) if (rows_l[i] < 0) zm--;
  for (i=0;i<n;i++) if (cols_l[i] < 0) zn--;
  if (zm) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SUP,"Some of the row indices can not be mapped! Maybe you should not use MATIS");
  if (zn) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SUP,"Some of the column indices can not be mapped! Maybe you should not use MATIS");
#endif
  ierr = MatSetValues(is->A,m,rows_l,n,cols_l,values,addv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatSetValuesBlocked_IS"
static PetscErrorCode MatSetValuesBlocked_IS(Mat mat, PetscInt m,const PetscInt *rows, PetscInt n,const PetscInt *cols, const PetscScalar *values, InsertMode addv)
{
  Mat_IS         *is = (Mat_IS*)mat->data;
  PetscErrorCode ierr;
#if defined(PETSC_USE_DEBUG)
  PetscInt       i,zm,zn;
#endif
  PetscInt       rows_l[2048],cols_l[2048];

  PetscFunctionBegin;
#if defined(PETSC_USE_DEBUG)
  if (m > 2048 || n > 2048) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"Number of row/column indices must be <= 2048: they are %D %D",m,n);
  /* count negative indices */
  for (i=0,zm=0;i<m;i++) if (rows[i] < 0) zm++;
  for (i=0,zn=0;i<n;i++) if (cols[i] < 0) zn++;
#endif
  ierr = ISGlobalToLocalMappingApplyBlock(mat->rmap->mapping,IS_GTOLM_MASK,m,rows,&m,rows_l);CHKERRQ(ierr);
  ierr = ISGlobalToLocalMappingApplyBlock(mat->cmap->mapping,IS_GTOLM_MASK,n,cols,&n,cols_l);CHKERRQ(ierr);
#if defined(PETSC_USE_DEBUG)
  /* count negative indices (should be the same as before) */
  for (i=0;i<m;i++) if (rows_l[i] < 0) zm--;
  for (i=0;i<n;i++) if (cols_l[i] < 0) zn--;
  if (zm) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SUP,"Some of the row indices can not be mapped! Maybe you should not use MATIS");
  if (zn) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SUP,"Some of the column indices can not be mapped! Maybe you should not use MATIS");
#endif
  ierr = MatSetValuesBlocked(is->A,m,rows_l,n,cols_l,values,addv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatSetValuesLocal_IS"
static PetscErrorCode MatSetValuesLocal_IS(Mat A,PetscInt m,const PetscInt *rows, PetscInt n,const PetscInt *cols,const PetscScalar *values,InsertMode addv)
{
  PetscErrorCode ierr;
  Mat_IS         *is = (Mat_IS*)A->data;

  PetscFunctionBegin;
  ierr = MatSetValues(is->A,m,rows,n,cols,values,addv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatSetValuesBlockedLocal_IS"
static PetscErrorCode MatSetValuesBlockedLocal_IS(Mat A,PetscInt m,const PetscInt *rows, PetscInt n,const PetscInt *cols,const PetscScalar *values,InsertMode addv)
{
  PetscErrorCode ierr;
  Mat_IS         *is = (Mat_IS*)A->data;

  PetscFunctionBegin;
  ierr = MatSetValuesBlocked(is->A,m,rows,n,cols,values,addv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatZeroRows_IS"
static PetscErrorCode MatZeroRows_IS(Mat A,PetscInt n,const PetscInt rows[],PetscScalar diag,Vec x,Vec b)
{
  Mat_IS         *matis = (Mat_IS*)A->data;
  PetscInt       nr,nl,len,i;
  PetscInt       *lrows;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* get locally owned rows */
  ierr = MatZeroRowsMapLocal_Private(A,n,rows,&len,&lrows);CHKERRQ(ierr);
  /* fix right hand side if needed */
  if (x && b) {
    const PetscScalar *xx;
    PetscScalar       *bb;

    ierr = VecGetArrayRead(x, &xx);CHKERRQ(ierr);
    ierr = VecGetArray(b, &bb);CHKERRQ(ierr);
    for (i=0;i<len;++i) bb[lrows[i]] = diag*xx[lrows[i]];
    ierr = VecRestoreArrayRead(x, &xx);CHKERRQ(ierr);
    ierr = VecRestoreArray(b, &bb);CHKERRQ(ierr);
  }
  /* get rows associated to the local matrices */
  if (!matis->sf) { /* setup SF if not yet created and allocate rootdata and leafdata */
    ierr = MatISComputeSF_Private(A);CHKERRQ(ierr);
  }
  ierr = MatGetSize(matis->A,&nl,NULL);CHKERRQ(ierr);
  ierr = PetscMemzero(matis->sf_leafdata,nl*sizeof(PetscInt));CHKERRQ(ierr);
  ierr = PetscMemzero(matis->sf_rootdata,A->rmap->n*sizeof(PetscInt));CHKERRQ(ierr);
  for (i=0;i<len;i++) matis->sf_rootdata[lrows[i]] = 1;
  ierr = PetscFree(lrows);CHKERRQ(ierr);
  ierr = PetscSFBcastBegin(matis->sf,MPIU_INT,matis->sf_rootdata,matis->sf_leafdata);CHKERRQ(ierr);
  ierr = PetscSFBcastEnd(matis->sf,MPIU_INT,matis->sf_rootdata,matis->sf_leafdata);CHKERRQ(ierr);
  ierr = PetscMalloc1(nl,&lrows);CHKERRQ(ierr);
  for (i=0,nr=0;i<nl;i++) if (matis->sf_leafdata[i]) lrows[nr++] = i;
  ierr = MatISZeroRowsLocal_Private(A,nr,lrows,diag);CHKERRQ(ierr);
  ierr = PetscFree(lrows);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISZeroRowsLocal_Private"
static PetscErrorCode MatISZeroRowsLocal_Private(Mat A,PetscInt n,const PetscInt rows[],PetscScalar diag)
{
  Mat_IS         *is = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (diag != 0.) {
    /*
       Set up is->x as a "counting vector". This is in order to MatMult_IS
       work properly in the interface nodes.
    */
    Vec counter;
    ierr = MatCreateVecs(A,NULL,&counter);CHKERRQ(ierr);
    ierr = VecSet(counter,0.);CHKERRQ(ierr);
    ierr = VecSet(is->y,1.);CHKERRQ(ierr);
    ierr = VecScatterBegin(is->rctx,is->y,counter,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecScatterEnd(is->rctx,is->y,counter,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
    ierr = VecScatterBegin(is->rctx,counter,is->y,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecScatterEnd(is->rctx,counter,is->y,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecDestroy(&counter);CHKERRQ(ierr);
  }

  if (!n) {
    is->pure_neumann = PETSC_TRUE;
  } else {
    PetscInt i;
    is->pure_neumann = PETSC_FALSE;

    ierr = MatZeroRows(is->A,n,rows,diag,0,0);CHKERRQ(ierr);
    if (diag != 0.) {
      const PetscScalar *array;
      ierr = VecGetArrayRead(is->y,&array);CHKERRQ(ierr);
      for (i=0; i<n; i++) {
        ierr = MatSetValue(is->A,rows[i],rows[i],diag/(array[rows[i]]),INSERT_VALUES);CHKERRQ(ierr);
      }
      ierr = VecRestoreArrayRead(is->y,&array);CHKERRQ(ierr);
    }
    ierr = MatAssemblyBegin(is->A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(is->A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatAssemblyBegin_IS"
static PetscErrorCode MatAssemblyBegin_IS(Mat A,MatAssemblyType type)
{
  Mat_IS         *is = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatAssemblyBegin(is->A,type);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatAssemblyEnd_IS"
static PetscErrorCode MatAssemblyEnd_IS(Mat A,MatAssemblyType type)
{
  Mat_IS         *is = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatAssemblyEnd(is->A,type);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISGetLocalMat_IS"
static PetscErrorCode MatISGetLocalMat_IS(Mat mat,Mat *local)
{
  Mat_IS *is = (Mat_IS*)mat->data;

  PetscFunctionBegin;
  *local = is->A;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISGetLocalMat"
/*@
    MatISGetLocalMat - Gets the local matrix stored inside a MATIS matrix.

  Input Parameter:
.  mat - the matrix

  Output Parameter:
.  local - the local matrix

  Level: advanced

  Notes:
    This can be called if you have precomputed the nonzero structure of the
  matrix and want to provide it to the inner matrix object to improve the performance
  of the MatSetValues() operation.

.seealso: MATIS
@*/
PetscErrorCode MatISGetLocalMat(Mat mat,Mat *local)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(mat,MAT_CLASSID,1);
  PetscValidPointer(local,2);
  ierr = PetscUseMethod(mat,"MatISGetLocalMat_C",(Mat,Mat*),(mat,local));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISSetLocalMat_IS"
static PetscErrorCode MatISSetLocalMat_IS(Mat mat,Mat local)
{
  Mat_IS         *is = (Mat_IS*)mat->data;
  PetscInt       nrows,ncols,orows,ocols;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (is->A) {
    ierr = MatGetSize(is->A,&orows,&ocols);CHKERRQ(ierr);
    ierr = MatGetSize(local,&nrows,&ncols);CHKERRQ(ierr);
    if (orows != nrows || ocols != ncols) SETERRQ4(PETSC_COMM_SELF,PETSC_ERR_ARG_SIZ,"Local MATIS matrix should be of size %dx%d (you passed a %dx%d matrix)\n",orows,ocols,nrows,ncols);
  }
  ierr  = PetscObjectReference((PetscObject)local);CHKERRQ(ierr);
  ierr  = MatDestroy(&is->A);CHKERRQ(ierr);
  is->A = local;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatISSetLocalMat"
/*@
    MatISSetLocalMat - Replace the local matrix stored inside a MATIS object.

  Input Parameter:
.  mat - the matrix
.  local - the local matrix

  Output Parameter:

  Level: advanced

  Notes:
    This can be called if you have precomputed the local matrix and
  want to provide it to the matrix object MATIS.

.seealso: MATIS
@*/
PetscErrorCode MatISSetLocalMat(Mat mat,Mat local)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(mat,MAT_CLASSID,1);
  PetscValidHeaderSpecific(local,MAT_CLASSID,2);
  ierr = PetscUseMethod(mat,"MatISSetLocalMat_C",(Mat,Mat),(mat,local));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatZeroEntries_IS"
static PetscErrorCode MatZeroEntries_IS(Mat A)
{
  Mat_IS         *a = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatZeroEntries(a->A);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatScale_IS"
static PetscErrorCode MatScale_IS(Mat A,PetscScalar a)
{
  Mat_IS         *is = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatScale(is->A,a);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatGetDiagonal_IS"
static PetscErrorCode MatGetDiagonal_IS(Mat A, Vec v)
{
  Mat_IS         *is = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* get diagonal of the local matrix */
  ierr = MatGetDiagonal(is->A,is->y);CHKERRQ(ierr);

  /* scatter diagonal back into global vector */
  ierr = VecSet(v,0);CHKERRQ(ierr);
  ierr = VecScatterBegin(is->rctx,is->y,v,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  ierr = VecScatterEnd(is->rctx,is->y,v,ADD_VALUES,SCATTER_REVERSE);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatSetOption_IS"
static PetscErrorCode MatSetOption_IS(Mat A,MatOption op,PetscBool flg)
{
  Mat_IS         *a = (Mat_IS*)A->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatSetOption(a->A,op,flg);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MatCreateIS"
/*@
    MatCreateIS - Creates a "process" unassembled matrix, assembled on each
       process but not across processes.

   Input Parameters:
+     comm    - MPI communicator that will share the matrix
.     bs      - block size of the matrix
.     m,n,M,N - local and/or global sizes of the left and right vector used in matrix vector products
.     rmap    - local to global map for rows
-     cmap    - local to global map for cols

   Output Parameter:
.    A - the resulting matrix

   Level: advanced

   Notes: See MATIS for more details.
          m and n are NOT related to the size of the map; they are the size of the part of the vector owned
          by that process. The sizes of rmap and cmap define the size of the local matrices.
          If either rmap or cmap are NULL, then the matrix is assumed to be square.

.seealso: MATIS, MatSetLocalToGlobalMapping()
@*/
PetscErrorCode  MatCreateIS(MPI_Comm comm,PetscInt bs,PetscInt m,PetscInt n,PetscInt M,PetscInt N,ISLocalToGlobalMapping rmap,ISLocalToGlobalMapping cmap,Mat *A)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (!rmap && !cmap) SETERRQ(comm,PETSC_ERR_USER,"You need to provide at least one of the mapping");
  ierr = MatCreate(comm,A);CHKERRQ(ierr);
  ierr = MatSetBlockSize(*A,bs);CHKERRQ(ierr);
  ierr = MatSetSizes(*A,m,n,M,N);CHKERRQ(ierr);
  ierr = MatSetType(*A,MATIS);CHKERRQ(ierr);
  if (rmap && cmap) {
    ierr = MatSetLocalToGlobalMapping(*A,rmap,cmap);CHKERRQ(ierr);
  } else if (!rmap) {
    ierr = MatSetLocalToGlobalMapping(*A,cmap,cmap);CHKERRQ(ierr);
  } else {
    ierr = MatSetLocalToGlobalMapping(*A,rmap,rmap);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*MC
   MATIS - MATIS = "is" - A matrix type to be used for using the non-overlapping domain decomposition type preconditioners (e.g. PCBDDC).
   This stores the matrices in globally unassembled form. Each processor
   assembles only its local Neumann problem and the parallel matrix vector
   product is handled "implicitly".

   Operations Provided:
+  MatMult()
.  MatMultAdd()
.  MatMultTranspose()
.  MatMultTransposeAdd()
.  MatZeroEntries()
.  MatSetOption()
.  MatZeroRows()
.  MatSetValues()
.  MatSetValuesBlocked()
.  MatSetValuesLocal()
.  MatSetValuesBlockedLocal()
.  MatScale()
.  MatGetDiagonal()
.  MatMissingDiagonal()
.  MatDuplicate()
.  MatCopy()
-  MatSetLocalToGlobalMapping()

   Options Database Keys:
. -mat_type is - sets the matrix type to "is" during a call to MatSetFromOptions()

   Notes: Options prefix for the inner matrix are given by -is_mat_xxx

          You must call MatSetLocalToGlobalMapping() before using this matrix type.

          You can do matrix preallocation on the local matrix after you obtain it with
          MatISGetLocalMat(); otherwise, you could use MatISSetPreallocation()

  Level: advanced

.seealso: Mat, MatISGetLocalMat(), MatSetLocalToGlobalMapping(), MatISSetPreallocation(), PCBDDC, MatCreateIS()

M*/

#undef __FUNCT__
#define __FUNCT__ "MatCreate_IS"
PETSC_EXTERN PetscErrorCode MatCreate_IS(Mat A)
{
  PetscErrorCode ierr;
  Mat_IS         *b;

  PetscFunctionBegin;
  ierr    = PetscNewLog(A,&b);CHKERRQ(ierr);
  A->data = (void*)b;

  /* matrix ops */
  ierr    = PetscMemzero(A->ops,sizeof(struct _MatOps));CHKERRQ(ierr);
  A->ops->mult                    = MatMult_IS;
  A->ops->multadd                 = MatMultAdd_IS;
  A->ops->multtranspose           = MatMultTranspose_IS;
  A->ops->multtransposeadd        = MatMultTransposeAdd_IS;
  A->ops->destroy                 = MatDestroy_IS;
  A->ops->setlocaltoglobalmapping = MatSetLocalToGlobalMapping_IS;
  A->ops->setvalues               = MatSetValues_IS;
  A->ops->setvaluesblocked        = MatSetValuesBlocked_IS;
  A->ops->setvalueslocal          = MatSetValuesLocal_IS;
  A->ops->setvaluesblockedlocal   = MatSetValuesBlockedLocal_IS;
  A->ops->zerorows                = MatZeroRows_IS;
  A->ops->assemblybegin           = MatAssemblyBegin_IS;
  A->ops->assemblyend             = MatAssemblyEnd_IS;
  A->ops->view                    = MatView_IS;
  A->ops->zeroentries             = MatZeroEntries_IS;
  A->ops->scale                   = MatScale_IS;
  A->ops->getdiagonal             = MatGetDiagonal_IS;
  A->ops->setoption               = MatSetOption_IS;
  A->ops->ishermitian             = MatIsHermitian_IS;
  A->ops->issymmetric             = MatIsSymmetric_IS;
  A->ops->duplicate               = MatDuplicate_IS;
  A->ops->missingdiagonal         = MatMissingDiagonal_IS;
  A->ops->copy                    = MatCopy_IS;
  A->ops->getlocalsubmatrix       = MatGetLocalSubMatrix_IS;
  A->ops->getsubmatrix            = MatGetSubMatrix_IS;

  /* special MATIS functions */
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISGetLocalMat_C",MatISGetLocalMat_IS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISSetLocalMat_C",MatISSetLocalMat_IS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISGetMPIXAIJ_C",MatISGetMPIXAIJ_IS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)A,"MatISSetPreallocation_C",MatISSetPreallocation_IS);CHKERRQ(ierr);
  ierr = PetscObjectChangeTypeName((PetscObject)A,MATIS);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
