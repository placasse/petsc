#if !defined(__TAODEF_H)
#define __TAODEF_H

#include "finclude/petsctsdef.h"

#if !defined(PETSC_USE_FORTRAN_DATATYPES)
#define Tao PetscFortranAddr
#define TaoLineSearch PetscFortranAddr
#define TaoConvergedReason integer
#endif

#define TAOLMVM     "lmvm"
#define TAONLS      "nls"
#define TAONTR      "ntr"
#define TAONTL      "ntl"
#define TAOCG       "cg"
#define TAOTRON     "tron"
#define TAOOWLQN    "owlqn"
#define TAOBMRM     "bmrm"
#define TAOBLMVM    "blmvm"
#define TAOBQPIP    "bqpip"
#define TAOGPCG     "gpcg"
#define TAONM       "nm"
#define TAOPOUNDERS "pounders"
#define TAOLCL      "lcl"
#define TAOSSILS    "ssils"
#define TAOSSFLS    "ssfls"
#define TAOASILS    "asils"
#define TAOASFLS    "asfls"
#define TAOIPM      "ipm"
#define TAOFDTEST   "test"

#endif
