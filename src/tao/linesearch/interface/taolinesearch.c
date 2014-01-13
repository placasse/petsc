#include "taosolver.h"   
#include "taolinesearch.h" /*I "taolinesearch.h" I*/
#include "tao-private/taolinesearch_impl.h"

PetscBool TaoLineSearchInitialized = PETSC_FALSE;
PetscFunctionList TaoLineSearchList              = PETSC_NULL;

PetscClassId TAOLINESEARCH_CLASSID=0;
PetscLogEvent TaoLineSearch_ApplyEvent = 0, TaoLineSearch_EvalEvent=0;

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchView"
/*@C
  TaoLineSearchView - Prints information about the TaoLineSearch
 
  Collective on TaoLineSearch

  InputParameters:
+ ls - the TaoSolver context
- viewer - visualization context

  Options Database Key:
. -tao_ls_view - Calls TaoLineSearchView() at the end of each line search

  Notes:
  The available visualization contexts include
+     PETSC_VIEWER_STDOUT_SELF - standard output (default)
-     PETSC_VIEWER_STDOUT_WORLD - synchronized standard
         output where only the first processor opens
         the file.  All other processors send their 
         data to the first processor to print. 

  Level: beginner

.seealso: PetscViewerASCIIOpen()
@*/

PetscErrorCode TaoLineSearchView(TaoLineSearch ls, PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscBool isascii, isstring;
  const TaoLineSearchType type;
  PetscBool destroyviewer = PETSC_FALSE;
  PetscFunctionBegin;
  PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
  if (!viewer) {
    destroyviewer = PETSC_TRUE;
    ierr = PetscViewerASCIIGetStdout(((PetscObject)ls)->comm, &viewer); CHKERRQ(ierr);
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,2);
  PetscCheckSameComm(ls,1,viewer,2);

  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&isascii);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERSTRING,&isstring);CHKERRQ(ierr);
  if (isascii) {
    if (((PetscObject)ls)->prefix) {
      ierr = PetscViewerASCIIPrintf(viewer,"TaoLineSearch Object:(%s)\n",((PetscObject)ls)->prefix);CHKERRQ(ierr);
    } else {
      ierr = PetscViewerASCIIPrintf(viewer,"TaoLineSearch Object:\n");CHKERRQ(ierr);
    }
    ierr = PetscViewerASCIIPushTab(viewer); CHKERRQ(ierr);
    ierr = TaoLineSearchGetType(ls,&type);CHKERRQ(ierr);
    if (type) {
      ierr = PetscViewerASCIIPrintf(viewer,"type: %s\n",type);CHKERRQ(ierr);
    } else {
      ierr = PetscViewerASCIIPrintf(viewer,"type: not set yet\n");CHKERRQ(ierr);
    }
    if (ls->ops->view) {
      ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
      ierr = (*ls->ops->view)(ls,viewer);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
    }
    ierr = PetscViewerASCIIPrintf(viewer,"maximum function evaluations=%D\n",ls->max_funcs);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"tolerances: ftol=%G, rtol=%G, gtol=%G\n", ls->ftol, ls->rtol,ls->gtol);CHKERRQ(ierr); 
    ierr = PetscViewerASCIIPrintf(viewer,"total number of function evaluations=%D\n",ls->nfeval);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"total number of gradient evaluations=%D\n",ls->ngeval);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"total number of function/gradient evaluations=%D\n",ls->nfgeval);CHKERRQ(ierr);

    if (ls->bounded) {
      ierr = PetscViewerASCIIPrintf(viewer,"using variable bounds\n");CHKERRQ(ierr);
    }
    ierr = PetscViewerASCIIPrintf(viewer,"Termination reason: %D\n",(int)ls->reason); CHKERRQ(ierr);
    ierr = PetscViewerASCIIPopTab(viewer); CHKERRQ(ierr);

  } else if (isstring) {
    ierr = TaoLineSearchGetType(ls,&type);CHKERRQ(ierr);
    ierr = PetscViewerStringSPrintf(viewer," %-3.3s",type);CHKERRQ(ierr);
  }
  if (destroyviewer) {
    ierr = PetscViewerDestroy(&viewer); CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);

}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchCreate"
/*@C
  TaoLineSearchCreate - Creates a TAO Line Search object.  Algorithms in TAO that use 
  line-searches will automatically create one.

  Collective on MPI_Comm

  Input Parameter:
. comm - MPI communicator

  Output Parameter:
. newls - the new TaoLineSearch context

  Available methods include:
+ more-thuente
. gpcg 
- unit - Do not perform any line search


   Options Database Keys:
.   -tao_ls_type - select which method TAO should use

   Level: beginner

.seealso: TaoLineSearchSetType(), TaoLineSearchApply(), TaoLineSearchDestroy()
@*/

PetscErrorCode TaoLineSearchCreate(MPI_Comm comm, TaoLineSearch *newls)
{
     PetscErrorCode ierr;
     TaoLineSearch ls;

     PetscFunctionBegin;
     PetscValidPointer(newls,2);
     *newls = PETSC_NULL;

 #ifndef PETSC_USE_DYNAMIC_LIBRARIES
     ierr = TaoLineSearchInitializePackage(); CHKERRQ(ierr);
 #endif 

     ierr = PetscHeaderCreate(ls,_p_TaoLineSearch,struct _TaoLineSearchOps,
			      TAOLINESEARCH_CLASSID, "TaoLineSearch",0,0,
			      comm,TaoLineSearchDestroy, TaoLineSearchView);
     CHKERRQ(ierr);
     ls->bounded = 0;
     ls->max_funcs=30;
     ls->ftol = 0.0001;
     ls->gtol = 0.9;
     ls->rtol = 1.0e-10;
     ls->stepmin=1.0e-20;
     ls->stepmax=1.0e+20;
     ls->step=1.0;
     ls->nfeval=0;
     ls->ngeval=0;
     ls->nfgeval=0;

     ls->ops->computeobjective=0;
     ls->ops->computegradient=0;
     ls->ops->computeobjectiveandgradient=0;
     ls->ops->computeobjectiveandgts=0;
     ls->ops->setup=0;
     ls->ops->apply=0;
     ls->ops->view=0;
     ls->ops->setfromoptions=0;
     ls->ops->reset=0;
     ls->ops->destroy=0;
     ls->setupcalled=PETSC_FALSE;
     ls->usetaoroutines=PETSC_FALSE;
     *newls = ls;
     PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetUp"
/*@ 
  TaoLineSearchSetUp - Sets up the internal data structures for the later use
  of a Tao solver

  Collective on ls
  
  Input Parameters:
. ls - the TaoLineSearch context

  Notes:
  The user will not need to explicitly call TaoLineSearchSetUp(), as it will 
  automatically be called in TaoLineSearchSolve().  However, if the user
  desires to call it explicitly, it should come after TaoLineSearchCreate()
  but before TaoLineSearchApply().

  Level: developer

.seealso: TaoLineSearchCreate(), TaoLineSearchApply()
@*/

PetscErrorCode TaoLineSearchSetUp(TaoLineSearch ls)
{
     PetscErrorCode ierr;
     const char *default_type=TAOLINESEARCH_MT;
     PetscBool flg;
     PetscFunctionBegin;
     PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
     if (ls->setupcalled) PetscFunctionReturn(0);
     if (!((PetscObject)ls)->type_name) {
	 ierr = TaoLineSearchSetType(ls,default_type); CHKERRQ(ierr);
     }
     if (ls->ops->setup) {
	 ierr = (*ls->ops->setup)(ls); CHKERRQ(ierr);
     }
     if (ls->usetaoroutines) {
       ierr = TaoIsObjectiveDefined(ls->taosolver,&flg); CHKERRQ(ierr);
       ls->hasobjective = flg;
       ierr = TaoIsGradientDefined(ls->taosolver,&flg); CHKERRQ(ierr);
       ls->hasgradient = flg;
       ierr = TaoIsObjectiveAndGradientDefined(ls->taosolver,&flg); CHKERRQ(ierr);
       ls->hasobjectiveandgradient = flg;
     } else {
       if (ls->ops->computeobjective) {
	 ls->hasobjective = PETSC_TRUE;
       } else {
	 ls->hasobjective = PETSC_FALSE;
       }
       if (ls->ops->computegradient) {
	 ls->hasgradient = PETSC_TRUE;
       } else {
	 ls->hasgradient = PETSC_FALSE;
       }
       if (ls->ops->computeobjectiveandgradient) {
	 ls->hasobjectiveandgradient = PETSC_TRUE;
       } else {
	 ls->hasobjectiveandgradient = PETSC_FALSE;
       }
     }
     ls->setupcalled = PETSC_TRUE;
     PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchReset"
/*@ 
  TaoLineSearchReset - Some line searches may carry state information
  from one TaoLineSearchApply() to the next.  This function resets this 
  state information.

  Collective on TaoLineSearch

  Input Parameter:
. ls - the TaoLineSearch context

  Level: developer

.seealso: TaoLineSearchCreate(), TaoLineSearchApply()
@*/
PetscErrorCode TaoLineSearchReset(TaoLineSearch ls)
{
     PetscErrorCode ierr;
     PetscFunctionBegin;
     PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
     if (ls->ops->reset) {
       ierr= (*ls->ops->reset)(ls); CHKERRQ(ierr);
     }
     PetscFunctionReturn(0);
}
#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchDestroy"
/*@ 
  TaoLineSearchDestroy - Destroys the TAO context that was created with 
  TaoLineSearchCreate()

  Collective on TaoLineSearch

  Input Parameter
. ls - the TaoLineSearch context

  Level: beginner

.seealse: TaoLineSearchCreate(), TaoLineSearchSolve()
@*/
PetscErrorCode TaoLineSearchDestroy(TaoLineSearch *ls)
{
     PetscErrorCode ierr;
     PetscFunctionBegin;
     if (!*ls) PetscFunctionReturn(0);
     PetscValidHeaderSpecific(*ls,TAOLINESEARCH_CLASSID,1);
     if (--((PetscObject)*ls)->refct > 0) {*ls=0; PetscFunctionReturn(0);}
     if ((*ls)->stepdirection!=PETSC_NULL) {
       ierr = PetscObjectDereference((PetscObject)(*ls)->stepdirection);CHKERRQ(ierr);
       (*ls)->stepdirection = PETSC_NULL;
     }
     if ((*ls)->start_x != PETSC_NULL) {
       ierr = PetscObjectDereference((PetscObject)(*ls)->start_x); CHKERRQ(ierr);
       (*ls)->start_x = PETSC_NULL;
     }
     if ((*ls)->ops->destroy) {
       ierr = (*(*ls)->ops->destroy)(*ls); CHKERRQ(ierr);
     }
     ierr = PetscHeaderDestroy(ls); CHKERRQ(ierr);
     PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchApply"
/*@ 
  TaoLineSearchApply - Performs a line-search in a given step direction.  Criteria for acceptable step length depends on the line-search algorithm chosen

  Collective on TaoLineSearch
  
  Input Parameters:
+ ls - the TaoSolver context
. x - The current solution (on output x contains the new solution determined by the line search)
. f - objective function value at current solution (on output contains the objective function value at new solution)
. g - gradient evaluated at x (on output contains the gradient at new solution)
- s - search direction

  Output Parameters:
+ x - new solution
. f - objective function value at x
. g - gradient vector at x
. steplength - scalar multiplier of s used ( x = x0 + steplength * x )
- reason - reason why the line-search stopped

  reason will be set to one of:

+ TAOLINESEARCH_FAILED_ASCENT - initial line search step * g is not descent direction
. TAOLINESEARCH_FAILED_INFORNAN - function evaluation gives Inf or Nan value
. TAOLINESEARCH_FAILED_BADPARAMETER - negative value set as parameter
. TAOLINESEARCH_HALTED_MAXFCN - maximum number of function evaluation reached
. TAOLINESEARCH_HALTED_UPPERBOUND - step is at upper bound
. TAOLINESEARCH_HALTED_LOWERBOUND - step is at lower bound
. TAOLINESEARCH_HALTED_RTOL - range of uncertainty is smaller than given tolerance
. TAOLINESEARCH_HALTED_USER - user can set this reason to stop line search
. TAOLINESEARCH_HALTED_OTHER - any other reason
- TAOLINESEARCH_SUCCESS - successful line search


  Note:
  The algorithm developer must set up the TaoLineSearch with calls to 
  TaoLineSearchSetObjectiveRoutine() and TaoLineSearchSetGradientRoutine(), TaoLineSearchSetObjectiveAndGradientRoutine(), or TaoLineSearchUseTaoSolverRoutines()
  
  Note:
  You may or may not need to follow this with a call to 
  TaoAddLineSearchCounts(), depending on whether you want these
  evaluations to count toward the total function/gradient evaluations.

  Level: beginner

  .seealso: TaoLineSearchCreate(), TaoLineSearchSetType(), TaoLineSearchSetInitialStepLength(), TaoAddLineSearchCounts()
 @*/

PetscErrorCode TaoLineSearchApply(TaoLineSearch ls, Vec x, PetscReal *f, Vec g, Vec s, PetscReal *steplength, TaoLineSearchTerminationReason *reason)
{
     PetscErrorCode ierr;
     PetscViewer viewer;
     PetscInt low1,low2,low3,high1,high2,high3;
     PetscBool flg;
     char filename[PETSC_MAX_PATH_LEN];

     PetscFunctionBegin;
     *reason = TAOLINESEARCH_CONTINUE_ITERATING;
     PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
     PetscValidHeaderSpecific(x,VEC_CLASSID,2);
     PetscValidScalarPointer(f,3);
     PetscValidHeaderSpecific(g,VEC_CLASSID,4);
     PetscValidHeaderSpecific(s,VEC_CLASSID,5);
     PetscValidPointer(reason,7);
     PetscCheckSameComm(ls,1,x,2);
     PetscCheckSameTypeAndComm(x,2,g,4);
     PetscCheckSameTypeAndComm(x,2,s,5);
     ierr = VecGetOwnershipRange(x, &low1, &high1); CHKERRQ(ierr);
     ierr = VecGetOwnershipRange(g, &low2, &high2); CHKERRQ(ierr);
     ierr = VecGetOwnershipRange(s, &low3, &high3); CHKERRQ(ierr);
     if ( low1!= low2 || low1!= low3 || high1!= high2 || high1!= high3) {
       SETERRQ(PETSC_COMM_SELF,1,"InCompatible vector local lengths");
     }


     if (ls->stepdirection) {
       ierr = PetscObjectDereference((PetscObject)(s)); CHKERRQ(ierr);
     }
     ls->stepdirection = s;
     ierr = PetscObjectReference((PetscObject)s); CHKERRQ(ierr);

     ierr = TaoLineSearchSetUp(ls); CHKERRQ(ierr);
     if (!ls->ops->apply) {
	 SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Line Search Object does not have 'apply' routine");
     }
     ls->nfeval=0;
     ls->ngeval=0;
     ls->nfgeval=0;
     /* Check parameter values */
     if (ls->ftol < 0.0) {
       ierr = PetscInfo1(ls,"Bad Line Search Parameter: ftol (%G) < 0\n",ls->ftol); CHKERRQ(ierr);
       *reason=TAOLINESEARCH_FAILED_BADPARAMETER;
     }
     if (ls->rtol < 0.0) {
       ierr = PetscInfo1(ls,"Bad Line Search Parameter: rtol (%G) < 0\n",ls->rtol); CHKERRQ(ierr);
       *reason=TAOLINESEARCH_FAILED_BADPARAMETER;
     }      

     if (ls->gtol < 0.0) {
       ierr = PetscInfo1(ls,"Bad Line Search Parameter: gtol (%G) < 0\n",ls->gtol); CHKERRQ(ierr);
       *reason=TAOLINESEARCH_FAILED_BADPARAMETER;
     }      
     if (ls->stepmin < 0.0) {
       ierr = PetscInfo1(ls,"Bad Line Search Parameter: stepmin (%G) < 0\n",ls->stepmin); CHKERRQ(ierr);
       *reason=TAOLINESEARCH_FAILED_BADPARAMETER;
     }      
     if (ls->stepmax < ls->stepmin) {
       ierr = PetscInfo2(ls,"Bad Line Search Parameter: stepmin (%G) > stepmax (%G)\n",ls->stepmin,ls->stepmax); CHKERRQ(ierr);
       *reason=TAOLINESEARCH_FAILED_BADPARAMETER;
     }      
     if (ls->max_funcs < 0) {
       ierr = PetscInfo1(ls,"Bad Line Search Parameter: max_funcs (%D) < 0\n",ls->max_funcs); CHKERRQ(ierr);
       *reason=TAOLINESEARCH_FAILED_BADPARAMETER;
     }      
     if (PetscIsInfOrNanReal(*f)) {
       ierr = PetscInfo1(ls,"Initial Line Search Function Value is Inf or Nan (%G)\n",*f); CHKERRQ(ierr);
       *reason=TAOLINESEARCH_FAILED_INFORNAN;
     }


     if (x != ls->start_x) {
	 ierr = PetscObjectReference((PetscObject)x);
	 if (ls->start_x) {
	     ierr = VecDestroy(&ls->start_x); CHKERRQ(ierr);
	 }
	 ls->start_x = x;
     }




     ierr = PetscLogEventBegin(TaoLineSearch_ApplyEvent,ls,0,0,0); CHKERRQ(ierr);
     ierr = (*ls->ops->apply)(ls,x,f,g,s); CHKERRQ(ierr);
     ierr = PetscLogEventEnd(TaoLineSearch_ApplyEvent, ls, 0,0,0); CHKERRQ(ierr);
     *reason=ls->reason;
     ls->new_f = *f;

     if (steplength) { 
       *steplength=ls->step;
     }

     ierr = PetscOptionsGetString(((PetscObject)ls)->prefix,"-tao_ls_view",filename,PETSC_MAX_PATH_LEN,&flg);CHKERRQ(ierr);
     if (ls->viewls && !PetscPreLoadingOn) {
	 ierr = PetscViewerASCIIOpen(((PetscObject)ls)->comm,filename,&viewer); CHKERRQ(ierr);
	 ierr = TaoLineSearchView(ls,viewer); CHKERRQ(ierr);
	 ierr = PetscViewerDestroy(&viewer); CHKERRQ(ierr);
     }
     PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetType"
/*@C
   TaoLineSearchSetType - Sets the algorithm used in a line search

   Collective on TaoLineSearch

   Input Parameters:
+  ls - the TaoLineSearch context
-  type - a known method


  Available methods include:
+ more-thuente
. gpcg 
- unit - Do not perform any line search


  Options Database Keys:
.   -tao_ls_type - select which method TAO should use

  Level: beginner
  

.seealso: TaoLineSearchCreate(), TaoLineSearchGetType(), TaoLineSearchApply()

@*/

PetscErrorCode TaoLineSearchSetType(TaoLineSearch ls, const TaoLineSearchType type)
{
     PetscErrorCode ierr;
     PetscErrorCode (*r)(TaoLineSearch);
     PetscBool flg;

     PetscFunctionBegin;
     PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
     PetscValidCharPointer(type,2);
     ierr = PetscObjectTypeCompare((PetscObject)ls, type, &flg); CHKERRQ(ierr);
     if (flg) PetscFunctionReturn(0);

     ierr = PetscFunctionListFind(TaoLineSearchList,type, (void (**)(void)) &r); CHKERRQ(ierr);
     if (!r) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"Unable to find requested TaoLineSearch type %s",type);
     if (ls->ops->destroy) {
	 ierr = (*(ls)->ops->destroy)(ls); CHKERRQ(ierr);
     }
     ls->max_funcs=30;
     ls->ftol = 0.0001;
     ls->gtol = 0.9;
     ls->rtol = 1.0e-10;
     ls->stepmin=1.0e-20;
     ls->stepmax=1.0e+20;


     ls->nfeval=0;
     ls->ngeval=0;
     ls->nfgeval=0;
     ls->ops->setup=0;
     ls->ops->apply=0;
     ls->ops->view=0;
     ls->ops->setfromoptions=0;
     ls->ops->destroy=0;
     ls->setupcalled = PETSC_FALSE;
     ierr = (*r)(ls); CHKERRQ(ierr);
     ierr = PetscObjectChangeTypeName((PetscObject)ls, type); CHKERRQ(ierr);
     PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetFromOptions"
/*@
  TaoLineSearchSetFromOptions - Sets various TaoLineSearch parameters from user
  options.

  Collective on TaoLineSearch

  Input Paremeter:
. ls - the TaoLineSearch context

  Options Database Keys:
+ -tao_ls_type <type> - The algorithm that TAO uses (more-thuente, gpcg, unit)
. -tao_ls_ftol <tol> - tolerance for sufficient decrease
. -tao_ls_gtol <tol> - tolerance for curvature condition
. -tao_ls_rtol <tol> - relative tolerance for acceptable step
. -tao_ls_stepmin <step> - minimum steplength allowed
. -tao_ls_stepmax <step> - maximum steplength allowed
. -tao_ls_max_funcs <n> - maximum number of function evaluations allowed
- -tao_ls_view - display line-search results to standard output

  Level: beginner
@*/
PetscErrorCode TaoLineSearchSetFromOptions(TaoLineSearch ls)
{
   PetscErrorCode ierr;

   const char *default_type=TAOLINESEARCH_MT;
   char type[256];
   PetscBool flg;
   PetscFunctionBegin;
   PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);

   ierr = PetscObjectOptionsBegin((PetscObject)ls); CHKERRQ(ierr);
   {
     if (!TaoLineSearchInitialized) {
       ierr = TaoLineSearchInitializePackage(); CHKERRQ(ierr);
     }
     if (((PetscObject)ls)->type_name) {
       default_type = ((PetscObject)ls)->type_name;
     }
     /* Check for type from options */
     ierr = PetscOptionsFList("-tao_ls_type","Tao Line Search type","TaoLineSearchSetType",TaoLineSearchList,default_type,type,256,&flg); CHKERRQ(ierr);
     if (flg) {
       ierr = TaoLineSearchSetType(ls,type); CHKERRQ(ierr);
     } else if (!((PetscObject)ls)->type_name) {
       ierr = TaoLineSearchSetType(ls,default_type);
     }

     ierr = PetscOptionsInt("-tao_ls_max_funcs","max function evals in line search",
			  "",ls->max_funcs,&ls->max_funcs,0);CHKERRQ(ierr);
     ierr = PetscOptionsReal("-tao_ls_ftol","tol for sufficient decrease","",
			   ls->ftol,&ls->ftol,0);CHKERRQ(ierr);
     ierr = PetscOptionsReal("-tao_ls_gtol","tol for curvature condition","",
			   ls->gtol,&ls->gtol,0);CHKERRQ(ierr);
     ierr = PetscOptionsReal("-tao_ls_rtol","relative tol for acceptable step","",
			   ls->rtol,&ls->rtol,0);CHKERRQ(ierr);
     ierr = PetscOptionsReal("-tao_ls_stepmin","lower bound for step","",
			   ls->stepmin,&ls->stepmin,0);CHKERRQ(ierr);
     ierr = PetscOptionsReal("-tao_ls_stepmax","upper bound for step","",
			   ls->stepmax,&ls->stepmax,0);CHKERRQ(ierr);
     ierr = PetscOptionsBool("-tao_ls_view","view TaoLineSearch info after each line search has completed","TaoLineSearchView",PETSC_FALSE,&ls->viewls,PETSC_NULL);CHKERRQ(ierr);

     if (ls->ops->setfromoptions) {
       ierr = (*ls->ops->setfromoptions)(ls); CHKERRQ(ierr);
     }
   }
   ierr = PetscOptionsEnd(); CHKERRQ(ierr);


   PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetType"
/*@C
  TaoLineSearchGetType - Gets the current line search algorithm

  Not Collective

  Input Parameter:
. ls - the TaoLineSearch context

  Output Paramter:
. type - the line search algorithm in effect

  Level: developer

@*/
PetscErrorCode TaoLineSearchGetType(TaoLineSearch ls, const TaoLineSearchType *type)
{
     PetscFunctionBegin;
     PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
     PetscValidPointer(type,2);
     *type = ((PetscObject)ls)->type_name;
     PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetNumberFunctionEvaluations"
/*@
  TaoLineSearchGetNumberFunctionEvaluations - Gets the number of function and gradient evaluation 
  routines used by the line search in last application (not cumulative).
  
  Not Collective

  Input Parameter:
. ls - the TaoLineSearch context

  Output Parameters:
+ nfeval   - number of function evaluations 
. ngeval   - number of gradient evaluations 
- nfgeval  - number of function/gradient evaluations 

  Level: intermediate

  Note:
  If the line search is using the TaoSolver objective and gradient
  routines directly (see TaoLineSearchUseTaoSolverRoutines()), then TAO 
  is already counting the number of evaluations.  

@*/
PetscErrorCode TaoLineSearchGetNumberFunctionEvaluations(TaoLineSearch ls, PetscInt *nfeval, PetscInt *ngeval, PetscInt *nfgeval)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
  *nfeval = ls->nfeval;
  *ngeval = ls->ngeval;
  *nfgeval = ls->nfgeval;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchIsUsingTaoSolverRoutines"
/*@ 
  TaoLineSearchIsUsingTaoSolverRoutines - Checks whether the line search is using
  TaoSolver evaluation routines.
 
  Not Collective

  Input Parameter:
. ls - the TaoLineSearch context

  Output Parameter:
. flg - PETSC_TRUE if the line search is using TaoSolver evaluation routines,
        otherwise PETSC_FALSE

  Level: developer
@*/
PetscErrorCode TaoLineSearchIsUsingTaoSolverRoutines(TaoLineSearch ls, PetscBool *flg)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
  *flg = (ls->usetaoroutines);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetObjectiveRoutine"
/*@C
  TaoLineSearchSetObjectiveRoutine - Sets the function evaluation routine for the line search

  Logically Collective on TaoLineSearch

  Input Parameter:
+ ls - the TaoLineSearch context
. func - the objective function evaluation routine
- ctx - the (optional) user-defined context for private data

  Calling sequence of func:
$      func (TaoLinesearch ls, Vec x, PetscReal *f, void *ctx);

+ x - input vector
. f - function value
- ctx (optional) user-defined context

  Level: beginner

  Note:
  Use this routine only if you want the line search objective 
  evaluation routine to be different from the TaoSolver's objective
  evaluation routine. If you use this routine you must also set
  the line search gradient and/or function/gradient routine.
  
  Note:
  Some algorithms (lcl, gpcg) set their own objective routine for the 
  line search, application programmers should be wary of overriding the
  default objective routine.

.seealso: TaoLineSearchCreate(), TaoLineSearchSetGradientRoutine(), TaoLineSearchSetObjectiveAndGradientRoutine(), TaoLineSearchUseTaoSolverRoutines()
@*/
PetscErrorCode TaoLineSearchSetObjectiveRoutine(TaoLineSearch ls, PetscErrorCode(*func)(TaoLineSearch ls, Vec x, PetscReal*, void*), void *ctx)
{
     PetscFunctionBegin;
     PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);

     ls->ops->computeobjective=func;
     if (ctx) ls->userctx_func=ctx;
     ls->usetaoroutines=PETSC_FALSE;
     PetscFunctionReturn(0);


}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetGradientRoutine"
/*@C
  TaoLineSearchSetGradientRoutine - Sets the gradient evaluation routine for the line search

  Logically Collective on TaoLineSearch

  Input Parameter:
+ ls - the TaoLineSearch context
. func - the gradient evaluation routine
- ctx - the (optional) user-defined context for private data

  Calling sequence of func:
$      func (TaoLinesearch ls, Vec x, Vec g, void *ctx);

+ x - input vector
. g - gradient vector
- ctx (optional) user-defined context

  Level: beginner

  Note:
  Use this routine only if you want the line search gradient
  evaluation routine to be different from the TaoSolver's gradient
  evaluation routine. If you use this routine you must also set
  the line search function and/or function/gradient routine.

  Note:
  Some algorithms (lcl, gpcg) set their own gradient routine for the 
  line search, application programmers should be wary of overriding the
  default gradient routine.

.seealso: TaoLineSearchCreate(), TaoLineSearchSetObjectiveRoutine(), TaoLineSearchSetObjectiveAndGradientRoutine(), TaoLineSearchUseTaoSolverRoutines()
@*/
PetscErrorCode TaoLineSearchSetGradientRoutine(TaoLineSearch ls, PetscErrorCode(*func)(TaoLineSearch ls, Vec x, Vec g, void*), void *ctx)
{
     PetscFunctionBegin;
     PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);

     ls->ops->computegradient=func;
     if (ctx) ls->userctx_grad=ctx;
     ls->usetaoroutines=PETSC_FALSE;
     PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetObjectiveAndGradientRoutine"
/*@C
  TaoLineSearchSetObjectiveAndGradientRoutine - Sets the objective/gradient evaluation routine for the line search

  Logically Collective on TaoLineSearch

  Input Parameter:
+ ls - the TaoLineSearch context
. func - the objective and gradient evaluation routine
- ctx - the (optional) user-defined context for private data

  Calling sequence of func:
$      func (TaoLinesearch ls, Vec x, PetscReal *f, Vec g, void *ctx);

+ x - input vector
. f - function value
. g - gradient vector
- ctx (optional) user-defined context

  Level: beginner

  Note:
  Use this routine only if you want the line search objective and gradient 
  evaluation routines to be different from the TaoSolver's objective
  and gradient evaluation routines.

  Note:
  Some algorithms (lcl, gpcg) set their own objective routine for the 
  line search, application programmers should be wary of overriding the
  default objective routine.

.seealso: TaoLineSearchCreate(), TaoLineSearchSetObjectiveRoutine(), TaoLineSearchSetGradientRoutine(), TaoLineSearchUseTaoSolverRoutines()
@*/
PetscErrorCode TaoLineSearchSetObjectiveAndGradientRoutine(TaoLineSearch ls, PetscErrorCode(*func)(TaoLineSearch ls, Vec x, PetscReal *, Vec g, void*), void *ctx)
{
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    
    ls->ops->computeobjectiveandgradient=func;
    if (ctx) ls->userctx_funcgrad=ctx;
    ls->usetaoroutines = PETSC_FALSE;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetObjectiveAndGTSRoutine"
/*@C
  TaoLineSearchSetObjectiveAndGTSRoutine - Sets the objective and 
  (gradient'*stepdirection) evaluation routine for the line search. 
  Sometimes it is more efficient to compute the inner product of the gradient
  and the step direction than it is to compute the gradient, and this is all 
  the line search typically needs of the gradient.

  Logically Collective on TaoLineSearch

  Input Parameter:
+ ls - the TaoLineSearch context
. func - the objective and gradient evaluation routine
- ctx - the (optional) user-defined context for private data

  Calling sequence of func:
$      func (TaoLinesearch ls, Vec x, PetscReal *f, PetscReal *gts, void *ctx);

+ x - input vector
. s - step direction
. f - function value
. gts - inner product of gradient and step direction vectors
- ctx (optional) user-defined context

  Note: The gradient will still need to be computed at the end of the line 
  search, so you will still need to set a line search gradient evaluation 
  routine

  Note: Bounded line searches (those used in bounded optimization algorithms) 
  don't use g's directly, but rather (g'x - g'x0)/steplength.  You can get the
  x0 and steplength with TaoLineSearchGetStartingVector() and TaoLineSearchGetStepLength()

  Level: advanced

  Note:
  Some algorithms (lcl, gpcg) set their own objective routine for the 
  line search, application programmers should be wary of overriding the
  default objective routine.

.seealso: TaoLineSearchCreate(), TaoLineSearchSetObjective(), TaoLineSearchSetGradient(), TaoLineSearchUseTaoSolverRoutines()
@*/
PetscErrorCode TaoLineSearchSetObjectiveAndGTSRoutine(TaoLineSearch ls, PetscErrorCode(*func)(TaoLineSearch ls, Vec x, Vec s, PetscReal *, PetscReal *, void*), void *ctx)
{
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    
    ls->ops->computeobjectiveandgts=func;
    if (ctx) ls->userctx_funcgts=ctx;
    ls->usegts = PETSC_TRUE;
    ls->usetaoroutines=PETSC_FALSE;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchUseTaoSolverRoutines"
/*@C
  TaoLineSearchUseTaoSolverRoutines - Informs the TaoLineSearch to use the 
  objective and gradient evaluation routines from the given TaoSolver object.

  Logically Collective on TaoLineSearch

  Input Parameter:
+ ls - the TaoLineSearch context
- ts - the TaoSolver context with defined objective/gradient evaluation routines

  Level: developer

.seealso: TaoLineSearchCreate()
@*/
PetscErrorCode TaoLineSearchUseTaoSolverRoutines(TaoLineSearch ls, TaoSolver ts) 
{
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    PetscValidHeaderSpecific(ts,TAOSOLVER_CLASSID,1);
    ls->taosolver = ts;
    ls->usetaoroutines=PETSC_TRUE;
    PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchComputeObjective"
/*@
  TaoLineSearchComputeObjective - Computes the objective function value at a given point

  Collective on TaoLineSearch

  Input Parameters:
+ ls - the TaoLineSearch context
- x - input vector

  Output Parameter:
. f - Objective value at X

  Notes: TaoLineSearchComputeObjective() is typically used within line searches
  so most users would not generally call this routine themselves.

  Level: developer

.seealso: TaoLineSearchComputeGradient(), TaoLineSearchComputeObjectiveAndGradient(), TaoLineSearchSetObjectiveRoutine()
@*/
PetscErrorCode TaoLineSearchComputeObjective(TaoLineSearch ls, Vec x, PetscReal *f) 
{
    PetscErrorCode ierr;
    Vec gdummy;
    PetscReal gts;
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    PetscValidHeaderSpecific(x,VEC_CLASSID,2);
    PetscValidPointer(f,3);
    PetscCheckSameComm(ls,1,x,2);
    if (ls->usetaoroutines) {
      ierr = TaoComputeObjective(ls->taosolver,x,f); CHKERRQ(ierr);
    } else {
      ierr = PetscLogEventBegin(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
      if (!ls->ops->computeobjective && !ls->ops->computeobjectiveandgradient
	  && !ls->ops->computeobjectiveandgts) {
	SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Line Search does not have objective function set");
      }
      PetscStackPush("TaoLineSearch user objective routine"); 
      CHKMEMQ;
      if (ls->ops->computeobjective) {
	ierr = (*ls->ops->computeobjective)(ls,x,f,ls->userctx_func); CHKERRQ(ierr);
      } else if (ls->ops->computeobjectiveandgradient) {
	ierr = VecDuplicate(x,&gdummy); CHKERRQ(ierr);
	ierr = (*ls->ops->computeobjectiveandgradient)(ls,x,f,gdummy,ls->userctx_funcgrad); CHKERRQ(ierr);
	ierr = VecDestroy(&gdummy); CHKERRQ(ierr);
      } else {
	ierr = (*ls->ops->computeobjectiveandgts)(ls,x,ls->stepdirection,f,&gts,ls->userctx_funcgts); CHKERRQ(ierr);
      }
      CHKMEMQ;
      PetscStackPop;
      ierr = PetscLogEventEnd(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
    }
    ls->nfeval++;
    PetscFunctionReturn(0);
    
}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchComputeObjectiveAndGradient"
/*@
  TaoLineSearchComputeObjectiveAndGradient - Computes the objective function value at a given point

  Collective on TaoSOlver

  Input Parameters:
+ ls - the TaoLineSearch context
- x - input vector

  Output Parameter:
+ f - Objective value at X
- g - Gradient vector at X

  Notes: TaoLineSearchComputeObjectiveAndGradient() is typically used within line searches
  so most users would not generally call this routine themselves.

  Level: developer

.seealso: TaoLineSearchComputeGradient(), TaoLineSearchComputeObjectiveAndGradient(), TaoLineSearchSetObjectiveRoutine()
@*/
PetscErrorCode TaoLineSearchComputeObjectiveAndGradient(TaoLineSearch ls, Vec x, PetscReal *f, Vec g) 
{
    PetscErrorCode ierr;
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    PetscValidHeaderSpecific(x,VEC_CLASSID,2);
    PetscValidPointer(f,3);
    PetscValidHeaderSpecific(g,VEC_CLASSID,4);
    PetscCheckSameComm(ls,1,x,2);
    PetscCheckSameComm(ls,1,g,4);
    if (ls->usetaoroutines) {
      ierr = TaoComputeObjectiveAndGradient(ls->taosolver,x,f,g); 
      CHKERRQ(ierr); 
    } else {
      ierr = PetscLogEventBegin(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
      if (!ls->ops->computeobjective && !ls->ops->computeobjectiveandgradient) {
	SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Line Search does not have objective function set");
      }
      if (!ls->ops->computegradient && !ls->ops->computeobjectiveandgradient) {
	SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Line Search does not have gradient function set");
      }

      PetscStackPush("TaoLineSearch user objective/gradient routine"); 
      CHKMEMQ;
      if (ls->ops->computeobjectiveandgradient) {
	ierr = (*ls->ops->computeobjectiveandgradient)(ls,x,f,g,ls->userctx_funcgrad); CHKERRQ(ierr);
      } else {
	ierr = (*ls->ops->computeobjective)(ls,x,f,ls->userctx_func); CHKERRQ(ierr);
	ierr = (*ls->ops->computegradient)(ls,x,g,ls->userctx_grad); CHKERRQ(ierr);
      }
      CHKMEMQ;
      PetscStackPop;
      ierr = PetscLogEventEnd(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
    }
    ierr = PetscInfo1(ls,"TaoLineSearch Function evaluation: %14.12e\n",*f);CHKERRQ(ierr);    
    ls->nfgeval++;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchComputeGradient"
/*@
  TaoLineSearchComputeGradient - Computes the gradient of the objective function

  Collective on TaoLineSearch

  Input Parameters:
+ ls - the TaoLineSearch context
- x - input vector

  Output Parameter:
. g - gradient vector  

  Notes: TaoComputeGradient() is typically used within line searches
  so most users would not generally call this routine themselves.

  Level: developer

.seealso: TaoLineSearchComputeObjective(), TaoLineSearchComputeObjectiveAndGradient(), TaoLineSearchSetGradient()
@*/
PetscErrorCode TaoLineSearchComputeGradient(TaoLineSearch ls, Vec x, Vec g) 
{
    PetscErrorCode ierr;
    PetscReal fdummy;
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    PetscValidHeaderSpecific(x,VEC_CLASSID,2);
    PetscValidHeaderSpecific(g,VEC_CLASSID,3);
    PetscCheckSameComm(ls,1,x,2);
    PetscCheckSameComm(ls,1,g,3);
    if (ls->usetaoroutines) {
      ierr = TaoComputeGradient(ls->taosolver,x,g); CHKERRQ(ierr);
    } else {
      ierr = PetscLogEventBegin(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
      if (!ls->ops->computegradient && !ls->ops->computeobjectiveandgradient) {
	SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Line Search does not have gradient functions set");
      }
      PetscStackPush("TaoLineSearch user gradient routine"); 
      CHKMEMQ;
      if (ls->ops->computegradient) { 
	ierr = (*ls->ops->computegradient)(ls,x,g,ls->userctx_grad); CHKERRQ(ierr);
      } else {
	ierr = (*ls->ops->computeobjectiveandgradient)(ls,x,&fdummy,g,ls->userctx_funcgrad); CHKERRQ(ierr);
      }
      CHKMEMQ;
      PetscStackPop;
      ierr = PetscLogEventEnd(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
    }
    ls->ngeval++;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchComputeObjectiveAndGTS"
/*@
  TaoLineSearchComputeObjectiveAndGTS - Computes the objective function value and inner product of gradient and step direction at a given point

  Collective on TaoSolver

  Input Parameters:
+ ls - the TaoLineSearch context
- x - input vector

  Output Parameter:
+ f - Objective value at X
- gts - inner product of gradient and step direction at X

  Notes: TaoLineSearchComputeObjectiveAndGTS() is typically used within line searches
  so most users would not generally call this routine themselves.

  Level: developer

.seealso: TaoLineSearchComputeGradient(), TaoLineSearchComputeObjectiveAndGradient(), TaoLineSearchSetObjectiveRoutine()
@*/
PetscErrorCode TaoLineSearchComputeObjectiveAndGTS(TaoLineSearch ls, Vec x, PetscReal *f, PetscReal *gts)
{
    PetscErrorCode ierr;
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    PetscValidHeaderSpecific(x,VEC_CLASSID,2);
    PetscValidPointer(f,3);
    PetscValidPointer(gts,4);
    PetscCheckSameComm(ls,1,x,2);
    ierr = PetscLogEventBegin(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
    if (!ls->ops->computeobjectiveandgts) {
	SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Line Search does not have objective and gts function set");
    }

    PetscStackPush("TaoLineSearch user objective/gts routine"); 
    CHKMEMQ;
    ierr = (*ls->ops->computeobjectiveandgts)(ls,x,ls->stepdirection,f,gts,ls->userctx_funcgts); CHKERRQ(ierr);
    CHKMEMQ;
    PetscStackPop;
    ierr = PetscLogEventEnd(TaoLineSearch_EvalEvent,ls,0,0,0); CHKERRQ(ierr);
    ierr = PetscInfo1(ls,"TaoLineSearch Function evaluation: %14.12e\n",*f);CHKERRQ(ierr);    
    ls->nfeval++;
    PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetSolution"
/*@
  TaoLineSearchGetSolution - Returns the solution to the line search

  Collective on TaoLineSearch

  Input Parameter:
. ls - the TaoLineSearch context

  Output Parameter:
+ x - the new solution
. f - the objective function value at x
. g - the gradient at x
. steplength - the multiple of the step direction taken by the line search
- reason - the reason why the line search terminated

  reason will be set to one of:

+ TAOLINESEARCH_FAILED_INFORNAN - function evaluation gives Inf or Nan value
. TAOLINESEARCH_FAILED_BADPARAMETER - negative value set as parameter
. TAOLINESEARCH_FAILED_ASCENT - initial line search step * g is not descent direction
. TAOLINESEARCH_HALTED_MAXFCN - maximum number of function evaluation reached
. TAOLINESEARCH_HALTED_UPPERBOUND - step is at upper bound
. TAOLINESEARCH_HALTED_LOWERBOUND - step is at lower bound
. TAOLINESEARCH_HALTED_RTOL - range of uncertainty is smaller than given tolerance

. TAOLINESEARCH_HALTED_USER - user can set this reason to stop line search
. TAOLINESEARCH_HALTED_OTHER - any other reason

+ TAOLINESEARCH_SUCCESS - successful line search

  Level: developer
 
@*/
PetscErrorCode TaoLineSearchGetSolution(TaoLineSearch ls, Vec x, PetscReal *f, Vec g, PetscReal *steplength, TaoLineSearchTerminationReason *reason) 
{
    PetscErrorCode ierr;
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    PetscValidHeaderSpecific(x,VEC_CLASSID,2);
    PetscValidPointer(f,3);
    PetscValidHeaderSpecific(g,VEC_CLASSID,4);
    PetscValidIntPointer(reason,6);

    if (ls->new_x) {
	ierr = VecCopy(ls->new_x,x); CHKERRQ(ierr);
    }
    *f = ls->new_f;
    if (ls->new_g) {
	ierr = VecCopy(ls->new_g,g); CHKERRQ(ierr);
    }
    if (steplength) {
      *steplength=ls->step;
    }
    *reason = ls->reason;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetStartingVector"
/*@
  TaoLineSearchGetStartingVector - Gets a the initial point of the line
  search. 

  Not Collective

  Input Parameter:
. ls - the TaoLineSearch context
 
  Output Parameter:
. x - The initial point of the line search

  Level: intermediate
@*/
PetscErrorCode TaoLineSearchGetStartingVector(TaoLineSearch ls, Vec *x)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
  if (x) {
    *x = ls->start_x;
  }
  PetscFunctionReturn(0);

}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetStepDirection"
/*@
  TaoLineSearchGetStepDirection - Gets the step direction of the line
  search. 

  Not Collective

  Input Parameter:
. ls - the TaoLineSearch context
 
  Output Parameter:
. s - the step direction of the line search

  Level: advanced
@*/
PetscErrorCode TaoLineSearchGetStepDirection(TaoLineSearch ls, Vec *s)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
  if (s) {
    *s = ls->stepdirection;
  }
  PetscFunctionReturn(0);

}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetFullStepObjective"
/*@
  TaoLineSearchGetFullStepObjective - Returns the objective function value at the full step.  Useful for some minimization algorithms.

  Not Collective

  Input Parameter:
. ls - the TaoLineSearch context

  Output Parameter:
. f - the objective value at the full step length

  Level: developer
@*/

PetscErrorCode TaoLineSearchGetFullStepObjective(TaoLineSearch ls, PetscReal *f_fullstep)
{
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    *f_fullstep = ls->f_fullstep;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetVariableBounds"
/*@
  TaoLineSearchSetVariableBounds - Sets the upper and lower bounds.  

  Logically Collective on TaoSolver

  Input Parameters:
+ ls - the TaoLineSearch context
. xl  - vector of lower bounds 
- xu  - vector of upper bounds

  Note: If the variable bounds are not set with this routine, then 
  TAO_NINFINITY and TAO_INFINITY are assumed

  Level: beginner

.seealso: TaoSetVariableBounds(), TaoLineSearchCreate()
@*/
PetscErrorCode TaoLineSearchSetVariableBounds(TaoLineSearch ls,Vec xl, Vec xu)
{
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    PetscValidHeaderSpecific(xl,VEC_CLASSID,2);
    PetscValidHeaderSpecific(xu,VEC_CLASSID,3);
    
    ls->lower = xl;
    ls->upper = xu;
    ls->bounded = 1;

    PetscFunctionReturn(0);
    
}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetInitialStepLength"
/*@
  TaoLineSearchSetInitialStepLength - Sets the initial step length of a line
  search.  If this value is not set then 1.0 is assumed.

  Logically Collective on TaoLineSearch

  Input Parameters:
+ ls - the TaoLineSearch context
- s - the initial step size
  
  Level: intermediate

.seealso: TaoLineSearchGetStepLength(), TaoLineSearchApply()
@*/
PetscErrorCode TaoLineSearchSetInitialStepLength(TaoLineSearch ls,PetscReal s)
{
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    
    ls->initstep = s;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetStepLength"
/*@
  TaoLineSearchGetStepLength - Get the current step length

  Not Collective

  Input Parameters:
. ls - the TaoLineSearch context

  Output Parameters
. s - the current step length
  
  Level: beginner

.seealso: TaoLineSearchSetInitialStepLength(), TaoLineSearchApply()
@*/
PetscErrorCode TaoLineSearchGetStepLength(TaoLineSearch ls,PetscReal *s)
{
    PetscFunctionBegin;
    PetscValidHeaderSpecific(ls,TAOLINESEARCH_CLASSID,1);
    *s = ls->step;
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchRegister"
/*MC
   TaoLineSearchRegister - Adds a line-search algorithm to the registry

   Not collective

   Input Parameters:
+  sname - name of a new user-defined solver
-  func - routine to Create method context

   Notes:
   TaoLineSearchRegister() may be called multiple times to add several user-defined solvers.

   Sample usage:
.vb
   TaoLineSearchRegister("my_linesearch",MyLinesearchCreate);
.ve

   Then, your solver can be chosen with the procedural interface via
$     TaoLineSearchSetType(ls,"my_linesearch")
   or at runtime via the option
$     -tao_ls_type my_linesearch

   Level: developer

.seealso: TaoLineSearchRegisterDestroy()
M*/
PetscErrorCode TaoLineSearchRegister(const char sname[], PetscErrorCode (*func)(TaoLineSearch))
{
    PetscErrorCode ierr;
    PetscFunctionBegin;
    ierr = PetscFunctionListAdd(&TaoLineSearchList, sname, (void (*)(void))func); CHKERRQ(ierr);
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchRegisterDestroy"
/*@C
   TaoLineSearchRegisterDestroy - Frees the list of line-search algorithms that were
   registered by TaoLineSearchRegister().

   Not Collective

   Level: developer

.seealso: TaoLineSearchRegister()
@*/
PetscErrorCode TaoLineSearchRegisterDestroy(void)
{
    PetscErrorCode ierr;
    PetscFunctionBegin;
    ierr = PetscFunctionListDestroy(&TaoLineSearchList); CHKERRQ(ierr);
    TaoLineSearchInitialized = PETSC_FALSE;
    PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchAppendOptionsPrefix"
/*@C
   TaoLineSearchAppendOptionsPrefix - Appends to the prefix used for searching 
   for all TaoLineSearch options in the database.


   Collective on TaoLineSearch

   Input Parameters:
+  ls - the TaoLineSearch solver context
-  prefix - the prefix string to prepend to all line search requests

   Notes:
   A hyphen (-) must NOT be given at the beginning of the prefix name.
   The first character of all runtime options is AUTOMATICALLY the hyphen.


   Level: advanced

.seealso: TaoLineSearchSetOptionsPrefix(), TaoLineSearchGetOptionsPrefix()
@*/
PetscErrorCode TaoLineSearchAppendOptionsPrefix(TaoLineSearch ls, const char p[])
{
  PetscObjectAppendOptionsPrefix((PetscObject)ls,p);
  return(0);
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchGetOptionsPrefix"
/*@C
  TaoLineSearchGetOptionsPrefix - Gets the prefix used for searching for all 
  TaoLineSearch options in the database

  Not Collective

  Input Parameters:
. ls - the TaoLineSearch context
  
  Output Parameters:
. prefix - pointer to the prefix string used is returned

  Notes: On the fortran side, the user should pass in a string 'prefix' of
  sufficient length to hold the prefix.

  Level: advanced

.seealso: TaoLineSearchSetOptionsPrefix(), TaoLineSearchAppendOptionsPrefix()
@*/
PetscErrorCode TaoLineSearchGetOptionsPrefix(TaoLineSearch ls, const char *p[])
{
   PetscObjectGetOptionsPrefix((PetscObject)ls,p);
   return 0;
}

#undef __FUNCT__
#define __FUNCT__ "TaoLineSearchSetOptionsPrefix"
/*@C
   TaoLineSearchSetOptionsPrefix - Sets the prefix used for searching for all
   TaoLineSearch options in the database.


   Logically Collective on TaoLineSearch

   Input Parameters:
+  ls - the TaoLineSearch context
-  prefix - the prefix string to prepend to all TAO option requests

   Notes:
   A hyphen (-) must NOT be given at the beginning of the prefix name.
   The first character of all runtime options is AUTOMATICALLY the hyphen.

   For example, to distinguish between the runtime options for two
   different line searches, one could call
.vb
      TaoLineSearchSetOptionsPrefix(ls1,"sys1_")
      TaoLineSearchSetOptionsPrefix(ls2,"sys2_")
.ve

   This would enable use of different options for each system, such as
.vb
      -sys1_tao_ls_type mt
      -sys2_tao_ls_type armijo
.ve


   Level: advanced

.seealso: TaoLineSearchAppendOptionsPrefix(), TaoLineSearchGetOptionsPrefix()
@*/

PetscErrorCode TaoLineSearchSetOptionsPrefix(TaoLineSearch ls, const char p[])
{
  PetscObjectSetOptionsPrefix((PetscObject)ls,p);
  return(0);
}