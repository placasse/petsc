#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: mfregis.c,v 1.3 1999/03/17 23:24:23 bsmith Exp bsmith $";
#endif

#include "src/snes/mf/snesmfj.h"   /*I  "snes.h"   I*/

EXTERN_C_BEGIN
extern int MatSNESMFCreate_Default(MatSNESMFCtx);
extern int MatSNESMFCreate_WP(MatSNESMFCtx);
EXTERN_C_END

#undef __FUNC__  
#define __FUNC__ "MatSNESMFRegisterAll"
/*@C
  MatSNESMFRegisterAll - Registers all of the compute-h in the MatSNESMF package.

  Not Collective

  Level: developer

.keywords: MatSNESMF, register, all

.seealso:  MatSNESMFRegisterDestroy(), MatSNESMFRegister(), MatSNESMFCreate(), 
           MatSNESMFSetType()
@*/
int MatSNESMFRegisterAll(char *path)
{
  int ierr;

  PetscFunctionBegin;
  MatSNESMFRegisterAllCalled = 1;

  ierr = MatSNESMFRegister("default",path,"MatSNESMFCreate_Default",MatSNESMFCreate_Default);CHKERRQ(ierr);
  ierr = MatSNESMFRegister("wp",path,"MatSNESMFCreate_WP",MatSNESMFCreate_WP);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

