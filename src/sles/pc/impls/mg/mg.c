#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: mg.c,v 1.91 1999/04/08 17:53:58 bsmith Exp bsmith $";
#endif
/*
    Defines the multigrid preconditioner interface.
*/
#include "src/sles/pc/impls/mg/mgimpl.h"                    /*I "mg.h" I*/

/*
       MGMCycle_Private - Given an MG structure created with MGCreate() runs 
                  one multiplicative cycle down through the levels and
                  back up.

    Input Parameter:
.   mg - structure created with  MGCreate().
*/
#undef __FUNC__  
#define __FUNC__ "MGMCycle_Private"
int MGMCycle_Private(MG *mglevels)
{
  MG     mg = *mglevels, mgc = *(mglevels - 1);
  int    cycles = mg->cycles, ierr,its;
  Scalar zero = 0.0;

  PetscFunctionBegin;
  if (mg->level == 0) {
    ierr = SLESSolve(mg->smoothd,mg->b,mg->x,&its); CHKERRQ(ierr);
  } else {
    while (cycles--) {
      ierr = SLESSolve(mg->smoothd,mg->b,mg->x,&its); CHKERRQ(ierr);
      ierr = (*mg->residual)(mg->A, mg->b, mg->x, mg->r ); CHKERRQ(ierr);
      ierr = MatMult(mg->restrct,  mg->r, mgc->b ); CHKERRQ(ierr);
      ierr = VecSet(&zero,mgc->x); CHKERRQ(ierr);
      ierr = MGMCycle_Private(mglevels-1); CHKERRQ(ierr); 
      ierr = MatMultTransAdd(mg->interpolate,mgc->x,mg->x,mg->x); CHKERRQ(ierr);
      ierr = SLESSolve(mg->smoothu,mg->b,mg->x,&its);CHKERRQ(ierr); 
    }
  }
  PetscFunctionReturn(0);
}

/*
       MGCreate_Private - Creates a MG structure for use with the
               multigrid code. Level 0 is the coarsest. (But the 
               finest level is stored first in the array).

*/
#undef __FUNC__  
#define __FUNC__ "MGCreate_Private"
static int MGCreate_Private(MPI_Comm comm,int levels,PC pc,MG **result)
{
  MG   *mg;
  int  i,ierr;
  char *prefix;

  PetscFunctionBegin;
  mg = (MG *) PetscMalloc( levels*sizeof(MG) ); CHKPTRQ(mg);
  PLogObjectMemory(pc,levels*(sizeof(MG)+sizeof(struct _MG)));

  ierr = PCGetOptionsPrefix(pc,&prefix); CHKERRQ(ierr);

  for ( i=0; i<levels; i++ ) {
    mg[i]         = (MG) PetscMalloc( sizeof(struct _MG) ); CHKPTRQ(mg[i]);
    PetscMemzero(mg[i],sizeof(struct _MG));
    mg[i]->level  = i;
    mg[i]->levels = levels;
    mg[i]->cycles = 1;
    ierr = SLESCreate(comm,&mg[i]->smoothd); CHKERRQ(ierr);
    ierr = SLESSetOptionsPrefix(mg[i]->smoothd,prefix); CHKERRQ(ierr);
    if (i == 0) {
      ierr = SLESAppendOptionsPrefix(mg[0]->smoothd,"mg_coarse_"); CHKERRQ(ierr);
    } else {
      ierr = SLESAppendOptionsPrefix(mg[i]->smoothd,"mg_levels_"); CHKERRQ(ierr);
    }
    PLogObjectParent(pc,mg[i]->smoothd);
    mg[i]->smoothu         = mg[i]->smoothd;
    mg[i]->default_smoothu = 10000;
    mg[i]->default_smoothd = 10000;
  }
  *result = mg;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "PCDestroy_MG"
static int PCDestroy_MG(PC pc)
{
  MG  *mg = (MG *) pc->data;
  int i, n = mg[0]->levels,ierr;

  PetscFunctionBegin;
  for ( i=0; i<n; i++ ) {
    if (mg[i]->smoothd != mg[i]->smoothu) {
      ierr = SLESDestroy(mg[i]->smoothd); CHKERRQ(ierr);
    }
    ierr = SLESDestroy(mg[i]->smoothu); CHKERRQ(ierr);
    PetscFree(mg[i]);
  }
  PetscFree(mg);
  PetscFunctionReturn(0);
}



extern int MGACycle_Private(MG*);
extern int MGFCycle_Private(MG*);
extern int MGKCycle_Private(MG*);

/*
   MGCycle - Runs either an additive, multiplicative, Kaskadic
             or full cycle of multigrid. 

  Note: 
  A simple wrapper which calls MGMCycle(),MGACycle(), or MGFCycle(). 
*/ 
#undef __FUNC__  
#define __FUNC__ "MGCycle"
static int MGCycle(PC pc,Vec b,Vec x)
{
  MG     *mg = (MG*) pc->data;
  Scalar zero = 0.0;
  int    levels = mg[0]->levels,ierr;

  PetscFunctionBegin;
  mg[levels-1]->b = b; 
  mg[levels-1]->x = x;
  if (mg[0]->am == MGMULTIPLICATIVE) {
    ierr = VecSet(&zero,x);CHKERRQ(ierr);
    ierr = MGMCycle_Private(mg+levels-1);CHKERRQ(ierr);
  } 
  else if (mg[0]->am == MGADDITIVE) {
    ierr = MGACycle_Private(mg);CHKERRQ(ierr);
  }
  else if (mg[0]->am == MGKASKADE) {
    ierr = MGKCycle_Private(mg);CHKERRQ(ierr);
  }
  else {
    ierr = MGFCycle_Private(mg);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MGCycleRichardson"
static int MGCycleRichardson(PC pc,Vec b,Vec x,Vec w,int its)
{
  MG  *mg = (MG*) pc->data;
  int ierr,levels = mg[0]->levels;

  PetscFunctionBegin;
  mg[levels-1]->b = b; 
  mg[levels-1]->x = x;
  while (its--) {
    ierr = MGMCycle_Private(mg); CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "PCSetFromOptions_MG"
static int PCSetFromOptions_MG(PC pc)
{
  int    ierr, m,levels = 1,flg;
  char   buff[16];

  PetscFunctionBegin;
  if (!pc->data) {
    ierr = OptionsGetInt(pc->prefix,"-pc_mg_levels",&levels,&flg);CHKERRQ(ierr);
    ierr = MGSetLevels(pc,levels); CHKERRQ(ierr);
  }
  ierr = OptionsGetInt(pc->prefix,"-pc_mg_cycles",&m,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = MGSetCycles(pc,m);CHKERRQ(ierr);
  } 
  ierr = OptionsGetInt(pc->prefix,"-pc_mg_smoothup",&m,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = MGSetNumberSmoothUp(pc,m);CHKERRQ(ierr);
  }
  ierr = OptionsGetInt(pc->prefix,"-pc_mg_smoothdown",&m,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = MGSetNumberSmoothDown(pc,m);CHKERRQ(ierr);
  }
  ierr = OptionsGetString(pc->prefix,"-pc_mg_type",buff,15,&flg);CHKERRQ(ierr);
  if (flg) {
    MGType mg = MGADDITIVE;
    if      (!PetscStrcmp(buff,"additive"))       mg = MGADDITIVE;
    else if (!PetscStrcmp(buff,"multiplicative")) mg = MGMULTIPLICATIVE;
    else if (!PetscStrcmp(buff,"full"))           mg = MGFULL;
    else if (!PetscStrcmp(buff,"kaskade"))        mg = MGKASKADE;
    else if (!PetscStrcmp(buff,"cascade"))        mg = MGKASKADE;
    else SETERRQ1(PETSC_ERR_ARG_OUTOFRANGE,0,"Unknown type: %s",buff);
    ierr = MGSetType(pc,mg); CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "PCPrintHelp_MG"
static int PCPrintHelp_MG(PC pc,char *p)
{
  int ierr;

  PetscFunctionBegin;
  ierr = (*PetscHelpPrintf)(pc->comm," Options for PCMG preconditioner:\n");CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(pc->comm," %spc_mg_type [additive,multiplicative,fullmultigrid,kaskade]\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(pc->comm,"              type of multigrid method\n");CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(pc->comm," %spc_mg_smoothdown m: number of pre-smooths\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(pc->comm," %spc_mg_smoothup m: number of post-smooths\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(pc->comm," %spc_mg_cycles m: 1 for V-cycle, 2 for W-cycle\n",p);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "PCView_MG"
static int PCView_MG(PC pc,Viewer viewer)
{
  MG         *mg = (MG *) pc->data;
  KSP        kspu, kspd;
  int        itu, itd,ierr,levels = mg[0]->levels,i;
  double     dtol, atol, rtol;
  char       *cstring;
  ViewerType vtype;

  PetscFunctionBegin;
  ierr = ViewerGetType(viewer,&vtype); CHKERRQ(ierr);
  if (PetscTypeCompare(vtype,ASCII_VIEWER)) {
    ierr = SLESGetKSP(mg[0]->smoothu,&kspu); CHKERRQ(ierr);
    ierr = SLESGetKSP(mg[0]->smoothd,&kspd); CHKERRQ(ierr);
    ierr = KSPGetTolerances(kspu,&dtol,&atol,&rtol,&itu); CHKERRQ(ierr);
    ierr = KSPGetTolerances(kspd,&dtol,&atol,&rtol,&itd); CHKERRQ(ierr);
    if (mg[0]->am == MGMULTIPLICATIVE) cstring = "multiplicative";
    else if (mg[0]->am == MGADDITIVE)  cstring = "additive";
    else if (mg[0]->am == MGFULL)      cstring = "full";
    else if (mg[0]->am == MGKASKADE)   cstring = "Kaskade";
    else cstring = "unknown";
    ierr = ViewerASCIIPrintf(viewer,"  MG: type is %s, cycles=%d, pre-smooths=%d, post-smooths=%d\n",
                      cstring,mg[0]->cycles,mg[0]->default_smoothu,mg[0]->default_smoothd); CHKERRQ(ierr);
    for ( i=0; i<levels; i++ ) {
      ierr = ViewerASCIIPrintf(viewer,"Down solver on level %d -------------------------------\n",i);CHKERRQ(ierr);
      ierr = ViewerASCIIPushTab(viewer); CHKERRQ(ierr);
      ierr = SLESView(mg[i]->smoothd,viewer); CHKERRQ(ierr);
      ierr = ViewerASCIIPopTab(viewer); CHKERRQ(ierr);
      if (mg[i]->smoothd == mg[i]->smoothu) {
        ierr = ViewerASCIIPrintf(viewer,"Up solver same as down solver\n");CHKERRQ(ierr);
      } else {
        ierr = ViewerASCIIPushTab(viewer); CHKERRQ(ierr);
        ierr = SLESView(mg[i]->smoothu,viewer); CHKERRQ(ierr);
        ierr = ViewerASCIIPopTab(viewer); CHKERRQ(ierr);
      }
    }
  } else {
    SETERRQ(1,1,"Viewer type not supported for this object");
  }
  PetscFunctionReturn(0);
}

/*
    Calls setup for the SLES on each level
*/
#undef __FUNC__  
#define __FUNC__ "PCSetUp_MG"
static int PCSetUp_MG(PC pc)
{
  MG         *mg = (MG *) pc->data;
  int        ierr,i,n = mg[0]->levels;
  KSP        ksp;

  PetscFunctionBegin;
  /*
     temporarily stick pc->vec into mg[0]->b and x so that 
   SLESSetUp is happy. Since currently those slots are empty.
  */
  mg[n-1]->x = pc->vec;
  mg[n-1]->b = pc->vec;

  for ( i=1; i<n; i++ ) {
    if (mg[i]->smoothd) {
      ierr = SLESSetFromOptions(mg[i]->smoothd); CHKERRQ(ierr);
      ierr = SLESGetKSP(mg[i]->smoothd,&ksp); CHKERRQ(ierr);
      ierr = KSPSetInitialGuessNonzero(ksp); CHKERRQ(ierr);
      ierr = SLESSetUp(mg[i]->smoothd,mg[i]->b,mg[i]->x); CHKERRQ(ierr);
    }
  }
  for ( i=0; i<n; i++ ) {
    if (mg[i]->smoothu && mg[i]->smoothu != mg[i]->smoothd) {
      ierr = SLESSetFromOptions(mg[i]->smoothu); CHKERRQ(ierr);
      ierr = SLESGetKSP(mg[i]->smoothu,&ksp); CHKERRQ(ierr);
      ierr = KSPSetInitialGuessNonzero(ksp); CHKERRQ(ierr);
      ierr = SLESSetUp(mg[i]->smoothu,mg[i]->b,mg[i]->x); CHKERRQ(ierr);
    }
  }
  ierr = SLESSetFromOptions(mg[0]->smoothd); CHKERRQ(ierr);
  ierr = SLESSetUp(mg[0]->smoothd,mg[0]->b,mg[0]->x); CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------------------*/

#undef __FUNC__  
#define __FUNC__ "MGSetLevels"
/*@
   MGSetLevels - Sets the number of levels to use with MG.
   Must be called before any other MG routine.

   Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  levels - the number of levels

   Level: advanced

.keywords: MG, set, levels, multigrid

.seealso: MGSetType(), MGGetLevels()
@*/
int MGSetLevels(PC pc,int levels)
{
  int ierr;
  MG  *mg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_COOKIE);

  if (pc->data) {
    SETERRQ(1,1,"Number levels already set for MG\n\
    make sure that you call MGSetLevels() before SLESSetFromOptions()");
  }
  ierr          = MGCreate_Private(pc->comm,levels,pc,&mg); CHKERRQ(ierr);
  mg[0]->am     = MGMULTIPLICATIVE;
  pc->data      = (void *) mg;
  pc->applyrich = MGCycleRichardson;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MGGetLevels"
/*@
   MGGetLevels - Gets the number of levels to use with MG.

   Not Collective

   Input Parameter:
.  pc - the preconditioner context

   Output parameter:
.  levels - the number of levels

   Level: advanced

.keywords: MG, get, levels, multigrid

.seealso: MGSetLevels()
@*/
int MGGetLevels(PC pc,int *levels)
{
  MG  *mg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_COOKIE);

  mg      = (MG*) pc->data;
  *levels = mg[0]->levels;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MGSetType"
/*@
   MGSetType - Determines the form of multigrid to use:
   multiplicative, additive, full, or the Kaskade algorithm.

   Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  form - multigrid form, one of MGMULTIPLICATIVE, MGADDITIVE,
   MGFULL, MGKASKADE

   Options Database Key:
.  -pc_mg_type <form> - Sets <form>, one of multiplicative,
   additive, full, kaskade   

   Level: advanced

.keywords: MG, set, method, multiplicative, additive, full, Kaskade, multigrid

.seealso: MGSetLevels()
@*/
int MGSetType(PC pc,MGType form)
{
  MG *mg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_COOKIE);
  mg = (MG *) pc->data;

  mg[0]->am = form;
  if (form == MGMULTIPLICATIVE) pc->applyrich = MGCycleRichardson;
  else pc->applyrich = 0;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MGSetCycles"
/*@
   MGSetCycles - Sets the number of cycles to use. 1 denotes a
   V-cycle; 2 denotes a W-cycle. Use MGSetCyclesOnLevel() for more 
   complicated cycling.

   Collective on PC

   Input Parameters:
+  mg - the multigrid context 
-  n - the number of cycles

   Options Database Key:
$  -pc_mg_cycles n - Sets number of multigrid cycles

   Level: advanced

.keywords: MG, set, cycles, V-cycle, W-cycle, multigrid

.seealso: MGSetCyclesOnLevel()
@*/
int MGSetCycles(PC pc,int n)
{ 
  MG  *mg;
  int i,levels;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_COOKIE);
  mg     = (MG *) pc->data;
  levels = mg[0]->levels;

  for ( i=0; i<levels; i++ ) {  
    mg[i]->cycles  = n; 
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MGCheck"
/*@
   MGCheck - Checks that all components of the MG structure have 
   been set.

   Collective on PC

   Input Parameters:
.  mg - the MG structure

   Level: advanced

.keywords: MG, check, set, multigrid
@*/
int MGCheck(PC pc)
{
  MG  *mg;
  int i, n, count = 0;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_COOKIE);
  mg = (MG *) pc->data;

  if (!mg) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,1,"Must set MG levels before calling");

  n = mg[0]->levels;

  for (i=1; i<n; i++) {
    if (!mg[i]->restrct) {
      (*PetscErrorPrintf)("No restrict set level %d \n",n-i); count++;
    }    
    if (!mg[i]->interpolate) {
      (*PetscErrorPrintf)("No interpolate set level %d \n",n-i); count++;
    }
    if (!mg[i]->residual) {
      (*PetscErrorPrintf)("No residual set level %d \n",n-i); count++;
    }
    if (!mg[i]->smoothu) {
      (*PetscErrorPrintf)("No smoothup set level %d \n",n-i); count++;
    }  
    if (!mg[i]->smoothd) {
      (*PetscErrorPrintf)("No smoothdown set level %d \n",n-i); count++;
    }
    if (!mg[i]->r) {
      (*PetscErrorPrintf)("No r set level %d \n",n-i); count++;
    } 
    if (!mg[i-1]->x) {
      (*PetscErrorPrintf)("No x set level %d \n",n-i); count++;
    }
    if (!mg[i-1]->b) {
      (*PetscErrorPrintf)("No b set level %d \n",n-i); count++;
    }
  }
  PetscFunctionReturn(count);
}


#undef __FUNC__  
#define __FUNC__ "MGSetNumberSmoothDown"
/*@
   MGSetNumberSmoothDown - Sets the number of pre-smoothing steps to
   use on all levels. Use MGGetSmootherDown() to set different 
   pre-smoothing steps on different levels.

   Collective on PC

   Input Parameters:
+  mg - the multigrid context 
-  n - the number of smoothing steps

   Options Database Key:
.  -pc_mg_smoothdown <n> - Sets number of pre-smoothing steps

   Level: advanced

.keywords: MG, smooth, down, pre-smoothing, steps, multigrid

.seealso: MGSetNumberSmoothUp()
@*/
int MGSetNumberSmoothDown(PC pc,int n)
{ 
  MG  *mg;
  int i, levels,ierr;
  KSP ksp;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_COOKIE);
  mg     = (MG *) pc->data;
  levels = mg[0]->levels;

  for ( i=0; i<levels; i++ ) {  
    ierr = SLESGetKSP(mg[i]->smoothd,&ksp);CHKERRQ(ierr);
    ierr = KSPSetTolerances(ksp,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT,n);CHKERRQ(ierr);
    mg[i]->default_smoothd = n;
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MGSetNumberSmoothUp"
/*@
   MGSetNumberSmoothUp - Sets the number of post-smoothing steps to use 
   on all levels. Use MGGetSmootherUp() to set different numbers of 
   post-smoothing steps on different levels.

   Collective on PC

   Input Parameters:
+  mg - the multigrid context 
-  n - the number of smoothing steps

   Options Database Key:
.  -pc_mg_smoothup <n> - Sets number of post-smoothing steps

   Level: advanced

.keywords: MG, smooth, up, post-smoothing, steps, multigrid

.seealso: MGSetNumberSmoothDown()
@*/
int  MGSetNumberSmoothUp(PC pc,int n)
{ 
  MG  *mg;
  int i,levels,ierr;
  KSP ksp;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_COOKIE);
  mg     = (MG *) pc->data;
  levels = mg[0]->levels;

  for ( i=0; i<levels; i++ ) {  
    ierr = SLESGetKSP(mg[i]->smoothu,&ksp);CHKERRQ(ierr);
    ierr = KSPSetTolerances(ksp,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT,n);CHKERRQ(ierr);
    mg[i]->default_smoothu = n;
  }
  PetscFunctionReturn(0);
}

/* ----------------------------------------------------------------------------------------*/

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCCreate_MG"
int PCCreate_MG(PC pc)
{
  PetscFunctionBegin;
  pc->apply          = MGCycle;
  pc->setup          = PCSetUp_MG;
  pc->destroy        = PCDestroy_MG;
  pc->data           = (void *) 0;
  pc->setfromoptions = PCSetFromOptions_MG;
  pc->printhelp      = PCPrintHelp_MG;
  pc->view           = PCView_MG;
  PetscFunctionReturn(0);
}
EXTERN_C_END
