#ifndef lint
static char vcid[] = "$Id: filev.c,v 1.22 1995/09/06 13:43:11 curfman Exp bsmith $";
#endif

#include "petsc.h"
#if defined(HAVE_STRING_H)
#include <string.h>
#endif
#include <stdarg.h>

struct _Viewer {
  PETSCHEADER
  FILE        *fd;
  int         format;
  char        *outputname;
};

Viewer STDOUT_VIEWER_SELF, STDERR_VIEWER_SELF, STDOUT_VIEWER_WORLD;

int ViewerInitialize_Private()
{
  ViewerFileOpen(MPI_COMM_SELF,"stderr",&STDERR_VIEWER_SELF);
  ViewerFileOpen(MPI_COMM_SELF,"stdout",&STDOUT_VIEWER_SELF);
  ViewerFileOpen(MPI_COMM_WORLD,"stdout",&STDOUT_VIEWER_WORLD);
  return 0;
}

static int ViewerDestroy_File(PetscObject obj)
{
  Viewer v = (Viewer) obj;
  int    mytid = 0;
  if (v->type == ASCII_FILES_VIEWER) {MPI_Comm_rank(v->comm,&mytid);} 
  if (!mytid && v->fd != stderr && v->fd != stdout) fclose(v->fd);
  PLogObjectDestroy(obj);
  PETSCHEADERDESTROY(obj);
  return 0;
}

int ViewerDestroy_Private()
{
  ViewerDestroy_File((PetscObject)STDERR_VIEWER_SELF);
  ViewerDestroy_File((PetscObject)STDOUT_VIEWER_SELF);
  ViewerDestroy_File((PetscObject)STDOUT_VIEWER_WORLD);
  return 0;
}

int ViewerFileGetPointer_Private(Viewer viewer, FILE **fd)
{
  *fd = viewer->fd;
  return 0;
}


int ViewerFileGetOutputname_Private(Viewer viewer, char **name)
{
  *name = viewer->outputname;
  return 0;
}

int ViewerFileGetFormat_Private(Viewer viewer,int *format)
{
  *format =  viewer->format;
  return 0;
}

/*@
   ViewerFileOpen - Opens an ASCII file as a viewer.

   Input Parameters:
.  comm - the communicator
.  name - the file name

   Output Parameter:
.  lab - the viewer to use with the specified file

   Notes:
   If a multiprocessor communicator is used (such as MPI_COMM_WORLD), 
   then only the first processor in the group opens the file.  All other 
   processors send their data to the first processor to print. 

   Each processor can instead write its own independent output by
   specifying the communicator MPI_COMM_SELF.

   As shown below, ViewerFileOpen() is useful in conjunction with 
   MatView() and VecView()
$
$    ViewerFileOpen("mat.output",MPI_COMM_WORLD,&viewer);
$    MatView(matrix,viewer);

   This viewer can be destroyed with ViewerDestroy().

.keywords: Viewer, file, open

.seealso: MatView(), VecView(), ViewerDestroy()
@*/
int ViewerFileOpen(MPI_Comm comm,char *name,Viewer *lab)
{
  Viewer v;
  if (comm == MPI_COMM_SELF) {
    PETSCHEADERCREATE(v,_Viewer,VIEWER_COOKIE,ASCII_FILE_VIEWER,comm);
  } else {
    PETSCHEADERCREATE(v,_Viewer,VIEWER_COOKIE,ASCII_FILES_VIEWER,comm);
  }
  PLogObjectCreate(v);
  v->destroy     = ViewerDestroy_File;

  if (!strcmp(name,"stderr")) v->fd = stderr;
  else if (!strcmp(name,"stdout")) v->fd = stdout;
  else {
    v->fd        = fopen(name,"w"); 
    if (!v->fd) SETERRQ(1,"ViewerFileOpen: cannot open file");
  }
  v->format        = FILE_FORMAT_DEFAULT;
  v->outputname    = 0;
#if defined(PETSC_LOG)
  PLogObjectState((PetscObject)v,"File: %s",name);
#endif
  *lab           = v;
  return 0;
}

/*@
   ViewerFileSetFormat - Sets the format for file viewers.

   Input Parameters:
.  v - the viewer
.  format - the format
.  char - optional object name

   Notes:
   Available formats include
$    FILE_FORMAT_DEFAULT - default
$    FILE_FORMAT_MATLAB - Matlab format
$    FILE_FORMAT_IMPL - implementation-specific format
$      (which is in many cases the same as the default)
$    FILE_FORMAT_INFO - basic information about object
 
   These formats are most often used for viewing matrices and vectors.
   Currently, the object name is used only in the Matlab format.

.keywords: Viewer, file, set, format

.seealso: ViewerFileOpen(), MatView(), VecView()
@*/
int ViewerFileSetFormat(Viewer v,int format,char *name)
{
  PETSCVALIDHEADERSPECIFIC(v,VIEWER_COOKIE);
  if (v->type == ASCII_FILES_VIEWER || v->type == ASCII_FILE_VIEWER) {
    v->format = format;
    v->outputname = name;
  }
  return 0;
}





