
#requiresdefine  'PETSC_HAVE_MATLAB_ENGINE'
#requiresscalar real
#requiresprecision  double

ALL: lib

CFLAGS    = ${MATLAB_INCLUDE}
FFLAGS    =
CPPFLAGS  =
SOURCEC	  = vmatlab.c
SOURCEF	  =
SOURCEH	  =
MANSEC	  = Viewer
LIBBASE	  = libpetscsys
LOCDIR	  = src/sys/classes/viewer/impls/matlab/
DIRS      =

include ${PETSC_DIR}/conf/variables
include ${PETSC_DIR}/conf/rules
include ${PETSC_DIR}/conf/test


