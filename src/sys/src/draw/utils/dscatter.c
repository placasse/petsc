#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: dscatter.c,v 1.22 1999/03/17 23:21:24 bsmith Exp bsmith $";
#endif
/*
       Contains the data structure for drawing scatter plots
    graphs in a window with an axis. This is intended for scatter
    plots that change dynamically.
*/

#include "petsc.h"         /*I "petsc.h" I*/

struct _p_DrawSP {
  PETSCHEADER(int) 
  int         (*destroy)(DrawSP);
  int         (*view)(DrawSP,Viewer);
  int         len,loc;
  Draw        win;
  DrawAxis    axis;
  double      xmin, xmax, ymin, ymax, *x, *y;
  int         nopts, dim;
};

#define CHUNCKSIZE 100

#undef __FUNC__  
#define __FUNC__ "DrawSPCreate"
/*@C
    DrawSPCreate - Creates a scatter plot data structure.

    Collective over Draw

    Input Parameters:
+   win - the window where the graph will be made.
-   dim - the number of sets of points which will be drawn

    Output Parameters:
.   outctx - the scatter plot context

   Level: intermediate

.keywords:  draw, scatter plot, graph, create

.seealso:  DrawSPDestroy()
@*/
int DrawSPCreate(Draw win,int dim,DrawSP *outctx)
{
  int         ierr;
  PetscObject vobj = (PetscObject) win;
  DrawSP      sp;

  PetscFunctionBegin;
  if (vobj->cookie == DRAW_COOKIE && PetscTypeCompare(vobj->type,DRAW_NULL)) {
    ierr = DrawOpenNull(vobj->comm,(Draw*)outctx); CHKERRQ(ierr);
    (*outctx)->win = win;
    PetscFunctionReturn(0);
  }
  PetscHeaderCreate(sp,_p_DrawSP,int,DRAWSP_COOKIE,0,"DrawSP",vobj->comm,DrawSPDestroy,0);
  sp->view    = 0;
  sp->destroy = 0;
  sp->nopts   = 0;
  sp->win     = win;
  sp->dim     = dim;
  sp->xmin    = 1.e20;
  sp->ymin    = 1.e20;
  sp->xmax    = -1.e20;
  sp->ymax    = -1.e20;
  sp->x       = (double *)PetscMalloc(2*dim*CHUNCKSIZE*sizeof(double));CHKPTRQ(sp->x);
  PLogObjectMemory(sp,2*dim*CHUNCKSIZE*sizeof(double));
  sp->y       = sp->x + dim*CHUNCKSIZE;
  sp->len     = dim*CHUNCKSIZE;
  sp->loc     = 0;
  ierr = DrawAxisCreate(win,&sp->axis); CHKERRQ(ierr);
  PLogObjectParent(sp,sp->axis);
  *outctx = sp;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DrawSPSetDimension"
/*@
   DrawSPSetDimension - Change the number of sets of points  that are to be drawn.

   Not Collective (ignored on all processors except processor 0 of DrawSP)

   Input Parameter:
+  sp - the line graph context.
-  dim - the number of curves.

   Level: intermediate

.keywords:  draw, line, graph, reset
@*/
int DrawSPSetDimension(DrawSP sp,int dim)
{
  PetscFunctionBegin;
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {PetscFunctionReturn(0);}
  PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  if (sp->dim == dim) PetscFunctionReturn(0);

  PetscFree(sp->x);
  sp->dim     = dim;
  sp->x       = (double *)PetscMalloc(2*dim*CHUNCKSIZE*sizeof(double)); CHKPTRQ(sp->x);
  PLogObjectMemory(sp,2*dim*CHUNCKSIZE*sizeof(double));
  sp->y       = sp->x + dim*CHUNCKSIZE;
  sp->len     = dim*CHUNCKSIZE;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DrawSPReset"
/*@
   DrawSPReset - Clears line graph to allow for reuse with new data.

   Not Collective (ignored on all processors except processor 0 of DrawSP)

   Input Parameter:
.  sp - the line graph context.

   Level: intermediate

.keywords:  draw, line, graph, reset
@*/
int DrawSPReset(DrawSP sp)
{
  PetscFunctionBegin;
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {PetscFunctionReturn(0);}
  PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  sp->xmin  = 1.e20;
  sp->ymin  = 1.e20;
  sp->xmax  = -1.e20;
  sp->ymax  = -1.e20;
  sp->loc   = 0;
  sp->nopts = 0;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DrawSPDestroy"
/*@C
   DrawSPDestroy - Frees all space taken up by scatter plot data structure.

   Collective over DrawSP

   Input Parameter:
.  sp - the line graph context

   Level: intermediate

.keywords:  draw, line, graph, destroy

.seealso:  DrawSPCreate()
@*/
int DrawSPDestroy(DrawSP sp)
{
  int ierr;

  PetscFunctionBegin;
  if (!sp || !(sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL))) {
    PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  }

  if (--sp->refct > 0) PetscFunctionReturn(0);
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {
    ierr = PetscObjectDestroy((PetscObject) sp);CHKERRQ(ierr);
    PetscFunctionReturn(0);
  }
  DrawAxisDestroy(sp->axis);
  PetscFree(sp->x);
  PLogObjectDestroy(sp);
  PetscHeaderDestroy(sp);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DrawSPAddPoint"
/*@
   DrawSPAddPoint - Adds another point to each of the scatter plots.

   Not Collective (ignored on all processors except processor 0 of DrawSP)

   Input Parameters:
+  sp - the scatter plot data structure
-  x, y - the points to two vectors containing the new x and y 
          point for each curve.

   Level: intermediate

.keywords:  draw, line, graph, add, point

.seealso: DrawSPAddPoints()
@*/
int DrawSPAddPoint(DrawSP sp,double *x,double *y)
{
  int i;

  PetscFunctionBegin;
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {PetscFunctionReturn(0);}

  PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  if (sp->loc+sp->dim >= sp->len) { /* allocate more space */
    double *tmpx,*tmpy;
    tmpx = (double *) PetscMalloc((2*sp->len+2*sp->dim*CHUNCKSIZE)*sizeof(double));CHKPTRQ(tmpx);
    PLogObjectMemory(sp,2*sp->dim*CHUNCKSIZE*sizeof(double));
    tmpy = tmpx + sp->len + sp->dim*CHUNCKSIZE;
    PetscMemcpy(tmpx,sp->x,sp->len*sizeof(double));
    PetscMemcpy(tmpy,sp->y,sp->len*sizeof(double));
    PetscFree(sp->x);
    sp->x = tmpx; sp->y = tmpy;
    sp->len += sp->dim*CHUNCKSIZE;
  }
  for (i=0; i<sp->dim; i++) {
    if (x[i] > sp->xmax) sp->xmax = x[i]; 
    if (x[i] < sp->xmin) sp->xmin = x[i];
    if (y[i] > sp->ymax) sp->ymax = y[i]; 
    if (y[i] < sp->ymin) sp->ymin = y[i];

    sp->x[sp->loc]   = x[i];
    sp->y[sp->loc++] = y[i];
  }
  sp->nopts++;
  PetscFunctionReturn(0);
}


#undef __FUNC__  
#define __FUNC__ "DrawSPAddPoints"
/*@C
   DrawSPAddPoints - Adds several points to each of the scatter plots.

   Not Collective (ignored on all processors except processor 0 of DrawSP)

   Input Parameters:
+  sp - the LineGraph data structure
.  xx,yy - points to two arrays of pointers that point to arrays 
           containing the new x and y points for each curve.
-  n - number of points being added

   Level: intermediate

.keywords:  draw, line, graph, add, points

.seealso: DrawSPAddPoint()
@*/
int DrawSPAddPoints(DrawSP sp,int n,double **xx,double **yy)
{
  int    i, j, k;
  double *x,*y;

  PetscFunctionBegin;
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {PetscFunctionReturn(0);}
  PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  if (sp->loc+n*sp->dim >= sp->len) { /* allocate more space */
    double *tmpx,*tmpy;
    int    chunk = CHUNCKSIZE;
    if (n > chunk) chunk = n;
    tmpx = (double *) PetscMalloc((2*sp->len+2*sp->dim*chunk)*sizeof(double));CHKPTRQ(tmpx);
    PLogObjectMemory(sp,2*sp->dim*CHUNCKSIZE*sizeof(double));
    tmpy = tmpx + sp->len + sp->dim*chunk;
    PetscMemcpy(tmpx,sp->x,sp->len*sizeof(double));
    PetscMemcpy(tmpy,sp->y,sp->len*sizeof(double));
    PetscFree(sp->x);
    sp->x   = tmpx; sp->y = tmpy;
    sp->len += sp->dim*CHUNCKSIZE;
  }
  for (j=0; j<sp->dim; j++) {
    x = xx[j]; y = yy[j];
    k = sp->loc + j;
    for ( i=0; i<n; i++ ) {
      if (x[i] > sp->xmax) sp->xmax = x[i]; 
      if (x[i] < sp->xmin) sp->xmin = x[i];
      if (y[i] > sp->ymax) sp->ymax = y[i]; 
      if (y[i] < sp->ymin) sp->ymin = y[i];

      sp->x[k]   = x[i];
      sp->y[k] = y[i];
      k += sp->dim;
    }
  }
  sp->loc   += n*sp->dim;
  sp->nopts += n;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DrawSPDraw"
/*@
   DrawSPDraw - Redraws a scatter plot.

   Not Collective (ignored on all processors except processor 0 of DrawSP)

   Input Parameter:
.  sp - the line graph context

   Level: intermediate

.keywords:  draw, line, graph
@*/
int DrawSPDraw(DrawSP sp)
{
  double   xmin=sp->xmin, xmax=sp->xmax, ymin=sp->ymin, ymax=sp->ymax;
  int      ierr, i, j, dim = sp->dim,nopts = sp->nopts,rank;
  Draw     win = sp->win;

  PetscFunctionBegin;
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {PetscFunctionReturn(0);}
  PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);

  if (nopts < 1) PetscFunctionReturn(0);
  if (xmin > xmax || ymin > ymax) PetscFunctionReturn(0);
  ierr = DrawClear(win);CHKERRQ(ierr);
  ierr = DrawAxisSetLimits(sp->axis, xmin, xmax, ymin, ymax);CHKERRQ(ierr);
  ierr = DrawAxisDraw(sp->axis);CHKERRQ(ierr);
  
  ierr = MPI_Comm_rank(sp->comm,&rank);CHKERRQ(ierr);
  if (rank)   PetscFunctionReturn(0);
  for ( i=0; i<dim; i++ ) {
    for ( j=0; j<nopts; j++ ) {
      ierr = DrawString(win,sp->x[j*dim+i],sp->y[j*dim+i],DRAW_RED,"x");CHKERRQ(ierr);
    }
  }
  ierr = DrawFlush(sp->win);CHKERRQ(ierr);
  ierr = DrawPause(sp->win);CHKERRQ(ierr);
  PetscFunctionReturn(0);
} 
 
#undef __FUNC__  
#define __FUNC__ "DrawSPSetLimits"
/*@
   DrawSPSetLimits - Sets the axis limits for a line graph. If more
   points are added after this call, the limits will be adjusted to
   include those additional points.

   Not Collective (ignored on all processors except processor 0 of DrawSP)

   Input Parameters:
+  xsp - the line graph context
-  x_min,x_max,y_min,y_max - the limits

   Level: intermediate

.keywords:  draw, line, graph, set limits
@*/
int DrawSPSetLimits( DrawSP sp,double x_min,double x_max,double y_min,
                                  double y_max) 
{
  PetscFunctionBegin;
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {PetscFunctionReturn(0);}
  PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  (sp)->xmin = x_min; 
  (sp)->xmax = x_max; 
  (sp)->ymin = y_min; 
  (sp)->ymax = y_max;
  PetscFunctionReturn(0);
}
 
#undef __FUNC__  
#define __FUNC__ "DrawSPGetAxis"
/*@C
   DrawSPGetAxis - Gets the axis context associated with a line graph.
   This is useful if one wants to change some axis property, such as
   labels, color, etc. The axis context should not be destroyed by the
   application code.

   Not Collective (except DrawAxis can only be used on processor 0 of DrawSP)

   Input Parameter:
.  sp - the line graph context

   Output Parameter:
.  axis - the axis context

   Level: intermediate

.keywords: draw, line, graph, get, axis
@*/
int DrawSPGetAxis(DrawSP sp,DrawAxis *axis)
{
  PetscFunctionBegin;
  if (sp && sp->cookie == DRAW_COOKIE && PetscTypeCompare(sp->type_name,DRAW_NULL)) {
    *axis = 0;
    PetscFunctionReturn(0);
  }
  PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  *axis = sp->axis;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DrawSPGetDraw"
/*@C
   DrawSPGetDraw - Gets the draw context associated with a line graph.

   Not Collective, Draw is parallel if DrawSP is parallel

   Input Parameter:
.  sp - the line graph context

   Output Parameter:
.  win - the draw context

   Level: intermediate

.keywords: draw, line, graph, get, context
@*/
int DrawSPGetDraw(DrawSP sp,Draw *win)
{
  PetscFunctionBegin;
  if (!sp || sp->cookie != DRAW_COOKIE || PetscTypeCompare(sp->type_name,DRAW_NULL)) {
    PetscValidHeaderSpecific(sp,DRAWSP_COOKIE);
  }
  *win = sp->win;
  PetscFunctionReturn(0);
}
