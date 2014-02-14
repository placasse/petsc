#ifndef __TAODM_H
#define __TAODM_H
#include <petscmat.h>
#include <petscdm.h>
#include <petsctao.h>

typedef struct _p_TaoDM* TaoDM;
PETSC_EXTERN PetscClassId TAODM_CLASSID;

PETSC_EXTERN PetscErrorCode TaoDMSetMatType(TaoDM *, const MatType);
PETSC_EXTERN PetscErrorCode TaoDMCreate(MPI_Comm, PetscInt, void*, TaoDM**);
PETSC_EXTERN PetscErrorCode TaoDMSetSolverType(TaoDM*, const TaoType);
PETSC_EXTERN PetscErrorCode TaoDMSetOptionsPrefix(TaoDM *, const char []);
PETSC_EXTERN PetscErrorCode TaoDMDestroy(TaoDM*);
PETSC_EXTERN PetscErrorCode TaoDMDestroyLevel(TaoDM);
PETSC_EXTERN PetscErrorCode TaoDMSetFromOptions(TaoDM*);
PETSC_EXTERN PetscErrorCode TaoDMSetTolerances(TaoDM*,PetscReal,PetscReal,PetscReal,PetscReal,PetscReal);
PETSC_EXTERN PetscErrorCode TaoDMSetUp(TaoDM*);
PETSC_EXTERN PetscErrorCode TaoDMSolve(TaoDM*);
PETSC_EXTERN PetscErrorCode TaoDMView(TaoDM*, PetscViewer);
PETSC_EXTERN PetscErrorCode TaoDMSetDM(TaoDM *, DM);
PETSC_EXTERN PetscErrorCode TaoDMGetDM(TaoDM , DM*);
PETSC_EXTERN PetscErrorCode TaoDMSetContext(TaoDM , void*);
PETSC_EXTERN PetscErrorCode TaoDMGetContext(TaoDM , void**);

PETSC_EXTERN PetscErrorCode TaoDMSetPreLevelMonitor(TaoDM*,PetscErrorCode(*)(TaoDM, PetscInt, void*),void*);
PETSC_EXTERN PetscErrorCode TaoDMSetPostLevelMonitor(TaoDM*,PetscErrorCode(*)(TaoDM, PetscInt, void*),void*);
PETSC_EXTERN PetscErrorCode TaoDMSetInitialGuessRoutine(TaoDM*,PetscErrorCode(*)(TaoDM,Vec));
PETSC_EXTERN PetscErrorCode TaoDMSetVariableBoundsRoutine(TaoDM*,PetscErrorCode(*)(TaoDM,Vec, Vec));
PETSC_EXTERN PetscErrorCode TaoDMSetObjectiveAndGradientRoutine(TaoDM*,PetscErrorCode(*)(Tao, Vec, PetscReal*, Vec, void*));
PETSC_EXTERN PetscErrorCode TaoDMSetObjectiveRoutine(TaoDM*,PetscErrorCode(*)(Tao, Vec, PetscReal*, void*));
PETSC_EXTERN PetscErrorCode TaoDMSetGradientRoutine(TaoDM*,PetscErrorCode(*)(Tao, Vec, Vec, void*));
PETSC_EXTERN PetscErrorCode TaoDMSetHessianRoutine(TaoDM*,PetscErrorCode(*)(Tao, Vec, Mat*, Mat*, MatStructure*,void*));

PETSC_EXTERN PetscErrorCode TaoDMSetLocalObjectiveRoutine(TaoDM*,PetscErrorCode(*)(DMDALocalInfo*,PetscReal**,PetscReal*,void*));
PETSC_EXTERN PetscErrorCode TaoDMSetLocalGradientRoutine(TaoDM*,PetscErrorCode(*)(DMDALocalInfo*,PetscReal**,PetscReal**,void*));
PETSC_EXTERN PetscErrorCode TaoDMSetLocalObjectiveAndGradientRoutine(TaoDM*,PetscErrorCode(*)(DMDALocalInfo*,PetscReal**,PetscReal*,PetscReal**,void*));
PETSC_EXTERN PetscErrorCode TaoDMSetLocalHessianRoutine(TaoDM*,PetscErrorCode(*)(DMDALocalInfo*,PetscReal**,Mat,void*));

PETSC_EXTERN PetscErrorCode TaoDMFormFunctionLocal(Tao, Vec, PetscReal *, void*);
PETSC_EXTERN PetscErrorCode TaoDMFormGradientLocal(Tao, Vec, Vec, void*);
PETSC_EXTERN PetscErrorCode TaoDMFormFunctionGradientLocal(Tao, Vec, PetscReal *, Vec, void*);
PETSC_EXTERN PetscErrorCode TaoDMFormHessianLocal(Tao, Vec, Mat*, Mat*, MatStructure*,void*);
PETSC_EXTERN PetscErrorCode TaoDMFormBounds(Tao, Vec, Vec, void*);

#endif