#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: ex1.c,v 1.23 1998/12/03 03:56:25 bsmith Exp bsmith $";
#endif
/*
       Formatted test for ISGeneral routines.
*/

static char help[] = "Tests IS general routines\n\n";

#include "is.h"

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **argv)
{
  int        i, n, ierr,*indices,rank,size,*ii;
  IS         is,newis;
  PetscTruth flag;

  PetscInitialize(&argc,&argv,(char*)0,help);
  MPI_Comm_rank(PETSC_COMM_WORLD,&rank);
  MPI_Comm_size(PETSC_COMM_WORLD,&size);

  /*
     Test IS of size 0 
  */
  ierr = ISCreateGeneral(PETSC_COMM_SELF,0,&n,&is); CHKERRA(ierr);
  ierr = ISGetSize(is,&n); CHKERRA(ierr);
  if (n != 0) SETERRQ(1,0,0);
  ierr = ISDestroy(is); CHKERRA(ierr);

  /*
     Create large IS and test ISGetIndices()
  */
  n = 10000 + rank;
  indices = (int *) PetscMalloc( n*sizeof(int) ); CHKERRA(ierr);
  for ( i=0; i<n; i++ ) {
    indices[i] = rank + i;
  }
  ierr = ISCreateGeneral(PETSC_COMM_SELF,n,indices,&is); CHKERRA(ierr);
  ierr = ISGetIndices(is,&ii); CHKERRA(ierr);
  for ( i=0; i<n; i++ ) {
    if (ii[i] != indices[i]) SETERRA(1,0,0);
  }
  ierr = ISRestoreIndices(is,&ii); CHKERRA(ierr);

  /* 
     Check identity and permutation 
  */
  ierr = ISPermutation(is,&flag); CHKERRA(ierr);
  if (flag == PETSC_TRUE) SETERRA(1,0,0);
  ierr = ISIdentity(is,&flag); CHKERRA(ierr);
  if (flag == PETSC_TRUE) SETERRA(1,0,0);
  ierr = ISSetPermutation(is); CHKERRA(ierr);
  ierr = ISSetIdentity(is);  CHKERRA(ierr);
  ierr = ISPermutation(is,&flag); CHKERRA(ierr);
  if (flag != PETSC_TRUE) SETERRA(1,0,0);
  ierr = ISIdentity(is,&flag); CHKERRA(ierr);
  if (flag != PETSC_TRUE) SETERRA(1,0,0);

  /*
     Check equality of index sets 
  */
  ierr = ISEqual(is,is,&flag); CHKERRA(ierr);
  if (flag != PETSC_TRUE) SETERRA(1,0,0);

  /*
     Sorting 
  */
  ierr = ISSort(is); CHKERRA(ierr);
  ierr = ISSorted(is,&flag); CHKERRA(ierr);
  if (flag != PETSC_TRUE) SETERRA(1,0,0);

  /*
     Thinks it is a different type?
  */
  ierr = ISStride(is,&flag); CHKERRA(ierr);
  if (flag == PETSC_TRUE) SETERRA(1,0,0);
  ierr = ISBlock(is,&flag); CHKERRA(ierr);
  if (flag == PETSC_TRUE) SETERRA(1,0,0);

  ierr = ISDestroy(is); CHKERRA(ierr);

  /*
     Inverting permutation
  */
  for ( i=0; i<n; i++ ) {
    indices[i] = n - i - 1;
  }
  ierr = ISCreateGeneral(PETSC_COMM_SELF,n,indices,&is); CHKERRA(ierr);
  PetscFree(indices);
  ierr = ISSetPermutation(is); CHKERRA(ierr);
  ierr = ISInvertPermutation(is,&newis); CHKERRA(ierr);
  ierr = ISGetIndices(newis,&ii); CHKERRA(ierr);
  for ( i=0; i<n; i++ ) {
    if (ii[i] != n - i - 1) SETERRA(1,0,0);
  }
  ierr = ISRestoreIndices(newis,&ii); CHKERRA(ierr);
  ierr = ISDestroy(newis); CHKERRA(ierr);
  ierr = ISDestroy(is); CHKERRA(ierr);
  PetscFinalize();
  return 0;
}
 






