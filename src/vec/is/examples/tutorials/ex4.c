
/*      "$Id: ex4.c,v 1.3 1998/01/19 19:07:22 balay Exp bsmith $"; */

static char help[] = "Demonstrates using ISLocalToGlobalMappings.\n\n";

/*T
    Concepts: Local to global mappings, global to local mappings
    Routines: ISLocalToGlobalMappingCreate(); ISLocalToGlobalMappingApply()
    Routines: ISGlobalToLocalMappingApply(); ISLocalToGlobalMappingDestroy()

    Comment:  Creates an index set based on blocks of integers. Views that index set
    and then destroys it.
T*/

#include "is.h"

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **argv)
{
  int                    i, n = 4, ierr,indices[] = {0,3,9,12}, m = 2, input[] = {0,2};
  int                    output[2],inglobals[13],outlocals[13];
  ISLocalToGlobalMapping mapping;

  ierr = PetscInitialize(&argc,&argv,(char*)0,help);

  /*
      Create a local to global mapping. Each processor independently
     creates a mapping  
  */
  ierr = ISLocalToGlobalMappingCreate(PETSC_COMM_WORLD,n,indices,&mapping); CHKERRA(ierr);

  /*
     Map a set of local indices to their global values 
  */
  ierr = ISLocalToGlobalMappingApply(mapping,m,input,output); CHKERRA(ierr);
  ierr = PetscIntView(m,output,VIEWER_STDOUT_SELF); CHKERRA(ierr);
  
  /*
     Map some global indices to local, retaining the ones without a local index by -1
  */
  for ( i=0; i<13; i++ ) {
    inglobals[i] = i;
  }
  ierr = ISGlobalToLocalMappingApply(mapping,IS_GTOLM_MASK,13,inglobals,PETSC_NULL,outlocals);
         CHKERRA(ierr);
  ierr = PetscIntView(13,outlocals,VIEWER_STDOUT_SELF); CHKERRA(ierr);

  /*
     Map some global indices to local, dropping the ones without a local index.
  */
  ierr = ISGlobalToLocalMappingApply(mapping,IS_GTOLM_DROP,13,inglobals,&m,outlocals);
         CHKERRA(ierr);
  ierr = PetscIntView(m,outlocals,VIEWER_STDOUT_SELF); CHKERRA(ierr);

  /*
     Free the space used by the local to global mapping
  */
  ierr = ISLocalToGlobalMappingDestroy(mapping); CHKERRA(ierr);


  PetscFinalize();
  return 0;
}


