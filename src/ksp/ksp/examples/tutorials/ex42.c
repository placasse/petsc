static char help[] =
  "Solves the incompressible, variable viscosity stokes equation in 3d using Q1Q1 elements, \n\
stabilized with Bochev's polynomial projection method. Note that implementation here assumes \n\
all boundaries are free-slip, i.e. zero normal flow and zero tangential stress \n\
     -mx : number elements in x-direction \n\
     -my : number elements in y-direction \n\
     -mz : number elements in z-direction \n\
     -stokes_ksp_monitor_blocks : active monitor for each component u,v,w,p \n\
     -model : defines viscosity and forcing function. Choose either: 0 (isoviscous), 1 (manufactured-broken), 2 (solcx-2d), 3 (sinker) \n";

/* Contributed by Dave May */

#include "petscksp.h"
#include "petscpcmg.h"
#include "petscdmda.h"

#define PROFILE_TIMING
#define ASSEMBLE_LOWER_TRIANGULAR

#define NSD            3 /* number of spatial dimensions */
#define NODES_PER_EL   8 /* nodes per element */
#define U_DOFS         3 /* degrees of freedom per velocity node */
#define P_DOFS         1 /* degrees of freedom per pressure node */
#define GAUSS_POINTS   8

/* Gauss point based evaluation */
typedef struct {
  PetscScalar gp_coords[NSD*GAUSS_POINTS];
  PetscScalar eta[GAUSS_POINTS];
  PetscScalar fx[GAUSS_POINTS];
  PetscScalar fy[GAUSS_POINTS];
  PetscScalar fz[GAUSS_POINTS];
  PetscScalar hc[GAUSS_POINTS];
} GaussPointCoefficients;

typedef struct {
  PetscScalar u_dof;
  PetscScalar v_dof;
  PetscScalar w_dof;
  PetscScalar p_dof;
} StokesDOF;


/* FEM routines */
/*
 Element: Local basis function ordering
 1-----2
 |     |
 |     |
 0-----3
 */
static void ShapeFunctionQ13D_Evaluate(PetscScalar _xi[],PetscScalar Ni[])
{
  PetscReal xi   = PetscRealPart(_xi[0]);
  PetscReal eta  = PetscRealPart(_xi[1]);
  PetscReal zeta = PetscRealPart(_xi[2]);

  Ni[0] = 0.125 * ( 1.0 - xi ) * ( 1.0 - eta ) * ( 1.0 - zeta );
  Ni[1] = 0.125 * ( 1.0 - xi ) * ( 1.0 + eta ) * ( 1.0 - zeta );
  Ni[2] = 0.125 * ( 1.0 + xi ) * ( 1.0 + eta ) * ( 1.0 - zeta );
  Ni[3] = 0.125 * ( 1.0 + xi ) * ( 1.0 - eta ) * ( 1.0 - zeta );

  Ni[4] = 0.125 * ( 1.0 - xi ) * ( 1.0 - eta ) * ( 1.0 + zeta );
  Ni[5] = 0.125 * ( 1.0 - xi ) * ( 1.0 + eta ) * ( 1.0 + zeta );
  Ni[6] = 0.125 * ( 1.0 + xi ) * ( 1.0 + eta ) * ( 1.0 + zeta );
  Ni[7] = 0.125 * ( 1.0 + xi ) * ( 1.0 - eta ) * ( 1.0 + zeta );
}

static void ShapeFunctionQ13D_Evaluate_dxi(PetscScalar _xi[],PetscScalar GNi[][NODES_PER_EL])
{
  PetscReal xi   = PetscRealPart(_xi[0]);
  PetscReal eta  = PetscRealPart(_xi[1]);
  PetscReal zeta = PetscRealPart(_xi[2]);
  /* xi deriv */
  GNi[0][0] = - 0.125 * ( 1.0 - eta ) * ( 1.0 - zeta );
  GNi[0][1] = - 0.125 * ( 1.0 + eta ) * ( 1.0 - zeta );
  GNi[0][2] =   0.125 * ( 1.0 + eta ) * ( 1.0 - zeta );
  GNi[0][3] =   0.125 * ( 1.0 - eta ) * ( 1.0 - zeta );

  GNi[0][4] = - 0.125 * ( 1.0 - eta ) * ( 1.0 + zeta );
  GNi[0][5] = - 0.125 * ( 1.0 + eta ) * ( 1.0 + zeta );
  GNi[0][6] =   0.125 * ( 1.0 + eta ) * ( 1.0 + zeta );
  GNi[0][7] =   0.125 * ( 1.0 - eta ) * ( 1.0 + zeta );
  /* eta deriv */
  GNi[1][0] = - 0.125 * ( 1.0 - xi ) * ( 1.0 - zeta );
  GNi[1][1] =   0.125 * ( 1.0 - xi ) * ( 1.0 - zeta );
  GNi[1][2] =   0.125 * ( 1.0 + xi ) * ( 1.0 - zeta );
  GNi[1][3] = - 0.125 * ( 1.0 + xi ) * ( 1.0 - zeta );

  GNi[1][4] = - 0.125 * ( 1.0 - xi ) * ( 1.0 + zeta );
  GNi[1][5] =   0.125 * ( 1.0 - xi ) * ( 1.0 + zeta );
  GNi[1][6] =   0.125 * ( 1.0 + xi ) * ( 1.0 + zeta );
  GNi[1][7] = - 0.125 * ( 1.0 + xi ) * ( 1.0 + zeta );
  /* zeta deriv */
  GNi[2][0] = -0.125 * ( 1.0 - xi ) * ( 1.0 - eta );
  GNi[2][1] = -0.125 * ( 1.0 - xi ) * ( 1.0 + eta );
  GNi[2][2] = -0.125 * ( 1.0 + xi ) * ( 1.0 + eta );
  GNi[2][3] = -0.125 * ( 1.0 + xi ) * ( 1.0 - eta );

  GNi[2][4] = 0.125 * ( 1.0 - xi ) * ( 1.0 - eta );
  GNi[2][5] = 0.125 * ( 1.0 - xi ) * ( 1.0 + eta );
  GNi[2][6] = 0.125 * ( 1.0 + xi ) * ( 1.0 + eta );
  GNi[2][7] = 0.125 * ( 1.0 + xi ) * ( 1.0 - eta );
}

static void matrix_inverse_3x3(PetscScalar A[3][3],PetscScalar B[3][3])
{
  PetscScalar t4, t6, t8, t10, t12, t14, t17;

  t4 = A[2][0] * A[0][1];
  t6 = A[2][0] * A[0][2];
  t8 = A[1][0] * A[0][1];
  t10 = A[1][0] * A[0][2];
  t12 = A[0][0] * A[1][1];
  t14 = A[0][0] * A[1][2];
  t17 = 0.1e1 / (t4 * A[1][2] - t6 * A[1][1] - t8 * A[2][2] + t10 * A[2][1] + t12 * A[2][2] - t14 * A[2][1]);

  B[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) * t17;
  B[0][1] = -(A[0][1] * A[2][2] - A[0][2] * A[2][1]) * t17;
  B[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * t17;
  B[1][0] = -(-A[2][0] * A[1][2] + A[1][0] * A[2][2]) * t17;
  B[1][1] = (-t6 + A[0][0] * A[2][2]) * t17;
  B[1][2] = -(-t10 + t14) * t17;
  B[2][0] = (-A[2][0] * A[1][1] + A[1][0] * A[2][1]) * t17;
  B[2][1] = -(-t4 + A[0][0] * A[2][1]) * t17;
  B[2][2] = (-t8 + t12) * t17;
}

static void ShapeFunctionQ13D_Evaluate_dx(PetscScalar GNi[][NODES_PER_EL],PetscScalar GNx[][NODES_PER_EL],PetscScalar coords[],PetscScalar *det_J)
{
  PetscScalar J00,J01,J02,J10,J11,J12,J20,J21,J22;
  PetscInt n;
  PetscScalar iJ[3][3],JJ[3][3];

  J00 = J01 = J02 = 0.0;
  J10 = J11 = J12 = 0.0;
  J20 = J21 = J22 = 0.0;
  for (n=0; n<NODES_PER_EL; n++) {
    PetscScalar cx = coords[ NSD*n + 0 ];
    PetscScalar cy = coords[ NSD*n + 1 ];
    PetscScalar cz = coords[ NSD*n + 2 ];

    /* J_ij = d(x_j) / d(xi_i) *//* J_ij = \sum _I GNi[j][I} * x_i */
    J00 = J00 + GNi[0][n] * cx;  /* J_xx */
    J01 = J01 + GNi[0][n] * cy;  /* J_xy = dx/deta */
    J02 = J02 + GNi[0][n] * cz;  /* J_xz = dx/dzeta */

    J10 = J10 + GNi[1][n] * cx;  /* J_yx = dy/dxi */
    J11 = J11 + GNi[1][n] * cy;  /* J_yy */
    J12 = J12 + GNi[1][n] * cz;  /* J_yz */

    J20 = J20 + GNi[2][n] * cx;  /* J_zx */
    J21 = J21 + GNi[2][n] * cy;  /* J_zy */
    J22 = J22 + GNi[2][n] * cz;  /* J_zz */
  }

  JJ[0][0] = J00;			JJ[0][1] = J01;			JJ[0][2] = J02;
  JJ[1][0] = J10;			JJ[1][1] = J11;			JJ[1][2] = J12;
  JJ[2][0] = J20;			JJ[2][1] = J21;			JJ[2][2] = J22;

  matrix_inverse_3x3(JJ,iJ);

  *det_J = J00*J11*J22 - J00*J12*J21 - J10*J01*J22 + J10*J02*J21 + J20*J01*J12 - J20*J02*J11;

  for (n=0; n<NODES_PER_EL; n++) {
    GNx[0][n] = GNi[0][n]*iJ[0][0] + GNi[1][n]*iJ[0][1] + GNi[2][n]*iJ[0][2];
    GNx[1][n] = GNi[0][n]*iJ[1][0] + GNi[1][n]*iJ[1][1] + GNi[2][n]*iJ[1][2];
    GNx[2][n] = GNi[0][n]*iJ[2][0] + GNi[1][n]*iJ[2][1] + GNi[2][n]*iJ[2][2];
  }
}

static void ConstructGaussQuadrature3D(PetscInt *ngp,PetscScalar gp_xi[][NSD],PetscScalar gp_weight[])
{
  *ngp         = 8;
  gp_xi[0][0]  = -0.57735026919; gp_xi[0][1] = -0.57735026919; gp_xi[0][2] = -0.57735026919;
  gp_xi[1][0]  = -0.57735026919; gp_xi[1][1] =  0.57735026919; gp_xi[1][2] = -0.57735026919;
  gp_xi[2][0]  =  0.57735026919; gp_xi[2][1] =  0.57735026919; gp_xi[2][2] = -0.57735026919;
  gp_xi[3][0]  =  0.57735026919; gp_xi[3][1] = -0.57735026919; gp_xi[3][2] = -0.57735026919;

  gp_xi[4][0]  = -0.57735026919; gp_xi[4][1] = -0.57735026919; gp_xi[4][2] =  0.57735026919;
  gp_xi[5][0]  = -0.57735026919; gp_xi[5][1] =  0.57735026919; gp_xi[5][2] =  0.57735026919;
  gp_xi[6][0]  =  0.57735026919; gp_xi[6][1] =  0.57735026919; gp_xi[6][2] =  0.57735026919;
  gp_xi[7][0]  =  0.57735026919; gp_xi[7][1] = -0.57735026919; gp_xi[7][2] =  0.57735026919;

  gp_weight[0] = 1.0;
  gp_weight[1] = 1.0;
  gp_weight[2] = 1.0;
  gp_weight[3] = 1.0;

  gp_weight[4] = 1.0;
  gp_weight[5] = 1.0;
  gp_weight[6] = 1.0;
  gp_weight[7] = 1.0;
}

/* procs to the left claim the ghost node as their element */
#undef __FUNCT__
#define __FUNCT__ "DMDAGetLocalElementSize"
static PetscErrorCode DMDAGetLocalElementSize(DM da,PetscInt *mxl,PetscInt *myl,PetscInt *mzl)
{
  PetscInt m,n,p,M,N,P;
  PetscInt sx,sy,sz;
  PetscInt ml,nl,pl;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMDAGetInfo(da,0,&M,&N,&P,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
  ierr = DMDAGetCorners(da,&sx,&sy,&sz,&m,&n,&p);CHKERRQ(ierr);

  ml = nl = pl = 0;
  if (mxl) {
    *mxl = m;
    if ((sx+m) == M) {  /* last proc */
      *mxl = m-1;
    }
    ml = *mxl;
  }
  if (myl) {
    *myl = n;
    if ((sy+n) == N) {  /* last proc */
      *myl = n-1;
    }
    nl = *myl;
  }
  if (mzl) {
    *mzl = p;
    if ((sz+p) == P) {  /* last proc */
      *mzl = p-1;
    }
    pl = *mzl;
  }

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMDAGetElementCorners"
static PetscErrorCode DMDAGetElementCorners(DM da,PetscInt *sx,PetscInt *sy,PetscInt *sz,PetscInt *mx,PetscInt *my,PetscInt *mz)
{
  PetscInt si,sj,sk;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMDAGetGhostCorners(da,&si,&sj,&sk,0,0,0);CHKERRQ(ierr);

  if (sx) {
    *sx = si;
    if (si != 0) {
      *sx = si+1;
    }
  }
  if (sy) {
    *sy = sj;
    if (sj != 0) {
      *sy = sj+1;
    }
  }
  if (sz) {
    *sz = sk;
    if (sk != 0) {
      *sz = sk+1;
    }
  }
  ierr = DMDAGetLocalElementSize(da,mx,my,mz);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMDAGetElementOwnershipRanges3d"
static PetscErrorCode DMDAGetElementOwnershipRanges3d(DM da,PetscInt **_lx,PetscInt **_ly,PetscInt **_lz)
{
  PetscMPIInt nproc,rank;
  MPI_Comm comm;
  PetscInt M,N,P,pM,pN,pP;
  PetscInt i,j,k,dim,esi,esj,esk,mx,my,mz;
  PetscInt *lmx,*lmy,*lmz,*tmp;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* create file name */
  PetscObjectGetComm( (PetscObject)da, &comm );
  MPI_Comm_size( comm, &nproc );
  MPI_Comm_rank( comm, &rank );

  ierr = DMDAGetInfo( da, &dim, &M,&N,&P, &pM,&pN,&pP, 0, 0, 0,0,0,0 );CHKERRQ(ierr);
  ierr = DMDAGetElementCorners(da,&esi,&esj,&esk,&mx,&my,&mz);CHKERRQ(ierr);

  if (dim == 1) {
    pN = 1;
    pP = 1;
  }
  if (dim == 2) {
    pP = 1;
  }

  ierr = PetscMalloc( sizeof(PetscInt)*(nproc), &tmp );CHKERRQ(ierr);

  ierr = PetscMalloc( sizeof(PetscInt)*(pM+1), &lmx );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pN+1), &lmy );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pP+1), &lmz );CHKERRQ(ierr);

  if (dim >= 1) {
    ierr = MPI_Allgather ( &mx, 1, MPIU_INT, tmp, 1, MPIU_INT, comm );CHKERRQ(ierr);
    j = k = 0;
    for( i=0; i<pM; i++ ) {
      PetscInt procid = i + j*pM; /* convert proc(i,j,k) to pid */
      lmx[i] = tmp[procid];
    }
  }

  if (dim >= 2 ) {
    ierr = MPI_Allgather ( &my, 1, MPIU_INT, tmp, 1, MPIU_INT, comm );CHKERRQ(ierr);
    i = k = 0;
    for( j=0; j<pN; j++ ) {
      PetscInt procid = i + j*pM; /* convert proc(i,j,k) to pid */
      lmy[j] = tmp[procid];
    }
  }

  if (dim == 3 ) {
    ierr = MPI_Allgather ( &mz, 1, MPIU_INT, tmp, 1, MPIU_INT, comm );CHKERRQ(ierr);
    i = j = 0;
    for( k=0; k<pP; k++ ) {
      PetscInt procid = i + j*pM + k*pM*pN; /* convert proc(i,j,k) to pid */
      lmz[k] = tmp[procid];
    }
  }

  if(_lx) { *_lx = lmx; }
  if(_ly) { *_ly = lmy; }
  if(_lz) { *_lz = lmz; }

  ierr = PetscFree(tmp);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

/*
 i,j are the element indices
 The unknown is a vector quantity.
 The s[].c is used to indicate the degree of freedom.
 */
#undef __FUNCT__
#define __FUNCT__ "DMDAGetElementEqnums3D_up"
static PetscErrorCode DMDAGetElementEqnums3D_up(MatStencil s_u[],MatStencil s_p[],PetscInt i,PetscInt j,PetscInt k)
{
  PetscInt n;
  PetscFunctionBegin;
  /* velocity */
  n = 0;
  /* node 0 */
  s_u[n].i = i; s_u[n].j = j; s_u[n].k = k; s_u[n].c = 0; n++; /* Vx0 */
  s_u[n].i = i; s_u[n].j = j; s_u[n].k = k; s_u[n].c = 1; n++; /* Vy0 */
  s_u[n].i = i; s_u[n].j = j; s_u[n].k = k; s_u[n].c = 2; n++; /* Vz0 */

  s_u[n].i = i; s_u[n].j = j+1; s_u[n].k = k; s_u[n].c = 0; n++;
  s_u[n].i = i; s_u[n].j = j+1; s_u[n].k = k; s_u[n].c = 1; n++;
  s_u[n].i = i; s_u[n].j = j+1; s_u[n].k = k; s_u[n].c = 2; n++;

  s_u[n].i = i+1; s_u[n].j = j+1; s_u[n].k = k; s_u[n].c = 0; n++;
  s_u[n].i = i+1; s_u[n].j = j+1; s_u[n].k = k; s_u[n].c = 1; n++;
  s_u[n].i = i+1; s_u[n].j = j+1; s_u[n].k = k; s_u[n].c = 2; n++;

  s_u[n].i = i+1; s_u[n].j = j; s_u[n].k = k; s_u[n].c = 0; n++;
  s_u[n].i = i+1; s_u[n].j = j; s_u[n].k = k; s_u[n].c = 1; n++;
  s_u[n].i = i+1; s_u[n].j = j; s_u[n].k = k; s_u[n].c = 2; n++;

  /* */
  s_u[n].i = i; s_u[n].j = j; s_u[n].k = k+1; s_u[n].c = 0; n++; /* Vx4 */
  s_u[n].i = i; s_u[n].j = j; s_u[n].k = k+1; s_u[n].c = 1; n++; /* Vy4 */
  s_u[n].i = i; s_u[n].j = j; s_u[n].k = k+1; s_u[n].c = 2; n++; /* Vz4 */

  s_u[n].i = i; s_u[n].j = j+1; s_u[n].k = k+1; s_u[n].c = 0; n++;
  s_u[n].i = i; s_u[n].j = j+1; s_u[n].k = k+1; s_u[n].c = 1; n++;
  s_u[n].i = i; s_u[n].j = j+1; s_u[n].k = k+1; s_u[n].c = 2; n++;

  s_u[n].i = i+1; s_u[n].j = j+1; s_u[n].k = k+1; s_u[n].c = 0; n++;
  s_u[n].i = i+1; s_u[n].j = j+1; s_u[n].k = k+1; s_u[n].c = 1; n++;
  s_u[n].i = i+1; s_u[n].j = j+1; s_u[n].k = k+1; s_u[n].c = 2; n++;

  s_u[n].i = i+1; s_u[n].j = j; s_u[n].k = k+1; s_u[n].c = 0; n++;
  s_u[n].i = i+1; s_u[n].j = j; s_u[n].k = k+1; s_u[n].c = 1; n++;
  s_u[n].i = i+1; s_u[n].j = j; s_u[n].k = k+1; s_u[n].c = 2; n++;

  /* pressure */
  n = 0;
  s_p[n].i = i;   s_p[n].j = j;   s_p[n].k = k; s_p[n].c = 3; n++; /* P0 */
  s_p[n].i = i;   s_p[n].j = j+1; s_p[n].k = k; s_p[n].c = 3; n++;
  s_p[n].i = i+1; s_p[n].j = j+1; s_p[n].k = k; s_p[n].c = 3; n++;
  s_p[n].i = i+1; s_p[n].j = j;   s_p[n].k = k; s_p[n].c = 3; n++;

  s_p[n].i = i;   s_p[n].j = j;   s_p[n].k = k+1; s_p[n].c = 3; n++; /* P0 */
  s_p[n].i = i;   s_p[n].j = j+1; s_p[n].k = k+1; s_p[n].c = 3; n++;
  s_p[n].i = i+1; s_p[n].j = j+1; s_p[n].k = k+1; s_p[n].c = 3; n++;
  s_p[n].i = i+1; s_p[n].j = j;   s_p[n].k = k+1; s_p[n].c = 3; n++;

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "GetElementCoords3D"
static PetscErrorCode GetElementCoords3D(DMDACoor3d ***coords,PetscInt i,PetscInt j,PetscInt k,PetscScalar el_coord[])
{
  PetscFunctionBegin;
  /* get coords for the element */
  el_coord[0] = coords[k  ][j  ][i  ].x;
  el_coord[1] = coords[k  ][j  ][i  ].y;
  el_coord[2] = coords[k  ][j  ][i  ].z;

  el_coord[3] = coords[k  ][j+1][i].x;
  el_coord[4] = coords[k  ][j+1][i].y;
  el_coord[5] = coords[k  ][j+1][i].z;

  el_coord[6] = coords[k  ][j+1][i+1].x;
  el_coord[7] = coords[k  ][j+1][i+1].y;
  el_coord[8] = coords[k  ][j+1][i+1].z;

  el_coord[9 ] = coords[k  ][j  ][i+1].x;
  el_coord[10] = coords[k  ][j  ][i+1].y;
  el_coord[11] = coords[k  ][j  ][i+1].z;

  el_coord[12] = coords[k+1][j  ][i  ].x;
  el_coord[13] = coords[k+1][j  ][i  ].y;
  el_coord[14] = coords[k+1][j  ][i  ].z;

  el_coord[15] = coords[k+1][j+1][i  ].x;
  el_coord[16] = coords[k+1][j+1][i  ].y;
  el_coord[17] = coords[k+1][j+1][i  ].z;

  el_coord[18] = coords[k+1][j+1][i+1].x;
  el_coord[19] = coords[k+1][j+1][i+1].y;
  el_coord[20] = coords[k+1][j+1][i+1].z;

  el_coord[21] = coords[k+1][j  ][i+1].x;
  el_coord[22] = coords[k+1][j  ][i+1].y;
  el_coord[23] = coords[k+1][j  ][i+1].z;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "StokesDAGetNodalFields3D"
static PetscErrorCode StokesDAGetNodalFields3D(StokesDOF ***field,PetscInt i,PetscInt j,PetscInt k,StokesDOF nodal_fields[])
{
  PetscFunctionBegin;
  /* get the nodal fields for u */
  nodal_fields[0].u_dof	=	field[k  ][j  ][i  ].u_dof;
  nodal_fields[0].v_dof	=	field[k  ][j  ][i  ].v_dof;
  nodal_fields[0].w_dof	=	field[k  ][j  ][i  ].w_dof;

  nodal_fields[1].u_dof	=	field[k  ][j+1][i  ].u_dof;
  nodal_fields[1].v_dof	=	field[k  ][j+1][i  ].v_dof;
  nodal_fields[1].w_dof	=	field[k  ][j+1][i  ].w_dof;

  nodal_fields[2].u_dof	=	field[k  ][j+1][i+1].u_dof;
  nodal_fields[2].v_dof	=	field[k  ][j+1][i+1].v_dof;
  nodal_fields[2].w_dof	=	field[k  ][j+1][i+1].w_dof;

  nodal_fields[3].u_dof	=	field[k  ][j  ][i+1].u_dof;
  nodal_fields[3].v_dof	=	field[k  ][j  ][i+1].v_dof;
  nodal_fields[3].w_dof	=	field[k  ][j  ][i+1].w_dof;

  nodal_fields[4].u_dof	=	field[k+1][j  ][i  ].u_dof;
  nodal_fields[4].v_dof	=	field[k+1][j  ][i  ].v_dof;
  nodal_fields[4].w_dof	=	field[k+1][j  ][i  ].w_dof;

  nodal_fields[5].u_dof	=	field[k+1][j+1][i  ].u_dof;
  nodal_fields[5].v_dof	=	field[k+1][j+1][i  ].v_dof;
  nodal_fields[5].w_dof	=	field[k+1][j+1][i  ].w_dof;

  nodal_fields[6].u_dof	=	field[k+1][j+1][i+1].u_dof;
  nodal_fields[6].v_dof	=	field[k+1][j+1][i+1].v_dof;
  nodal_fields[6].w_dof	=	field[k+1][j+1][i+1].w_dof;

  nodal_fields[7].u_dof	=	field[k+1][j  ][i+1].u_dof;
  nodal_fields[7].v_dof	=	field[k+1][j  ][i+1].v_dof;
  nodal_fields[7].w_dof	=	field[k+1][j  ][i+1].w_dof;

  /* get the nodal fields for p */
  nodal_fields[0].p_dof	=	field[k  ][j  ][i  ].p_dof;
  nodal_fields[1].p_dof	=	field[k  ][j+1][i  ].p_dof;
  nodal_fields[2].p_dof	=	field[k  ][j+1][i+1].p_dof;
  nodal_fields[3].p_dof	=	field[k  ][j  ][i+1].p_dof;

  nodal_fields[4].p_dof	=	field[k+1][j  ][i  ].p_dof;
  nodal_fields[5].p_dof	=	field[k+1][j+1][i  ].p_dof;
  nodal_fields[6].p_dof	=	field[k+1][j+1][i+1].p_dof;
  nodal_fields[7].p_dof	=	field[k+1][j  ][i+1].p_dof;

  PetscFunctionReturn(0);
}

static PetscInt ASS_MAP_wIwDI_uJuDJ(
  PetscInt wi,PetscInt wd,PetscInt w_NPE,PetscInt w_dof,
  PetscInt ui,PetscInt ud,PetscInt u_NPE,PetscInt u_dof)
{
  PetscInt ij;
  PetscInt r,c,nr,nc;

  nr = w_NPE*w_dof;
  nc = u_NPE*u_dof;

  r = w_dof*wi+wd;
  c = u_dof*ui+ud;

  ij = r*nc+c;

  return ij;
}

#undef __FUNCT__
#define __FUNCT__ "DMDASetValuesLocalStencil3D_ADD_VALUES"
static PetscErrorCode DMDASetValuesLocalStencil3D_ADD_VALUES(StokesDOF ***fields_F,MatStencil u_eqn[],MatStencil p_eqn[],PetscScalar Fe_u[],PetscScalar Fe_p[])
{
  PetscInt n,I,J,K;

  PetscFunctionBegin;
  for (n = 0; n<NODES_PER_EL; n++) {

    I = u_eqn[NSD*n  ].i;
    J = u_eqn[NSD*n  ].j;
    K = u_eqn[NSD*n  ].k;
    fields_F[K][J][I].u_dof = fields_F[K][J][I].u_dof+Fe_u[NSD*n  ];

    I = u_eqn[NSD*n+1].i;
    J = u_eqn[NSD*n+1].j;
    K = u_eqn[NSD*n+1].k;
    fields_F[K][J][I].v_dof = fields_F[K][J][I].v_dof+Fe_u[NSD*n+1];

    I = u_eqn[NSD*n+2].i;
    J = u_eqn[NSD*n+2].j;
    K = u_eqn[NSD*n+2].k;
    fields_F[K][J][I].w_dof = fields_F[K][J][I].w_dof+Fe_u[NSD*n+2];


    I = p_eqn[n].i;
    J = p_eqn[n].j;
    K = p_eqn[n].k;
    fields_F[K][J][I].p_dof = fields_F[K][J][I].p_dof+Fe_p[n ];

  }
  PetscFunctionReturn(0);
}

static void FormStressOperatorQ13D(PetscScalar Ke[],PetscScalar coords[],PetscScalar eta[])
{
  PetscInt    ngp;
  PetscScalar gp_xi[GAUSS_POINTS][NSD];
  PetscScalar gp_weight[GAUSS_POINTS];
  PetscInt    p,i,j,k;
  PetscScalar GNi_p[NSD][NODES_PER_EL],GNx_p[NSD][NODES_PER_EL];
  PetscScalar J_p,tildeD[6];
  PetscScalar B[6][U_DOFS*NODES_PER_EL];
  const PetscInt nvdof = U_DOFS*NODES_PER_EL;

  /* define quadrature rule */
  ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

  /* evaluate integral */
  for (p = 0; p < ngp; p++) {
    ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
    ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,coords,&J_p);

    for (i = 0; i < NODES_PER_EL; i++) {
      PetscScalar d_dx_i = GNx_p[0][i];
      PetscScalar d_dy_i = GNx_p[1][i];
      PetscScalar d_dz_i = GNx_p[2][i];

      B[0][3*i  ] = d_dx_i; B[0][3*i+1] = 0.0;    B[0][3*i+2] = 0.0;
      B[1][3*i  ] = 0.0;    B[1][3*i+1] = d_dy_i; B[1][3*i+2] = 0.0;
      B[2][3*i  ] = 0.0;    B[2][3*i+1] = 0.0;    B[2][3*i+2] = d_dz_i;

      B[3][3*i] = d_dy_i; B[3][3*i+1] = d_dx_i;  B[3][3*i+2] = 0.0;   /* e_xy */
      B[4][3*i] = d_dz_i; B[4][3*i+1] = 0.0;     B[4][3*i+2] = d_dx_i;/* e_xz */
      B[5][3*i] = 0.0;    B[5][3*i+1] = d_dz_i;  B[5][3*i+2] = d_dy_i;/* e_yz */
    }


    tildeD[0] = 2.0*gp_weight[p]*J_p*eta[p];
    tildeD[1] = 2.0*gp_weight[p]*J_p*eta[p];
    tildeD[2] = 2.0*gp_weight[p]*J_p*eta[p];

    tildeD[3] =     gp_weight[p]*J_p*eta[p];
    tildeD[4] =     gp_weight[p]*J_p*eta[p];
    tildeD[5] =     gp_weight[p]*J_p*eta[p];

    /* form Bt tildeD B */
    /*
     Ke_ij = Bt_ik . D_kl . B_lj
     = B_ki . D_kl . B_lj
     = B_ki . D_kk . B_kj
     */
    for (i = 0; i < nvdof; i++) {
      for (j = i; j < nvdof; j++) {
        for (k = 0; k < 6; k++) {
          Ke[i*nvdof+j] += B[k][i]*tildeD[k]*B[k][j];
        }
      }
    }

  }
  /* fill lower triangular part */
#ifdef ASSEMBLE_LOWER_TRIANGULAR
  for (i = 0; i < nvdof; i++) {
    for (j = i; j < nvdof; j++) {
      Ke[j*nvdof+i] = Ke[i*nvdof+j];
    }
  }
#endif
}

static void FormGradientOperatorQ13D(PetscScalar Ke[],PetscScalar coords[])
{
  PetscInt    ngp;
  PetscScalar gp_xi[GAUSS_POINTS][NSD];
  PetscScalar gp_weight[GAUSS_POINTS];
  PetscInt    p,i,j,di;
  PetscScalar Ni_p[NODES_PER_EL];
  PetscScalar GNi_p[NSD][NODES_PER_EL],GNx_p[NSD][NODES_PER_EL];
  PetscScalar J_p,fac;

  /* define quadrature rule */
  ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

  /* evaluate integral */
  for (p = 0; p < ngp; p++) {
    ShapeFunctionQ13D_Evaluate(gp_xi[p],Ni_p);
    ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
    ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,coords,&J_p);
    fac = gp_weight[p]*J_p;

    for (i = 0; i < NODES_PER_EL; i++) { /* u nodes */
      for (di = 0; di < NSD; di++) { /* u dofs */
        for (j = 0; j < NODES_PER_EL; j++) {  /* p nodes, p dofs = 1 (ie no loop) */
          PetscInt IJ;
          IJ = ASS_MAP_wIwDI_uJuDJ(i,di,NODES_PER_EL,3,j,0,NODES_PER_EL,1);

          Ke[IJ] = Ke[IJ]-GNx_p[di][i]*Ni_p[j]*fac;
        }
      }
    }
  }
}

static void FormDivergenceOperatorQ13D(PetscScalar De[],PetscScalar coords[])
{
  PetscScalar Ge[U_DOFS*NODES_PER_EL*P_DOFS*NODES_PER_EL];
  PetscInt    i,j;
  PetscInt    nr_g,nc_g;

  PetscMemzero(Ge,sizeof(PetscScalar)*U_DOFS*NODES_PER_EL*P_DOFS*NODES_PER_EL);
  FormGradientOperatorQ13D(Ge,coords);

  nr_g = U_DOFS*NODES_PER_EL;
  nc_g = P_DOFS*NODES_PER_EL;

  for (i = 0; i < nr_g; i++) {
    for (j = 0; j < nc_g; j++) {
      De[nr_g*j+i] = Ge[nc_g*i+j];
    }
  }
}

static void FormStabilisationOperatorQ13D(PetscScalar Ke[],PetscScalar coords[],PetscScalar eta[])
{
  PetscInt    ngp;
  PetscScalar gp_xi[GAUSS_POINTS][NSD];
  PetscScalar gp_weight[GAUSS_POINTS];
  PetscInt    p,i,j;
  PetscScalar Ni_p[NODES_PER_EL];
  PetscScalar GNi_p[NSD][NODES_PER_EL],GNx_p[NSD][NODES_PER_EL];
  PetscScalar J_p,fac,eta_avg;

  /* define quadrature rule */
  ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

  /* evaluate integral */
  for (p = 0; p < ngp; p++) {
    ShapeFunctionQ13D_Evaluate(gp_xi[p],Ni_p);
    ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
    ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,coords,&J_p);
    fac = gp_weight[p]*J_p;
    /*
     for (i = 0; i < NODES_PER_EL; i++) {
     for (j = i; j < NODES_PER_EL; j++) {
     Ke[NODES_PER_EL*i+j] -= fac*(Ni_p[i]*Ni_p[j]-0.015625);
     }
     }
     */

    for (i = 0; i < NODES_PER_EL; i++) {
      for (j = 0; j < NODES_PER_EL; j++) {
        Ke[NODES_PER_EL*i+j] -= fac*(Ni_p[i]*Ni_p[j]-0.015625);
      }
    }

  }

  /* scale */
  eta_avg = 0.0;
  for (p = 0; p < ngp; p++) {
    eta_avg += eta[p];
  }
  eta_avg = (1.0/((PetscScalar)ngp))*eta_avg;
  fac     = 1.0/eta_avg;

  /*
   for (i = 0; i < NODES_PER_EL; i++) {
   for (j = i; j < NODES_PER_EL; j++) {
   Ke[NODES_PER_EL*i+j] = fac*Ke[NODES_PER_EL*i+j];
   #ifdef ASSEMBLE_LOWER_TRIANGULAR
   Ke[NODES_PER_EL*j+i] = Ke[NODES_PER_EL*i+j];
   #endif
   }
   }
   */

  for (i = 0; i < NODES_PER_EL; i++) {
    for (j = 0; j < NODES_PER_EL; j++) {
      Ke[NODES_PER_EL*i+j] = fac*Ke[NODES_PER_EL*i+j];
    }
  }
}

static void FormScaledMassMatrixOperatorQ13D(PetscScalar Ke[],PetscScalar coords[],PetscScalar eta[])
{
  PetscInt    ngp;
  PetscScalar gp_xi[GAUSS_POINTS][NSD];
  PetscScalar gp_weight[GAUSS_POINTS];
  PetscInt    p,i,j;
  PetscScalar Ni_p[NODES_PER_EL];
  PetscScalar GNi_p[NSD][NODES_PER_EL],GNx_p[NSD][NODES_PER_EL];
  PetscScalar J_p,fac,eta_avg;

  /* define quadrature rule */
  ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

  /* evaluate integral */
  for (p = 0; p < ngp; p++) {
    ShapeFunctionQ13D_Evaluate(gp_xi[p],Ni_p);
    ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
    ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,coords,&J_p);
    fac = gp_weight[p]*J_p;

    /*
     for (i = 0; i < NODES_PER_EL; i++) {
     for (j = i; j < NODES_PER_EL; j++) {
     Ke[NODES_PER_EL*i+j] = Ke[NODES_PER_EL*i+j]-fac*Ni_p[i]*Ni_p[j];
     }
     }
     */

    for (i = 0; i < NODES_PER_EL; i++) {
      for (j = 0; j < NODES_PER_EL; j++) {
        Ke[NODES_PER_EL*i+j] = Ke[NODES_PER_EL*i+j]-fac*Ni_p[i]*Ni_p[j];
      }
    }

  }

  /* scale */
  eta_avg = 0.0;
  for (p = 0; p < ngp; p++) {
    eta_avg += eta[p];
  }
  eta_avg = (1.0/((PetscScalar)ngp))*eta_avg;
  fac     = 1.0/eta_avg;
  /*
   for (i = 0; i < NODES_PER_EL; i++) {
   for (j = i; j < NODES_PER_EL; j++) {
   Ke[NODES_PER_EL*i+j] = fac*Ke[NODES_PER_EL*i+j];
   #ifdef ASSEMBLE_LOWER_TRIANGULAR
   Ke[NODES_PER_EL*j+i] = Ke[NODES_PER_EL*i+j];
   #endif
   }
   }
   */

  for (i = 0; i < NODES_PER_EL; i++) {
    for (j = 0; j < NODES_PER_EL; j++) {
      Ke[NODES_PER_EL*i+j] = fac*Ke[NODES_PER_EL*i+j];
    }
  }

}

static void FormMomentumRhsQ13D(PetscScalar Fe[],PetscScalar coords[],PetscScalar fx[],PetscScalar fy[],PetscScalar fz[])
{
  PetscInt    ngp;
  PetscScalar gp_xi[GAUSS_POINTS][NSD];
  PetscScalar gp_weight[GAUSS_POINTS];
  PetscInt    p,i;
  PetscScalar Ni_p[NODES_PER_EL];
  PetscScalar GNi_p[NSD][NODES_PER_EL],GNx_p[NSD][NODES_PER_EL];
  PetscScalar J_p,fac;

  /* define quadrature rule */
  ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

  /* evaluate integral */
  for (p = 0; p < ngp; p++) {
    ShapeFunctionQ13D_Evaluate(gp_xi[p],Ni_p);
    ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
    ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,coords,&J_p);
    fac = gp_weight[p]*J_p;

    for (i = 0; i < NODES_PER_EL; i++) {
      Fe[NSD*i  ] -= fac*Ni_p[i]*fx[p];
      Fe[NSD*i+1] -= fac*Ni_p[i]*fy[p];
      Fe[NSD*i+2] -= fac*Ni_p[i]*fz[p];
    }
  }
}

static void FormContinuityRhsQ13D(PetscScalar Fe[],PetscScalar coords[],PetscScalar hc[])
{
  PetscInt    ngp;
  PetscScalar gp_xi[GAUSS_POINTS][NSD];
  PetscScalar gp_weight[GAUSS_POINTS];
  PetscInt    p,i;
  PetscScalar Ni_p[NODES_PER_EL];
  PetscScalar GNi_p[NSD][NODES_PER_EL],GNx_p[NSD][NODES_PER_EL];
  PetscScalar J_p,fac;

  /* define quadrature rule */
  ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

  /* evaluate integral */
  for (p = 0; p < ngp; p++) {
    ShapeFunctionQ13D_Evaluate(gp_xi[p],Ni_p);
    ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
    ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,coords,&J_p);
    fac = gp_weight[p]*J_p;

    for (i = 0; i < NODES_PER_EL; i++) {
      Fe[i] -= fac*Ni_p[i]*hc[p];
    }
  }
}

#define _ZERO_ROWCOL_i(A,i) {                   \
    PetscInt KK;                                \
    PetscScalar tmp = A[24*(i)+(i)];            \
    for(KK=0;KK<24;KK++){A[24*(i)+KK]=0.0;}     \
    for(KK=0;KK<24;KK++){A[24*KK+(i)]=0.0;}     \
    A[24*(i)+(i)] = tmp;}                       \

#define _ZERO_ROW_i(A,i) {                      \
    PetscInt KK;                                \
    for(KK=0;KK<8;KK++){A[8*(i)+KK]=0.0;}}

#define _ZERO_COL_i(A,i) {                      \
    PetscInt KK;                                \
    for(KK=0;KK<8;KK++){A[24*KK+(i)]=0.0;}}

#undef __FUNCT__
#define __FUNCT__ "AssembleA_Stokes"
static PetscErrorCode AssembleA_Stokes(Mat A,DM stokes_da,DM properties_da,Vec properties)
{
  DM                     cda;
  Vec                    coords;
  DMDACoor3d             ***_coords;
  MatStencil             u_eqn[NODES_PER_EL*U_DOFS];
  MatStencil             p_eqn[NODES_PER_EL*P_DOFS];
  PetscInt               sex,sey,sez,mx,my,mz;
  PetscInt               ei,ej,ek;
  PetscScalar            Ae[NODES_PER_EL*U_DOFS*NODES_PER_EL*U_DOFS];
  PetscScalar            Ge[NODES_PER_EL*U_DOFS*NODES_PER_EL*P_DOFS];
  PetscScalar            De[NODES_PER_EL*P_DOFS*NODES_PER_EL*U_DOFS];
  PetscScalar            Ce[NODES_PER_EL*P_DOFS*NODES_PER_EL*P_DOFS];
  PetscScalar            el_coords[NODES_PER_EL*NSD];
  Vec                    local_properties;
  GaussPointCoefficients ***props;
  PetscScalar            *prop_eta;
  PetscInt               n,M,N,P;
  PetscLogDouble         t0,t1;
  PetscErrorCode         ierr;

  PetscFunctionBegin;

  ierr = DMDAGetInfo(stokes_da,0,&M,&N,&P,0,0,0, 0,0,0,0,0,0);CHKERRQ(ierr);
  /* setup for coords */
  ierr = DMDAGetCoordinateDA(stokes_da,&cda);CHKERRQ(ierr);
  ierr = DMDAGetGhostedCoordinates(stokes_da,&coords);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(cda,coords,&_coords);CHKERRQ(ierr);

  /* setup for coefficients */
  ierr = DMCreateLocalVector(properties_da,&local_properties);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(properties_da,properties,INSERT_VALUES,local_properties);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(properties_da,properties,INSERT_VALUES,local_properties);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(properties_da,local_properties,&props);CHKERRQ(ierr);

  ierr = DMDAGetElementCorners(stokes_da,&sex,&sey,&sez,&mx,&my,&mz);CHKERRQ(ierr);
  PetscGetTime(&t0);
  for (ek = sez; ek < sez+mz; ek++) {
    for (ej = sey; ej < sey+my; ej++) {
      for (ei = sex; ei < sex+mx; ei++) {
        /* get coords for the element */
        GetElementCoords3D(_coords,ei,ej,ek,el_coords);

        /* get coefficients for the element */
        prop_eta = props[ek][ej][ei].eta;

        /* initialise element stiffness matrix */
        ierr = PetscMemzero(Ae,sizeof(PetscScalar)*NODES_PER_EL*U_DOFS*NODES_PER_EL*U_DOFS);CHKERRQ(ierr);
        ierr = PetscMemzero(Ge,sizeof(PetscScalar)*NODES_PER_EL*U_DOFS*NODES_PER_EL*P_DOFS);CHKERRQ(ierr);
        ierr = PetscMemzero(De,sizeof(PetscScalar)*NODES_PER_EL*P_DOFS*NODES_PER_EL*U_DOFS);CHKERRQ(ierr);
        ierr = PetscMemzero(Ce,sizeof(PetscScalar)*NODES_PER_EL*P_DOFS*NODES_PER_EL*P_DOFS);CHKERRQ(ierr);

        /* form element stiffness matrix */
        FormStressOperatorQ13D(Ae,el_coords,prop_eta);
        FormGradientOperatorQ13D(Ge,el_coords);
        /*#ifdef ASSEMBLE_LOWER_TRIANGULAR*/
        FormDivergenceOperatorQ13D(De,el_coords);
        /*#endif*/
        FormStabilisationOperatorQ13D(Ce,el_coords,prop_eta);

        /* insert element matrix into global matrix */
        ierr = DMDAGetElementEqnums3D_up(u_eqn,p_eqn,ei,ej,ek);CHKERRQ(ierr);

        for (n=0; n<NODES_PER_EL; n++) {
          if ( (u_eqn[3*n  ].i == 0) || (u_eqn[3*n  ].i == M-1) ) {
            _ZERO_ROWCOL_i(Ae,3*n);
            _ZERO_ROW_i   (Ge,3*n);
            _ZERO_COL_i   (De,3*n);
          }

          if ( (u_eqn[3*n+1].j == 0) || (u_eqn[3*n+1].j == N-1) ) {
            _ZERO_ROWCOL_i(Ae,3*n+1);
            _ZERO_ROW_i   (Ge,3*n+1);
            _ZERO_COL_i   (De,3*n+1);
          }

          if ( (u_eqn[3*n+2].k == 0) || (u_eqn[3*n+2].k == P-1) ) {
            _ZERO_ROWCOL_i(Ae,3*n+2);
            _ZERO_ROW_i   (Ge,3*n+2);
            _ZERO_COL_i   (De,3*n+2);
          }
        }
        ierr = MatSetValuesStencil(A,NODES_PER_EL*U_DOFS,u_eqn,NODES_PER_EL*U_DOFS,u_eqn,Ae,ADD_VALUES);CHKERRQ(ierr);
        ierr = MatSetValuesStencil(A,NODES_PER_EL*U_DOFS,u_eqn,NODES_PER_EL*P_DOFS,p_eqn,Ge,ADD_VALUES);CHKERRQ(ierr);
        ierr = MatSetValuesStencil(A,NODES_PER_EL*P_DOFS,p_eqn,NODES_PER_EL*U_DOFS,u_eqn,De,ADD_VALUES);CHKERRQ(ierr);
        ierr = MatSetValuesStencil(A,NODES_PER_EL*P_DOFS,p_eqn,NODES_PER_EL*P_DOFS,p_eqn,Ce,ADD_VALUES);CHKERRQ(ierr);
      }
    }
  }
  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  ierr = DMDAVecRestoreArray(cda,coords,&_coords);CHKERRQ(ierr);

  ierr = DMDAVecRestoreArray(properties_da,local_properties,&props);CHKERRQ(ierr);
  ierr = VecDestroy(&local_properties);CHKERRQ(ierr);
  PetscGetTime(&t1);

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "AssembleA_PCStokes"
static PetscErrorCode AssembleA_PCStokes(Mat A,DM stokes_da,DM properties_da,Vec properties)
{
  DM                     cda;
  Vec                    coords;
  DMDACoor3d             ***_coords;
  MatStencil             u_eqn[NODES_PER_EL*U_DOFS];
  MatStencil             p_eqn[NODES_PER_EL*P_DOFS];
  PetscInt               sex,sey,sez,mx,my,mz;
  PetscInt               ei,ej,ek;
  PetscScalar            Ae[NODES_PER_EL*U_DOFS*NODES_PER_EL*U_DOFS];
  PetscScalar            Ge[NODES_PER_EL*U_DOFS*NODES_PER_EL*P_DOFS];
  PetscScalar            De[NODES_PER_EL*P_DOFS*NODES_PER_EL*U_DOFS];
  PetscScalar            Ce[NODES_PER_EL*P_DOFS*NODES_PER_EL*P_DOFS];
  PetscScalar            el_coords[NODES_PER_EL*NSD];
  Vec                    local_properties;
  GaussPointCoefficients ***props;
  PetscScalar            *prop_eta;
  PetscInt               n,M,N,P;
  PetscErrorCode         ierr;

  PetscFunctionBegin;

  ierr = DMDAGetInfo(stokes_da,0,&M,&N,&P,0,0,0, 0,0,0,0,0,0);CHKERRQ(ierr);
  /* setup for coords */
  ierr = DMDAGetCoordinateDA(stokes_da,&cda);CHKERRQ(ierr);
  ierr = DMDAGetGhostedCoordinates(stokes_da,&coords);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(cda,coords,&_coords);CHKERRQ(ierr);

  /* setup for coefficients */
  ierr = DMCreateLocalVector(properties_da,&local_properties);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(properties_da,properties,INSERT_VALUES,local_properties);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(properties_da,properties,INSERT_VALUES,local_properties);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(properties_da,local_properties,&props);CHKERRQ(ierr);

  ierr = DMDAGetElementCorners(stokes_da,&sex,&sey,&sez,&mx,&my,&mz);CHKERRQ(ierr);
  for (ek = sez; ek < sez+mz; ek++) {
    for (ej = sey; ej < sey+my; ej++) {
      for (ei = sex; ei < sex+mx; ei++) {
        /* get coords for the element */
        GetElementCoords3D(_coords,ei,ej,ek,el_coords);

        /* get coefficients for the element */
        prop_eta = props[ek][ej][ei].eta;

        /* initialise element stiffness matrix */
        ierr = PetscMemzero(Ae,sizeof(PetscScalar)*NODES_PER_EL*U_DOFS*NODES_PER_EL*U_DOFS);CHKERRQ(ierr);
        ierr = PetscMemzero(Ge,sizeof(PetscScalar)*NODES_PER_EL*U_DOFS*NODES_PER_EL*P_DOFS);CHKERRQ(ierr);
        ierr = PetscMemzero(De,sizeof(PetscScalar)*NODES_PER_EL*P_DOFS*NODES_PER_EL*U_DOFS);CHKERRQ(ierr);
        ierr = PetscMemzero(Ce,sizeof(PetscScalar)*NODES_PER_EL*P_DOFS*NODES_PER_EL*P_DOFS);CHKERRQ(ierr);

        /* form element stiffness matrix */
        FormStressOperatorQ13D(Ae,el_coords,prop_eta);
        FormGradientOperatorQ13D(Ge,el_coords);
        /*FormDivergenceOperatorQ13D(De,el_coords);*/
        FormScaledMassMatrixOperatorQ13D(Ce,el_coords,prop_eta);

        /* insert element matrix into global matrix */
        ierr = DMDAGetElementEqnums3D_up(u_eqn,p_eqn,ei,ej,ek);CHKERRQ(ierr);

        for (n=0; n<NODES_PER_EL; n++) {
          if ( (u_eqn[3*n].i == 0) || (u_eqn[3*n].i == M-1) ) {
            _ZERO_ROWCOL_i(Ae,3*n);
            _ZERO_ROW_i   (Ge,3*n);
            _ZERO_COL_i   (De,3*n);
          }

          if ( (u_eqn[3*n+1].j == 0) || (u_eqn[3*n+1].j == N-1) ) {
            _ZERO_ROWCOL_i(Ae,3*n+1);
            _ZERO_ROW_i   (Ge,3*n+1);
            _ZERO_COL_i   (De,3*n+1);
          }

          if ( (u_eqn[3*n+2].k == 0) || (u_eqn[3*n+2].k == P-1) ) {
            _ZERO_ROWCOL_i(Ae,3*n+2);
            _ZERO_ROW_i   (Ge,3*n+2);
            _ZERO_COL_i   (De,3*n+2);
          }
        }
        ierr = MatSetValuesStencil(A,NODES_PER_EL*U_DOFS,u_eqn,NODES_PER_EL*U_DOFS,u_eqn,Ae,ADD_VALUES);CHKERRQ(ierr);
        ierr = MatSetValuesStencil(A,NODES_PER_EL*U_DOFS,u_eqn,NODES_PER_EL*P_DOFS,p_eqn,Ge,ADD_VALUES);CHKERRQ(ierr);
        /*ierr = MatSetValuesStencil(A,NODES_PER_EL*P_DOFS,p_eqn,NODES_PER_EL*U_DOFS,u_eqn,De,ADD_VALUES);CHKERRQ(ierr);*/
        ierr = MatSetValuesStencil(A,NODES_PER_EL*P_DOFS,p_eqn,NODES_PER_EL*P_DOFS,p_eqn,Ce,ADD_VALUES);CHKERRQ(ierr);
      }
    }
  }
  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  ierr = DMDAVecRestoreArray(cda,coords,&_coords);CHKERRQ(ierr);

  ierr = DMDAVecRestoreArray(properties_da,local_properties,&props);CHKERRQ(ierr);
  ierr = VecDestroy(&local_properties);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "AssembleF_Stokes"
static PetscErrorCode AssembleF_Stokes(Vec F,DM stokes_da,DM properties_da,Vec properties)
{
  DM                     cda;
  Vec                    coords;
  DMDACoor3d             ***_coords;
  MatStencil             u_eqn[NODES_PER_EL*U_DOFS];
  MatStencil             p_eqn[NODES_PER_EL*P_DOFS];
  PetscInt               sex,sey,sez,mx,my,mz;
  PetscInt               ei,ej,ek;
  PetscScalar            Fe[NODES_PER_EL*U_DOFS];
  PetscScalar            He[NODES_PER_EL*P_DOFS];
  PetscScalar            el_coords[NODES_PER_EL*NSD];
  Vec                    local_properties;
  GaussPointCoefficients ***props;
  PetscScalar            *prop_fx,*prop_fy,*prop_fz,*prop_hc;
  Vec                    local_F;
  StokesDOF              ***ff;
  PetscInt               n,M,N,P;
  PetscErrorCode         ierr;

  PetscFunctionBegin;

  ierr = DMDAGetInfo(stokes_da,0,&M,&N,&P,0,0,0, 0,0,0,0,0,0);CHKERRQ(ierr);
  /* setup for coords */
  ierr = DMDAGetCoordinateDA(stokes_da,&cda);CHKERRQ(ierr);
  ierr = DMDAGetGhostedCoordinates(stokes_da,&coords);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(cda,coords,&_coords);CHKERRQ(ierr);

  /* setup for coefficients */
  ierr = DMGetLocalVector(properties_da,&local_properties);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(properties_da,properties,INSERT_VALUES,local_properties);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(properties_da,properties,INSERT_VALUES,local_properties);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(properties_da,local_properties,&props);CHKERRQ(ierr);

  /* get acces to the vector */
  ierr = DMGetLocalVector(stokes_da,&local_F);CHKERRQ(ierr);
  ierr = VecZeroEntries(local_F);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(stokes_da,local_F,&ff);CHKERRQ(ierr);


  ierr = DMDAGetElementCorners(stokes_da,&sex,&sey,&sez,&mx,&my,&mz);CHKERRQ(ierr);
  for (ek = sez; ek < sez+mz; ek++) {
    for (ej = sey; ej < sey+my; ej++) {
      for (ei = sex; ei < sex+mx; ei++) {
        /* get coords for the element */
        GetElementCoords3D(_coords,ei,ej,ek,el_coords);

        /* get coefficients for the element */
        prop_fx = props[ek][ej][ei].fx;
        prop_fy = props[ek][ej][ei].fy;
        prop_fz = props[ek][ej][ei].fz;
        prop_hc = props[ek][ej][ei].hc;

        /* initialise element stiffness matrix */
        ierr = PetscMemzero(Fe,sizeof(PetscScalar)*NODES_PER_EL*U_DOFS);CHKERRQ(ierr);
        ierr = PetscMemzero(He,sizeof(PetscScalar)*NODES_PER_EL*P_DOFS);CHKERRQ(ierr);

        /* form element stiffness matrix */
        FormMomentumRhsQ13D(Fe,el_coords,prop_fx,prop_fy,prop_fz);
        FormContinuityRhsQ13D(He,el_coords,prop_hc);

        /* insert element matrix into global matrix */
        ierr = DMDAGetElementEqnums3D_up(u_eqn,p_eqn,ei,ej,ek);CHKERRQ(ierr);


        for (n=0; n<NODES_PER_EL; n++) {
          if ( (u_eqn[3*n  ].i == 0) || (u_eqn[3*n  ].i == M-1) ) {
            Fe[3*n  ] = 0.0;
          }

          if ( (u_eqn[3*n+1].j == 0) || (u_eqn[3*n+1].j == N-1) ) {
            Fe[3*n+1] = 0.0;
          }

          if ( (u_eqn[3*n+2].k == 0) || (u_eqn[3*n+2].k == P-1) ) {
            Fe[3*n+2] = 0.0;
          }
        }

        ierr = DMDASetValuesLocalStencil3D_ADD_VALUES(ff,u_eqn,p_eqn,Fe,He);CHKERRQ(ierr);
      }
    }
  }
  ierr = DMDAVecRestoreArray(stokes_da,local_F,&ff);CHKERRQ(ierr);
  ierr = DMLocalToGlobalBegin(stokes_da,local_F,ADD_VALUES,F);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(stokes_da,local_F,ADD_VALUES,F);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(stokes_da,&local_F);CHKERRQ(ierr);


  ierr = DMDAVecRestoreArray(cda,coords,&_coords);CHKERRQ(ierr);

  ierr = DMDAVecRestoreArray(properties_da,local_properties,&props);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(properties_da,&local_properties);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static void evaluate_MS_FrankKamentski_constants(PetscReal *theta,PetscReal *MX,PetscReal *MY,PetscReal *MZ)
{
  *theta = 0.0;
  *MX = 2.0 * M_PI;
  *MY = 2.0 * M_PI;
  *MZ = 2.0 * M_PI;
}
static void evaluate_MS_FrankKamentski(PetscReal pos[],PetscReal v[],PetscReal *p,PetscReal *eta,PetscReal Fm[],PetscReal *Fc)
{
  PetscReal x,y,z;
  PetscReal theta,MX,MY,MZ;

  evaluate_MS_FrankKamentski_constants(&theta,&MX,&MY,&MZ);
  x = pos[0];
  y = pos[1];
  z = pos[2];
  if (v) {
    /*
     v[0] = pow(z,3)*exp(y)*sin(M_PI*x);
     v[1] = z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y);
     v[2] = -(pow(x,3) + pow(y,3))*sin(2.0*M_PI*z);
     */
    /*
     v[0] = sin(M_PI*x);
     v[1] = sin(M_PI*y);
     v[2] = sin(M_PI*z);
     */
    v[0] = pow(z,3)*exp(y)*sin(M_PI*x);
    v[1] = z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y);
    v[2] = pow(z,2)*(cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y)/2 - M_PI*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y)/2) - M_PI*pow(z,4)*cos(M_PI*x)*exp(y)/4 ;
  }
  if (p) {
    *p = pow(x,2) + pow(y,2) + pow(z,2);
  }
  if (eta) {
    /**eta = exp(-theta*(1.0 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)));*/
    *eta = 1.0;
  }
  if (Fm) {
    /*
     Fm[0] = -2*x - 2.0*pow(M_PI,2)*pow(z,3)*exp(y)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(M_PI*x) - 0.2*MZ*theta*(-1.5*pow(x,2)*sin(2.0*M_PI*z) + 1.5*pow(z,2)*exp(y)*sin(M_PI*x))*cos(MX*x)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(MY*y)*sin(MZ*z) - 0.2*M_PI*MX*theta*pow(z,3)*cos(M_PI*x)*cos(MZ*z)*exp(y)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(MX*x)*sin(MY*y) + 2.0*(3.0*z*exp(y)*sin(M_PI*x) - 3.0*M_PI*pow(x,2)*cos(2.0*M_PI*z))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) + 2.0*(0.5*pow(z,3)*exp(y)*sin(M_PI*x) + M_PI*z*exp(-y)*sin(M_PI*y)*sin(2.0*M_PI*x) - 1.0*z*pow(M_PI,2)*cos(M_PI*y)*exp(-y)*sin(2.0*M_PI*x))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) + 2.0*theta*(1 + 0.1*MY*cos(MX*x)*cos(MY*y)*cos(MZ*z))*(0.5*pow(z,3)*exp(y)*sin(M_PI*x) - 1.0*M_PI*z*exp(-y)*sin(M_PI*y)*sin(2.0*M_PI*x))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) ;
     Fm[1] = -2*y - 0.2*MX*theta*(0.5*pow(z,3)*exp(y)*sin(M_PI*x) - 1.0*M_PI*z*exp(-y)*sin(M_PI*y)*sin(2.0*M_PI*x))*cos(MZ*z)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(MX*x)*sin(MY*y) - 0.2*MZ*theta*(-1.5*pow(y,2)*sin(2.0*M_PI*z) + 0.5*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y))*cos(MX*x)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(MY*y)*sin(MZ*z) + 2.0*(-2.0*z*pow(M_PI,2)*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + 0.5*M_PI*pow(z,3)*cos(M_PI*x)*exp(y))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) + 2.0*(z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) - z*pow(M_PI,2)*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) - 2*M_PI*z*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) + 2.0*theta*(1 + 0.1*MY*cos(MX*x)*cos(MY*y)*cos(MZ*z))*(-z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + M_PI*z*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) - 6.0*M_PI*pow(y,2)*cos(2.0*M_PI*z)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)));
     Fm[2] = -2*z + 8.0*pow(M_PI,2)*(pow(x,3) + pow(y,3))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(2.0*M_PI*z) - 0.2*MX*theta*(-1.5*pow(x,2)*sin(2.0*M_PI*z) + 1.5*pow(z,2)*exp(y)*sin(M_PI*x))*cos(MZ*z)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(MX*x)*sin(MY*y) + 0.4*M_PI*MZ*theta*(pow(x,3) + pow(y,3))*cos(MX*x)*cos(2.0*M_PI*z)*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))*sin(MY*y)*sin(MZ*z) + 2.0*(-3.0*x*sin(2.0*M_PI*z) + 1.5*M_PI*pow(z,2)*cos(M_PI*x)*exp(y))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) + 2.0*(-3.0*y*sin(2.0*M_PI*z) - 0.5*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + 0.5*M_PI*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y))) + 2.0*theta*(1 + 0.1*MY*cos(MX*x)*cos(MY*y)*cos(MZ*z))*(-1.5*pow(y,2)*sin(2.0*M_PI*z) + 0.5*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y))*exp(-theta*(1 - y - 0.1*cos(MX*x)*cos(MZ*z)*sin(MY*y)))  ;
     */
    /*
     Fm[0]=-2*x - 2.0*pow(M_PI,2)*sin(M_PI*x);
     Fm[1]=-2*y - 2.0*pow(M_PI,2)*sin(M_PI*y);
     Fm[2]=-2*z - 2.0*pow(M_PI,2)*sin(M_PI*z);
     */
    /*
     Fm[0] = -2*x + pow(z,3)*exp(y)*sin(M_PI*x) + 6.0*z*exp(y)*sin(M_PI*x) - 6.0*M_PI*pow(x,2)*cos(2.0*M_PI*z) - 2.0*pow(M_PI,2)*pow(z,3)*exp(y)*sin(M_PI*x) + 2.0*M_PI*z*exp(-y)*sin(M_PI*y)*sin(2.0*M_PI*x) - 2.0*z*pow(M_PI,2)*cos(M_PI*y)*exp(-y)*sin(2.0*M_PI*x) ;
     Fm[1] = -2*y - 6.0*M_PI*pow(y,2)*cos(2.0*M_PI*z) + 2.0*z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) - 6.0*z*pow(M_PI,2)*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + M_PI*pow(z,3)*cos(M_PI*x)*exp(y) - 4.0*M_PI*z*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y);
     Fm[2] = -2*z - 6.0*x*sin(2.0*M_PI*z) - 6.0*y*sin(2.0*M_PI*z) - cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + 8.0*pow(M_PI,2)*(pow(x,3) + pow(y,3))*sin(2.0*M_PI*z) + 3.0*M_PI*pow(z,2)*cos(M_PI*x)*exp(y) + M_PI*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y) ;
     */
    Fm[0] = -2*x + 2*z*(pow(M_PI,2)*cos(M_PI*y)*exp(-y)*sin(2.0*M_PI*x) - 1.0*M_PI*exp(-y)*sin(M_PI*y)*sin(2.0*M_PI*x)) + pow(z,3)*exp(y)*sin(M_PI*x) + 6.0*z*exp(y)*sin(M_PI*x) - 1.0*pow(M_PI,2)*pow(z,3)*exp(y)*sin(M_PI*x) + 2.0*M_PI*z*exp(-y)*sin(M_PI*y)*sin(2.0*M_PI*x) - 2.0*z*pow(M_PI,2)*cos(M_PI*y)*exp(-y)*sin(2.0*M_PI*x);
    Fm[1] = -2*y + 2*z*(-cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y)/2 + pow(M_PI,2)*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y)/2 + M_PI*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y)) + 2.0*z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) - 6.0*z*pow(M_PI,2)*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) - 4.0*M_PI*z*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y) ;
    Fm[2] = -2*z + pow(z,2)*(-2.0*pow(M_PI,2)*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + 2.0*pow(M_PI,3)*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y)) + pow(z,2)*(cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y)/2 - 3*pow(M_PI,2)*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y)/2 + pow(M_PI,3)*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y)/2 - 3*M_PI*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y)/2) + 1.0*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + 0.25*pow(M_PI,3)*pow(z,4)*cos(M_PI*x)*exp(y) - 0.25*M_PI*pow(z,4)*cos(M_PI*x)*exp(y) - 3.0*M_PI*pow(z,2)*cos(M_PI*x)*exp(y) - 1.0*M_PI*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y) ;
  }
  if (Fc) {
    /**Fc = -2.0*M_PI*(pow(x,3) + pow(y,3))*cos(2.0*M_PI*z) - z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + M_PI*pow(z,3)*cos(M_PI*x)*exp(y) + M_PI*z*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y) ;*/
    /**Fc = M_PI*cos(M_PI*x) + M_PI*cos(M_PI*y) + M_PI*cos(M_PI*z);*/
    /**Fc = -2.0*M_PI*(pow(x,3) + pow(y,3))*cos(2.0*M_PI*z) - z*cos(2.0*M_PI*x)*exp(-y)*sin(M_PI*y) + M_PI*pow(z,3)*cos(M_PI*x)*exp(y) + M_PI*z*cos(M_PI*y)*cos(2.0*M_PI*x)*exp(-y);*/
    *Fc = 0.0;
  }
}

#undef __FUNCT__
#define __FUNCT__ "DMDACreateManufacturedSolution"
static PetscErrorCode DMDACreateManufacturedSolution(PetscInt mx,PetscInt my,PetscInt mz,DM *_da,Vec *_X)
{
  DM             da,cda;
  Vec            X,local_X;
  StokesDOF      ***_stokes;
  Vec            coords;
  DMDACoor3d     ***_coords;
  PetscInt       si,sj,sk,ei,ej,ek,i,j,k;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMDACreate3d(PETSC_COMM_WORLD,DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,DMDA_STENCIL_BOX,
                      mx+1,my+1,mz+1,PETSC_DECIDE,PETSC_DECIDE,PETSC_DECIDE,4,1,PETSC_NULL,PETSC_NULL,PETSC_NULL,&da);CHKERRQ(ierr);
  ierr = DMDASetFieldName(da,0,"anlytic_Vx");CHKERRQ(ierr);
  ierr = DMDASetFieldName(da,1,"anlytic_Vy");CHKERRQ(ierr);
  ierr = DMDASetFieldName(da,2,"anlytic_Vz");CHKERRQ(ierr);
  ierr = DMDASetFieldName(da,3,"analytic_P");CHKERRQ(ierr);

  ierr = DMDASetUniformCoordinates(da,0.0,1.0,0.0,1.0,0.0,1.0);CHKERRQ(ierr);

  ierr = DMDAGetGhostedCoordinates(da,&coords);CHKERRQ(ierr);
  ierr = DMDAGetCoordinateDA(da,&cda);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(cda,coords,&_coords);CHKERRQ(ierr);

  ierr = DMCreateGlobalVector(da,&X);CHKERRQ(ierr);
  ierr = DMCreateLocalVector(da,&local_X);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(da,local_X,&_stokes);CHKERRQ(ierr);

  ierr = DMDAGetGhostCorners(da,&si,&sj,&sk,&ei,&ej,&ek);CHKERRQ(ierr);
  for (k = sk; k < sk+ek; k++) {
    for (j = sj; j < sj+ej; j++) {
      for (i = si; i < si+ei; i++) {
        PetscReal pos[NSD],pressure,vel[NSD];

        pos[0] = PetscRealPart(_coords[k][j][i].x);
        pos[1] = PetscRealPart(_coords[k][j][i].y);
        pos[2] = PetscRealPart(_coords[k][j][i].z);

        evaluate_MS_FrankKamentski(pos,vel,&pressure,PETSC_NULL,PETSC_NULL,PETSC_NULL);

        _stokes[k][j][i].u_dof = vel[0];
        _stokes[k][j][i].v_dof = vel[1];
        _stokes[k][j][i].w_dof = vel[2];
        _stokes[k][j][i].p_dof = pressure;
      }
    }
  }
  ierr = DMDAVecRestoreArray(da,local_X,&_stokes);CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(cda,coords,&_coords);CHKERRQ(ierr);

  ierr = DMLocalToGlobalBegin(da,local_X,INSERT_VALUES,X);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(da,local_X,INSERT_VALUES,X);CHKERRQ(ierr);

  ierr = VecDestroy(&local_X);CHKERRQ(ierr);

  *_da = da;
  *_X  = X;

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMDAIntegrateErrors3D"
static PetscErrorCode DMDAIntegrateErrors3D(DM stokes_da,Vec X,Vec X_analytic)
{
  DM          cda;
  Vec         coords,X_analytic_local,X_local;
  DMDACoor3d  ***_coords;
  PetscInt    sex,sey,sez,mx,my,mz;
  PetscInt    ei,ej,ek;
  PetscScalar el_coords[NODES_PER_EL*NSD];
  StokesDOF   ***stokes_analytic,***stokes;
  StokesDOF   stokes_analytic_e[NODES_PER_EL],stokes_e[NODES_PER_EL];

  PetscScalar    GNi_p[NSD][NODES_PER_EL],GNx_p[NSD][NODES_PER_EL];
  PetscScalar    Ni_p[NODES_PER_EL];
  PetscInt       ngp;
  PetscScalar    gp_xi[GAUSS_POINTS][NSD];
  PetscScalar    gp_weight[GAUSS_POINTS];
  PetscInt       p,i;
  PetscScalar    J_p,fac;
  PetscScalar    h,p_e_L2,u_e_L2,u_e_H1,p_L2,u_L2,u_H1,tp_L2,tu_L2,tu_H1;
  PetscScalar    tint_p_ms,tint_p,int_p_ms,int_p;
  PetscInt       M;
  PetscReal      xymin[NSD],xymax[NSD];
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* define quadrature rule */
  ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

  /* setup for coords */
  ierr = DMDAGetCoordinateDA(stokes_da,&cda);CHKERRQ(ierr);
  ierr = DMDAGetGhostedCoordinates(stokes_da,&coords);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(cda,coords,&_coords);CHKERRQ(ierr);

  /* setup for analytic */
  ierr = DMCreateLocalVector(stokes_da,&X_analytic_local);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(stokes_da,X_analytic,INSERT_VALUES,X_analytic_local);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(stokes_da,X_analytic,INSERT_VALUES,X_analytic_local);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(stokes_da,X_analytic_local,&stokes_analytic);CHKERRQ(ierr);

  /* setup for solution */
  ierr = DMCreateLocalVector(stokes_da,&X_local);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(stokes_da,X,INSERT_VALUES,X_local);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(stokes_da,X,INSERT_VALUES,X_local);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(stokes_da,X_local,&stokes);CHKERRQ(ierr);

  ierr = DMDAGetInfo(stokes_da,0,&M,0,0,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
  ierr = DMDAGetBoundingBox(stokes_da,xymin,xymax);CHKERRQ(ierr);

  h = (xymax[0]-xymin[0])/((PetscReal)(M-1));

  tp_L2 = tu_L2 = tu_H1 = 0.0;
  tint_p_ms = tint_p = 0.0;

  ierr = DMDAGetElementCorners(stokes_da,&sex,&sey,&sez,&mx,&my,&mz);CHKERRQ(ierr);

  for (ek = sez; ek < sez+mz; ek++) {
    for (ej = sey; ej < sey+my; ej++) {
      for (ei = sex; ei < sex+mx; ei++) {
        /* get coords for the element */
        ierr = GetElementCoords3D(_coords,ei,ej,ek,el_coords);CHKERRQ(ierr);
        ierr = StokesDAGetNodalFields3D(stokes,ei,ej,ek,stokes_e);CHKERRQ(ierr);
        ierr = StokesDAGetNodalFields3D(stokes_analytic,ei,ej,ek,stokes_analytic_e);CHKERRQ(ierr);

        /* evaluate integral */
        for (p = 0; p < ngp; p++) {
          ShapeFunctionQ13D_Evaluate(gp_xi[p],Ni_p);
          ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
          ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,el_coords,&J_p);
          fac = gp_weight[p]*J_p;

          for (i = 0; i < NODES_PER_EL; i++) {
            tint_p_ms = tint_p_ms+fac*Ni_p[i]*stokes_analytic_e[i].p_dof;
            tint_p    = tint_p   +fac*Ni_p[i]*stokes_e[i].p_dof;
          }
        }
      }
    }
  }
  ierr = MPI_Allreduce(&tint_p_ms,&int_p_ms,1,MPIU_SCALAR,MPIU_SUM,PETSC_COMM_WORLD);CHKERRQ(ierr);
  ierr = MPI_Allreduce(&tint_p,&int_p,1,MPIU_SCALAR,MPIU_SUM,PETSC_COMM_WORLD);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"\\int P dv %1.4e (h)  %1.4e (ms)\n",PetscRealPart(int_p),PetscRealPart(int_p_ms));CHKERRQ(ierr);

  /* remove mine and add manufacture one */
  ierr = DMDAVecRestoreArray(stokes_da,X_analytic_local,&stokes_analytic);CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(stokes_da,X_local,&stokes);CHKERRQ(ierr);

  {
    PetscInt k,L,dof;
    PetscScalar *fields;
    ierr = DMDAGetInfo(stokes_da,0, 0,0,0, 0,0,0, &dof,0,0,0,0,0);CHKERRQ(ierr);

    ierr = VecGetLocalSize(X_local,&L);CHKERRQ(ierr);
    ierr = VecGetArray(X_local,&fields);CHKERRQ(ierr);
    for (k=0; k<L/dof; k++) {
      fields[dof*k+3] = fields[dof*k+3] - int_p + int_p_ms;
    }
    ierr = VecRestoreArray(X_local,&fields);CHKERRQ(ierr);

    ierr = VecGetLocalSize(X,&L);CHKERRQ(ierr);
    ierr = VecGetArray(X,&fields);CHKERRQ(ierr);
    for (k=0; k<L/dof; k++) {
      fields[dof*k+3] = fields[dof*k+3] - int_p + int_p_ms;
    }
    ierr = VecRestoreArray(X,&fields);CHKERRQ(ierr);
  }


  ierr = DMDAVecGetArray(stokes_da,X_local,&stokes);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(stokes_da,X_analytic_local,&stokes_analytic);CHKERRQ(ierr);

  for (ek = sez; ek < sez+mz; ek++) {
    for (ej = sey; ej < sey+my; ej++) {
      for (ei = sex; ei < sex+mx; ei++) {
        /* get coords for the element */
        ierr = GetElementCoords3D(_coords,ei,ej,ek,el_coords);CHKERRQ(ierr);
        ierr = StokesDAGetNodalFields3D(stokes,ei,ej,ek,stokes_e);CHKERRQ(ierr);
        ierr = StokesDAGetNodalFields3D(stokes_analytic,ei,ej,ek,stokes_analytic_e);CHKERRQ(ierr);

        /* evaluate integral */
        p_e_L2 = 0.0;
        u_e_L2 = 0.0;
        u_e_H1 = 0.0;
        for (p = 0; p < ngp; p++) {
          ShapeFunctionQ13D_Evaluate(gp_xi[p],Ni_p);
          ShapeFunctionQ13D_Evaluate_dxi(gp_xi[p],GNi_p);
          ShapeFunctionQ13D_Evaluate_dx(GNi_p,GNx_p,el_coords,&J_p);
          fac = gp_weight[p]*J_p;

          for (i = 0; i < NODES_PER_EL; i++) {
            PetscScalar u_error,v_error,w_error;

            p_e_L2 = p_e_L2+fac*Ni_p[i]*(stokes_e[i].p_dof-stokes_analytic_e[i].p_dof)*(stokes_e[i].p_dof-stokes_analytic_e[i].p_dof);

            u_error = stokes_e[i].u_dof-stokes_analytic_e[i].u_dof;
            v_error = stokes_e[i].v_dof-stokes_analytic_e[i].v_dof;
            w_error = stokes_e[i].w_dof-stokes_analytic_e[i].w_dof;
            /*
             if (p==0) {
             printf("p=0: %d %d %d %1.4e,%1.4e,%1.4e \n", ei,ej,ek,u_error,v_error,w_error);
             }
             */
            u_e_L2  += fac*Ni_p[i]*(u_error*u_error+v_error*v_error+w_error*w_error);

            u_e_H1 = u_e_H1+fac*( GNx_p[0][i]*u_error*GNx_p[0][i]*u_error              /* du/dx */
                                  +GNx_p[1][i]*u_error*GNx_p[1][i]*u_error
                                  +GNx_p[2][i]*u_error*GNx_p[2][i]*u_error
                                  +GNx_p[0][i]*v_error*GNx_p[0][i]*v_error              /* dv/dx */
                                  +GNx_p[1][i]*v_error*GNx_p[1][i]*v_error
                                  +GNx_p[2][i]*v_error*GNx_p[2][i]*v_error
                                  +GNx_p[0][i]*w_error*GNx_p[0][i]*w_error              /* dw/dx */
                                  +GNx_p[1][i]*w_error*GNx_p[1][i]*w_error
                                  +GNx_p[2][i]*w_error*GNx_p[2][i]*w_error );
          }
        }

        tp_L2 += p_e_L2;
        tu_L2 += u_e_L2;
        tu_H1 += u_e_H1;
      }
    }
  }
  ierr = MPI_Allreduce(&tp_L2,&p_L2,1,MPIU_SCALAR,MPIU_SUM,PETSC_COMM_WORLD);CHKERRQ(ierr);
  ierr = MPI_Allreduce(&tu_L2,&u_L2,1,MPIU_SCALAR,MPIU_SUM,PETSC_COMM_WORLD);CHKERRQ(ierr);
  ierr = MPI_Allreduce(&tu_H1,&u_H1,1,MPIU_SCALAR,MPIU_SUM,PETSC_COMM_WORLD);CHKERRQ(ierr);
  p_L2 = PetscSqrtScalar(p_L2);
  u_L2 = PetscSqrtScalar(u_L2);
  u_H1 = PetscSqrtScalar(u_H1);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"%1.4e   %1.4e   %1.4e   %1.4e \n",PetscRealPart(h),PetscRealPart(p_L2),PetscRealPart(u_L2),PetscRealPart(u_H1));CHKERRQ(ierr);


  ierr = DMDAVecRestoreArray(cda,coords,&_coords);CHKERRQ(ierr);

  ierr = DMDAVecRestoreArray(stokes_da,X_analytic_local,&stokes_analytic);CHKERRQ(ierr);
  ierr = VecDestroy(&X_analytic_local);CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(stokes_da,X_local,&stokes);CHKERRQ(ierr);
  ierr = VecDestroy(&X_local);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DAView_3DVTK_StructuredGrid_appended"
PetscErrorCode DAView_3DVTK_StructuredGrid_appended( DM da, Vec FIELD, const char file_prefix[] )
{
  char vtk_filename[PETSC_MAX_PATH_LEN];
  PetscMPIInt rank;
  MPI_Comm comm;
  FILE *vtk_fp;
  PetscInt si,sj,sk, nx,ny,nz, i;
  PetscInt f, n_fields, N;
  DM cda;
  Vec coords;
  Vec l_FIELD;
  PetscScalar *_L_FIELD;
  PetscInt memory_offset;
  PetscScalar *buffer;
  PetscLogDouble t0,t1;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscGetTime(&t0);CHKERRQ(ierr);

  /* create file name */
  PetscObjectGetComm( (PetscObject)da, &comm );
  MPI_Comm_rank( comm, &rank );
  ierr = PetscSNPrintf(vtk_filename, sizeof vtk_filename, "subdomain-%s-p%1.4d.vts", file_prefix, rank );CHKERRQ(ierr);

  /* open file and write header */
  vtk_fp = fopen( vtk_filename, "w" );
  if( vtk_fp == NULL ) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_SYS,"ERROR(DAView_3DVTK_StructuredGrid_appended): Cannot open file = %s \n", vtk_filename );

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "<?xml version=\"1.0\"?>\n");

  /* coords */
  ierr = DMDAGetGhostCorners( da, &si,&sj,&sk, &nx,&ny,&nz );CHKERRQ(ierr);
  N = nx * ny * nz;

#ifdef PETSC_WORDS_BIGENDIAN
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "<VTKFile type=\"StructuredGrid\" version=\"0.1\" byte_order=\"BigEndian\">\n");
#else
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "<VTKFile type=\"StructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
#endif
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "  <StructuredGrid WholeExtent=\"%D %D %D %D %D %D\">\n", si,si+nx-1, sj,sj+ny-1, sk,sk+nz-1);
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    <Piece Extent=\"%D %D %D %D %D %D\">\n", si,si+nx-1, sj,sj+ny-1, sk,sk+nz-1);

  memory_offset = 0;

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "      <CellData></CellData>\n");

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "      <Points>\n");

  /* copy coordinates */
  ierr = DMDAGetCoordinateDA( da, &cda);CHKERRQ(ierr);
  ierr = DMDAGetGhostedCoordinates( da,&coords );CHKERRQ(ierr);
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"appended\" offset=\"%d\" />\n",memory_offset);
  memory_offset = memory_offset + sizeof(PetscInt) + sizeof(PetscScalar)*N*3;

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "      </Points>\n");


  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "      <PointData Scalars=\" ");
  ierr = DMDAGetInfo( da, 0, 0,0,0, 0,0,0, &n_fields, 0,0,0, 0, 0 );
  for( f=0; f<n_fields; f++ ) {
    const char *field_name;
    ierr = DMDAGetFieldName( da, f, &field_name );CHKERRQ(ierr);
    PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "%s ", field_name );
  }
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "\">\n");

  for( f=0; f<n_fields; f++ ) {
    const char *field_name;

    ierr = DMDAGetFieldName( da, f, &field_name );CHKERRQ(ierr);
    PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "        <DataArray type=\"Float64\" Name=\"%s\" format=\"appended\" offset=\"%d\"/>\n", field_name,memory_offset );
    memory_offset = memory_offset + sizeof(PetscInt) + sizeof(PetscScalar)*N;
  }

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "      </PointData>\n");
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    </Piece>\n");
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "  </StructuredGrid>\n");

  ierr = PetscMalloc( sizeof(PetscScalar)*N, &buffer );CHKERRQ(ierr);
  ierr = DMGetLocalVector( da, &l_FIELD );CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin( da, FIELD, INSERT_VALUES, l_FIELD );CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd( da, FIELD, INSERT_VALUES, l_FIELD );CHKERRQ(ierr);
  ierr = VecGetArray( l_FIELD, &_L_FIELD );CHKERRQ(ierr);

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp,"  <AppendedData encoding=\"raw\">\n");
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp,"_");

  /* write coordinates */
  {
    int length = sizeof(PetscScalar)*N*3;
    PetscScalar *allcoords;

    fwrite( &length,sizeof(int),1,vtk_fp);
    ierr = VecGetArray(coords,&allcoords);CHKERRQ(ierr);
    fwrite( allcoords, sizeof(PetscScalar),3*N, vtk_fp );
    ierr = VecRestoreArray(coords,&allcoords);CHKERRQ(ierr);
  }
  /* write fields */
  for( f=0; f<n_fields; f++ ) {
    int length = sizeof(PetscScalar)*N;
    fwrite( &length,sizeof(int),1,vtk_fp);
    /* load */
    for( i=0; i<N; i++ ) {
      buffer[i] = _L_FIELD[ n_fields*i + f ];
    }

    /* write */
    fwrite( buffer,sizeof(PetscScalar),N,vtk_fp);
  }
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp,"\n  </AppendedData>\n");

  ierr = PetscFree(buffer);CHKERRQ(ierr);
  ierr = VecRestoreArray( l_FIELD, &_L_FIELD );CHKERRQ(ierr);
  ierr = DMRestoreLocalVector( da, &l_FIELD );CHKERRQ(ierr);

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "</VTKFile>\n");


  if( vtk_fp!= NULL ) {
    fclose( vtk_fp );
    vtk_fp = NULL;
  }

  ierr = PetscGetTime(&t1);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DAViewVTK_write_PieceExtend"
PetscErrorCode DAViewVTK_write_PieceExtend( FILE *vtk_fp, PetscInt indent_level, DM da, const char local_file_prefix[] )
{
  PetscMPIInt nproc,rank;
  MPI_Comm comm;
  const PetscInt *lx,*ly,*lz;
  PetscInt M,N,P,pM,pN,pP,sum,*olx,*oly,*olz;
  PetscInt *osx,*osy,*osz,*oex,*oey,*oez;
  PetscInt i,j,k,II,stencil;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* create file name */
  PetscObjectGetComm( (PetscObject)da, &comm );
  MPI_Comm_size( comm, &nproc );
  MPI_Comm_rank( comm, &rank );

  ierr = DMDAGetInfo( da, 0, &M,&N,&P, &pM,&pN,&pP, 0, &stencil, 0,0,0, 0 );CHKERRQ(ierr);
  ierr = DMDAGetOwnershipRanges( da,&lx,&ly,&lz );CHKERRQ(ierr);

  /* generate start,end list */
  ierr = PetscMalloc( sizeof(PetscInt)*(pM+1), &olx );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pN+1), &oly );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pP+1), &olz );CHKERRQ(ierr);
  sum = 0;
  for( i=0; i<pM; i++ ) {
    olx[i] = sum;
    sum = sum + lx[i];
  } olx[pM] = sum;
  sum = 0;
  for( i=0; i<pN; i++ ) {
    oly[i] = sum;
    sum = sum + ly[i];
  } oly[pN] = sum;
  sum = 0;
  for( i=0; i<pP; i++ ) {
    olz[i] = sum;
    sum = sum + lz[i];
  } olz[pP] = sum;
  ierr = PetscMalloc( sizeof(PetscInt)*(pM), &osx );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pN), &osy );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pP), &osz );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pM), &oex );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pN), &oey );CHKERRQ(ierr);
  ierr = PetscMalloc( sizeof(PetscInt)*(pP), &oez );CHKERRQ(ierr);
  for( i=0; i<pM; i++ ) {
    osx[i] = olx[i]   - stencil;
    oex[i] = olx[i] + lx[i] + stencil;
    if(osx[i]<0)osx[i]=0;
    if(oex[i]>M)oex[i]=M;
  }

  for( i=0; i<pN; i++ ) {
    osy[i] = oly[i]   - stencil;
    oey[i] = oly[i] + ly[i] + stencil;
    if(osy[i]<0)osy[i]=0;
    if(oey[i]>M)oey[i]=N;
  }
  for( i=0; i<pP; i++ ) {
    osz[i] = olz[i]   - stencil;
    oez[i] = olz[i] + lz[i] + stencil;
    if(osz[i]<0)osz[i]=0;
    if(oez[i]>P)oez[i]=P;
  }

  for( k=0;k<pP;k++ ) {
    for( j=0;j<pN;j++ ) {
      for( i=0;i<pM;i++ ) {
        char name[PETSC_MAX_PATH_LEN];
        PetscInt procid = i + j*pM + k*pM*pN; /* convert proc(i,j,k) to pid */
        ierr = PetscSNPrintf(name, sizeof name, "subdomain-%s-p%1.4d.vts", local_file_prefix, procid );CHKERRQ(ierr);
        for( II=0; II<indent_level; II++ ) {
          PetscFPrintf(PETSC_COMM_SELF,vtk_fp,"  ");
        }
        PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "<Piece Extent=\"%d %d %d %d %d %d\"      Source=\"%s\"/>\n",
                     osx[i],oex[i]-1,
                     osy[j],oey[j]-1,
                     osz[k],oez[k]-1, name);
      }
    }
  }
  ierr = PetscFree( olx );CHKERRQ(ierr);
  ierr = PetscFree( oly );CHKERRQ(ierr);
  ierr = PetscFree( olz );CHKERRQ(ierr);
  ierr = PetscFree( osx );CHKERRQ(ierr);
  ierr = PetscFree( osy );CHKERRQ(ierr);
  ierr = PetscFree( osz );CHKERRQ(ierr);
  ierr = PetscFree( oex );CHKERRQ(ierr);
  ierr = PetscFree( oey );CHKERRQ(ierr);
  ierr = PetscFree( oez );CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DAView_3DVTK_PStructuredGrid"
PetscErrorCode DAView_3DVTK_PStructuredGrid( DM da, const char file_prefix[], const char local_file_prefix[] )
{
  MPI_Comm comm;
  PetscMPIInt nproc,rank;
  char vtk_filename[PETSC_MAX_PATH_LEN];
  FILE *vtk_fp;
  PetscInt M,N,P, si,sj,sk, nx,ny,nz;
  PetscInt i,dofs;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  /* only master generates this file */
  PetscObjectGetComm( (PetscObject)da, &comm );
  MPI_Comm_size( comm, &nproc );
  MPI_Comm_rank( comm, &rank );

  if( rank != 0 ) { PetscFunctionReturn(0); }

  /* create file name */
  ierr = PetscSNPrintf(vtk_filename,sizeof vtk_filename, "%s.pvts", file_prefix );CHKERRQ(ierr);
  vtk_fp = fopen( vtk_filename, "w" );
  if( vtk_fp == NULL ) {
    printf("ERROR(DAView_3DVTK_PStructuredGrid): Cannot open file = %s \n", vtk_filename );
    exit(0);
  }

  /* (VTK) generate pvts header */
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "<?xml version=\"1.0\"?>\n");

#ifdef PETSC_WORDS_BIGENDIAN
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "<VTKFile type=\"PStructuredGrid\" version=\"0.1\" byte_order=\"BigEndian\">\n");
#else
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "<VTKFile type=\"PStructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
#endif

  /* define size of the nodal mesh based on the cell DM */
  DMDAGetInfo( da, 0, &M,&N,&P, 0,0,0, &dofs,0,0,0,0,0 );
  DMDAGetGhostCorners( da, &si,&sj,&sk, &nx,&ny,&nz );
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "  <PStructuredGrid GhostLevel=\"1\" WholeExtent=\"%d %d %d %d %d %d\">\n", 0,M-1, 0,N-1, 0,P-1 ); /* note overlap = 1 for Q1 */


  /* DUMP THE CELL REFERENCES */
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    <PCellData>\n");
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    </PCellData>\n");

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    <PPoints>\n");
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "      <PDataArray type=\"Float64\" Name=\"Points\" NumberOfComponents=\"%d\"/>\n",NSD);
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    </PPoints>\n");

  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    <PPointData>\n");
  for(i=0;i<dofs;i++){
    const char *fieldname;
    ierr = DMDAGetFieldName(da,i,&fieldname);CHKERRQ(ierr);
    PetscFPrintf(PETSC_COMM_SELF,vtk_fp,"      <PDataArray type=\"Float64\" Name=\"%s\" NumberOfComponents=\"1\"/>\n",fieldname);
  }
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "    </PPointData>\n");

  /* write out the parallel information */
  ierr = DAViewVTK_write_PieceExtend(vtk_fp,2,da,local_file_prefix);CHKERRQ(ierr);


  /* close the file */
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "  </PStructuredGrid>\n");
  PetscFPrintf(PETSC_COMM_SELF, vtk_fp, "</VTKFile>\n");

  if(vtk_fp!=NULL){
    fclose( vtk_fp );
    vtk_fp = NULL;
  }

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DAView3DPVTS"
PetscErrorCode DAView3DPVTS(DM da, Vec x,const char NAME[])
{
  char vts_filename[PETSC_MAX_PATH_LEN];
  char pvts_filename[PETSC_MAX_PATH_LEN];
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscSNPrintf(vts_filename,sizeof vts_filename, "%s-mesh", NAME );CHKERRQ(ierr);
  ierr = DAView_3DVTK_StructuredGrid_appended(da,x,vts_filename);CHKERRQ(ierr);

  ierr = PetscSNPrintf(pvts_filename,sizeof pvts_filename, "%s-mesh", NAME );CHKERRQ(ierr);
  ierr = DAView_3DVTK_PStructuredGrid( da, pvts_filename, vts_filename );CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "KSPMonitorStokesBlocks"
PetscErrorCode KSPMonitorStokesBlocks(KSP ksp,PetscInt n,PetscReal rnorm,void *dummy)
{
  PetscErrorCode ierr;
  PetscReal norms[4];
  Vec Br, v,w;
  Mat A;

  PetscFunctionBegin;
  ierr = KSPGetOperators( ksp, &A, 0, 0 ); CHKERRQ(ierr);
  ierr = MatGetVecs( A, &w,&v ); CHKERRQ(ierr);
  ierr = VecSetBlockSize(v,4);CHKERRQ(ierr);

  ierr = KSPBuildResidual( ksp, v, w, &Br ); CHKERRQ(ierr);

  ierr = VecStrideNorm(Br,0,NORM_2,&norms[0]);CHKERRQ(ierr);
  ierr = VecStrideNorm(Br,1,NORM_2,&norms[1]);CHKERRQ(ierr);
  ierr = VecStrideNorm(Br,2,NORM_2,&norms[2]);CHKERRQ(ierr);
  ierr = VecStrideNorm(Br,3,NORM_2,&norms[3]);CHKERRQ(ierr);

  ierr = VecDestroy(&v); CHKERRQ(ierr);
  ierr = VecDestroy(&w); CHKERRQ(ierr);

  PetscPrintf(PETSC_COMM_WORLD,"  %d KSP Component U,V,W,P residual norm [ %1.12e, %1.12e, %1.12e, %1.12e ]\n",n,norms[0],norms[1],norms[2],norms[3]);

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCMGSetupViaCoarsen"
static PetscErrorCode PCMGSetupViaCoarsen(PC pc,DM da_fine)
{
  PetscInt       nlevels,k,finest;
  DM             *da_list,*daclist;
  Mat            R;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  nlevels = 1;
  PetscOptionsGetInt( PETSC_NULL, "-levels", &nlevels, 0 );

  ierr = PetscMalloc( sizeof(DM)*nlevels, &da_list );CHKERRQ(ierr);
  for( k=0; k<nlevels; k++ ) {  da_list[k] = PETSC_NULL;  }
  ierr = PetscMalloc( sizeof(DM)*nlevels, &daclist );CHKERRQ(ierr);
  for( k=0; k<nlevels; k++ ) {  daclist[k] = PETSC_NULL;  }

  /* finest grid is nlevels - 1 */
  finest = nlevels - 1;
  daclist[0] = da_fine;
  PetscObjectReference( (PetscObject)da_fine );
  ierr = DMCoarsenHierarchy(da_fine,nlevels-1,&daclist[1]);CHKERRQ(ierr);
  for( k=0; k<nlevels; k++ ) {
    da_list[k] = daclist[nlevels-1-k];
    ierr = DMDASetUniformCoordinates(da_list[k], 0.0,1.0, 0.0,1.0, 0.0,1.0);CHKERRQ(ierr);
  }

  ierr = PCMGSetLevels(pc,nlevels,PETSC_NULL);CHKERRQ(ierr);
  ierr = PCMGSetType(pc,PC_MG_MULTIPLICATIVE);CHKERRQ(ierr);
  ierr = PCMGSetGalerkin(pc);CHKERRQ(ierr);

  for( k=1; k<nlevels; k++ ){
    ierr = DMGetInterpolation(da_list[k-1],da_list[k],&R,PETSC_NULL);CHKERRQ(ierr);
    ierr = PCMGSetInterpolation(pc,k,R);CHKERRQ(ierr);
    ierr = MatDestroy(&R);CHKERRQ(ierr);
  }

  /* tidy up */
  for( k=0; k<nlevels; k++ ) {
    ierr = DMDestroy(&da_list[k]);CHKERRQ(ierr);
  }
  ierr = PetscFree(da_list);CHKERRQ(ierr);
  ierr = PetscFree(daclist);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "solve_stokes_3d_coupled"
static PetscErrorCode solve_stokes_3d_coupled(PetscInt mx,PetscInt my,PetscInt mz)
{
  DM                     da_Stokes,da_prop;
  PetscInt               u_dof,p_dof,dof,stencil_width;
  Mat                    A,B;
  PetscInt               mxl,myl,mzl;
  DM                     prop_cda,vel_cda;
  Vec                    prop_coords,vel_coords;
  PetscInt               si,sj,sk,nx,ny,nz,p;
  Vec                    f,X;
  PetscInt               prop_dof,prop_stencil_width;
  Vec                    properties,l_properties;
  PetscReal              dx,dy,dz;
  PetscInt               M,N,P;
  DMDACoor3d             ***_prop_coords,***_vel_coords;
  GaussPointCoefficients ***element_props;
  PetscInt               its;
  KSP                    ksp_S;
  PetscInt               model_definition = 0;
  PetscInt               cpu_x,cpu_y,cpu_z,*lx = PETSC_NULL,*ly = PETSC_NULL,*lz = PETSC_NULL;
  PetscInt               ei,ej,ek,sex,sey,sez,Imx,Jmy,Kmz;
  PetscErrorCode         ierr;

  PetscFunctionBegin;
  /* Generate the da for velocity and pressure */
  /* Num nodes in each direction is mx+1, my+1, mz+1 */
  u_dof         = U_DOFS; /* Vx, Vy - velocities */
  p_dof         = P_DOFS; /* p - pressure */
  dof           = u_dof+p_dof;
  stencil_width = 1;
  ierr          = DMDACreate3d(PETSC_COMM_WORLD,DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,DMDA_STENCIL_BOX,
                               mx+1,my+1,mz+1,PETSC_DECIDE,PETSC_DECIDE,PETSC_DECIDE,dof,stencil_width,PETSC_NULL,PETSC_NULL,PETSC_NULL,&da_Stokes);CHKERRQ(ierr);
  ierr = DMDASetFieldName(da_Stokes,0,"Vx");CHKERRQ(ierr);
  ierr = DMDASetFieldName(da_Stokes,1,"Vy");CHKERRQ(ierr);
  ierr = DMDASetFieldName(da_Stokes,2,"Vz");CHKERRQ(ierr);
  ierr = DMDASetFieldName(da_Stokes,3,"P");CHKERRQ(ierr);

  /* unit box [0,1] x [0,1] x [0,1] */
  ierr = DMDASetUniformCoordinates(da_Stokes,0.0,1.0,0.0,1.0,0.0,1.0);CHKERRQ(ierr);

  /* local number of elements */
  ierr = DMDAGetLocalElementSize(da_Stokes,&mxl,&myl,&mzl);CHKERRQ(ierr);

  /* !!! IN PARALLEL WE MUST MAKE SURE THE TWO DMDA's ALIGN !!! */
  ierr = DMDAGetInfo(da_Stokes,0,0,0,0,&cpu_x,&cpu_y,&cpu_z,0,0,0,0,0,0);CHKERRQ(ierr);
  ierr = DMDAGetElementOwnershipRanges3d(da_Stokes,&lx,&ly,&lz);CHKERRQ(ierr);

  prop_dof           = (PetscInt)(sizeof(GaussPointCoefficients)/sizeof(PetscScalar)); /* gauss point setup */
  prop_stencil_width = 0;
  ierr               = DMDACreate3d(PETSC_COMM_WORLD,DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,DMDA_STENCIL_BOX,
                                    mx,my,mz,cpu_x,cpu_y,cpu_z,prop_dof,prop_stencil_width,lx,ly,lz,&da_prop);CHKERRQ(ierr);
  ierr = PetscFree(lx);CHKERRQ(ierr);
  ierr = PetscFree(ly);CHKERRQ(ierr);
  ierr = PetscFree(lz);CHKERRQ(ierr);

  /* define centroid positions */
  ierr = DMDAGetInfo(da_prop,0,&M,&N,&P,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
  dx   = 1.0/((PetscReal)(M));
  dy   = 1.0/((PetscReal)(N));
  dz   = 1.0/((PetscReal)(P));

  ierr = DMDASetUniformCoordinates(da_prop,0.0+0.5*dx,1.0-0.5*dx,0.0+0.5*dy,1.0-0.5*dy,0.0+0.5*dz,1.0-0.5*dz);CHKERRQ(ierr);

  ierr = DMCreateGlobalVector(da_prop,&properties);CHKERRQ(ierr);
  ierr = DMCreateLocalVector(da_prop,&l_properties);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(da_prop,l_properties,&element_props);CHKERRQ(ierr);

  ierr = DMDAGetCoordinateDA(da_prop,&prop_cda);CHKERRQ(ierr);
  ierr = DMDAGetGhostedCoordinates(da_prop,&prop_coords);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(prop_cda,prop_coords,&_prop_coords);CHKERRQ(ierr);

  ierr = DMDAGetGhostCorners(prop_cda,&si,&sj,&sk,&nx,&ny,&nz);CHKERRQ(ierr);

  ierr = DMDAGetCoordinateDA(da_Stokes,&vel_cda);CHKERRQ(ierr);
  ierr = DMDAGetGhostedCoordinates(da_Stokes,&vel_coords);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(vel_cda,vel_coords,&_vel_coords);CHKERRQ(ierr);


  /* interpolate the coordinates */
  ierr = DMDAGetElementCorners(da_Stokes,&sex,&sey,&sez,&Imx,&Jmy,&Kmz);CHKERRQ(ierr);
  for (ek = sez; ek < sez+Kmz; ek++) {
    for (ej = sey; ej < sey+Jmy; ej++) {
      for (ei = sex; ei < sex+Imx; ei++) {
        /* get coords for the element */
        PetscInt    ngp;
        PetscScalar gp_xi[GAUSS_POINTS][NSD],gp_weight[GAUSS_POINTS];
        PetscScalar el_coords[NSD*NODES_PER_EL];

        ierr = GetElementCoords3D(_vel_coords,ei,ej,ek,el_coords);CHKERRQ(ierr);
        ConstructGaussQuadrature3D(&ngp,gp_xi,gp_weight);

        for (p = 0; p < GAUSS_POINTS; p++) {
          PetscScalar xi_p[NSD],Ni_p[NODES_PER_EL];
          PetscScalar gp_x,gp_y,gp_z;
          PetscInt    n;

          xi_p[0] = gp_xi[p][0];
          xi_p[1] = gp_xi[p][1];
          xi_p[2] = gp_xi[p][2];
          ShapeFunctionQ13D_Evaluate(xi_p,Ni_p);

          gp_x = gp_y = gp_z = 0.0;
          for (n = 0; n < NODES_PER_EL; n++) {
            gp_x = gp_x+Ni_p[n]*el_coords[NSD*n  ];
            gp_y = gp_y+Ni_p[n]*el_coords[NSD*n+1];
            gp_z = gp_z+Ni_p[n]*el_coords[NSD*n+2];
          }
          element_props[ek][ej][ei].gp_coords[NSD*p  ] = gp_x;
          element_props[ek][ej][ei].gp_coords[NSD*p+1] = gp_y;
          element_props[ek][ej][ei].gp_coords[NSD*p+2] = gp_z;
        }
      }
    }
  }

  /* define coefficients */
  ierr = PetscOptionsGetInt(PETSC_NULL,"-model",&model_definition,PETSC_NULL);CHKERRQ(ierr);

  switch (model_definition) {
  case 0: /* isoviscous */
    for (ek = sez; ek < sez+Kmz; ek++) {
      for (ej = sey; ej < sey+Jmy; ej++) {
        for (ei = sex; ei < sex+Imx; ei++) {

          for (p = 0; p < GAUSS_POINTS; p++) {
            PetscReal coord_x = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p  ]);
            PetscReal coord_y = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+1]);
            PetscReal coord_z = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+2]);

            element_props[ek][ej][ei].eta[p] = 1.0;

            element_props[ek][ej][ei].fx[p] = 0.0*coord_x;
            element_props[ek][ej][ei].fy[p] = -sin((double)2.2*M_PI*coord_y)*cos(1.0*M_PI*coord_x);
            element_props[ek][ej][ei].fz[p] = 0.0*coord_z;
            element_props[ek][ej][ei].hc[p] = 0.0;
          }
        }
      }
    }
    break;

  case 1: /* manufactured */
    for (ek = sez; ek < sez+Kmz; ek++) {
      for (ej = sey; ej < sey+Jmy; ej++) {
        for (ei = sex; ei < sex+Imx; ei++) {
          PetscReal eta,Fm[NSD],Fc,pos[NSD];

          for (p = 0; p < GAUSS_POINTS; p++) {
            PetscReal coord_x = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p  ]);
            PetscReal coord_y = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+1]);
            PetscReal coord_z = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+2]);

            pos[0] = coord_x;
            pos[1] = coord_y;
            pos[2] = coord_z;

            evaluate_MS_FrankKamentski(pos,PETSC_NULL,PETSC_NULL,&eta,Fm,&Fc);
            element_props[ek][ej][ei].eta[p] = eta;

            element_props[ek][ej][ei].fx[p] = Fm[0];
            element_props[ek][ej][ei].fy[p] = Fm[1];
            element_props[ek][ej][ei].fz[p] = Fm[2];
            element_props[ek][ej][ei].hc[p] = Fc;
          }
        }
      }
    }
    break;

  case 2: /* solcx */
    for (ek = sez; ek < sez+Kmz; ek++) {
      for (ej = sey; ej < sey+Jmy; ej++) {
        for (ei = sex; ei < sex+Imx; ei++) {
          for (p = 0; p < GAUSS_POINTS; p++) {
            PetscReal coord_x = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p  ]);
            PetscReal coord_y = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+1]);
            PetscReal coord_z = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+2]);

            element_props[ek][ej][ei].eta[p] = 1.0;

            element_props[ek][ej][ei].fx[p] = 0.0;
            element_props[ek][ej][ei].fy[p] = -sin((double)3*M_PI*coord_y)*cos(1.0*M_PI*coord_x);
            element_props[ek][ej][ei].fz[p] = 0.0*coord_z;
            element_props[ek][ej][ei].hc[p] = 0.0;
          }
        }
      }
    }
    break;

  case 3: /* sinker */
    for (ek = sez; ek < sez+Kmz; ek++) {
      for (ej = sey; ej < sey+Jmy; ej++) {
        for (ei = sex; ei < sex+Imx; ei++) {
          for (p = 0; p < GAUSS_POINTS; p++) {
            PetscReal xp = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p  ]);
            PetscReal yp = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+1]);
            PetscReal zp = PetscRealPart(element_props[ek][ej][ei].gp_coords[NSD*p+2]);

            element_props[ek][ej][ei].eta[p] = 1.0e-2;
            element_props[ek][ej][ei].fx[p] = 0.0;
            element_props[ek][ej][ei].fy[p] = 0.0;
            element_props[ek][ej][ei].fz[p] = 0.0;
            element_props[ek][ej][ei].hc[p] = 0.0;

            if ( (fabs(xp-0.5) < 0.2) &&
                 (fabs(yp-0.5) < 0.2) &&
                 (fabs(zp-0.5) < 0.2) ) {
              element_props[ek][ej][ei].eta[p] = 1.0;
              element_props[ek][ej][ei].fz[p]  = 1.0;
            }

          }
        }
      }
    }
    break;

  default:
    SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_SUP,"No default model is supported. Choose either -model {0,1,2,3}");
    break;
  }

  ierr = DMDAVecRestoreArray(prop_cda,prop_coords,&_prop_coords);CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(vel_cda,vel_coords,&_vel_coords);CHKERRQ(ierr);

  ierr = DMDAVecRestoreArray(da_prop,l_properties,&element_props);CHKERRQ(ierr);
  ierr = DMLocalToGlobalBegin(da_prop,l_properties,ADD_VALUES,properties);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(da_prop,l_properties,ADD_VALUES,properties);CHKERRQ(ierr);

  /* Generate a matrix with the correct non-zero pattern of type AIJ. This will work in parallel and serial */
  ierr = DMGetMatrix(da_Stokes,MATAIJ,&A);CHKERRQ(ierr);
  ierr = DMGetMatrix(da_Stokes,MATAIJ,&B);CHKERRQ(ierr);
  ierr = MatGetVecs(A,&f,&X);CHKERRQ(ierr);

  /* assemble A11 */
  ierr = MatZeroEntries(A);CHKERRQ(ierr);
  ierr = MatZeroEntries(B);CHKERRQ(ierr);
  ierr = VecZeroEntries(f);CHKERRQ(ierr);

  ierr = AssembleA_Stokes(A,da_Stokes,da_prop,properties);CHKERRQ(ierr);
  ierr = AssembleA_PCStokes(B,da_Stokes,da_prop,properties);CHKERRQ(ierr);
  /* build force vector */
  ierr = AssembleF_Stokes(f,da_Stokes,da_prop,properties);CHKERRQ(ierr);

  /* SOLVE */
  ierr = KSPCreate(PETSC_COMM_WORLD,&ksp_S);CHKERRQ(ierr);
  ierr = KSPSetOptionsPrefix(ksp_S,"stokes_"); /* stokes */ CHKERRQ(ierr);
  ierr = KSPSetOperators(ksp_S,A,B,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
  ierr = KSPSetFromOptions(ksp_S);CHKERRQ(ierr);

  {
    PC pc;
    const PetscInt ufields[] = {0,1,2},pfields[] = {3};
    ierr = KSPGetPC(ksp_S,&pc);CHKERRQ(ierr);
    ierr = PCFieldSplitSetBlockSize(pc,4);CHKERRQ(ierr);
    ierr = PCFieldSplitSetFields(pc,"u",3,ufields);CHKERRQ(ierr);
    ierr = PCFieldSplitSetFields(pc,"p",1,pfields);CHKERRQ(ierr);
  }

  {
    PC pc;
    PetscBool same;
    ierr = KSPGetPC(ksp_S,&pc);CHKERRQ(ierr);
    same = PETSC_FALSE;
    ierr = PetscTypeCompare((PetscObject)pc,PCMG,&same);CHKERRQ(ierr);
    if (same) {
      ierr = PCMGSetupViaCoarsen(pc,da_Stokes);CHKERRQ(ierr);
    }
  }

  {
    PetscBool stokes_monitor;
    ierr = PetscOptionsGetBool(PETSC_NULL,"-stokes_ksp_monitor_blocks",&stokes_monitor,0);CHKERRQ(ierr);
    if (stokes_monitor) {
      ierr = KSPMonitorSet(ksp_S,KSPMonitorStokesBlocks,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }
  }
  ierr = KSPSolve(ksp_S,f,X);CHKERRQ(ierr);
  {
    PetscBool flg = PETSC_TRUE;
    ierr = PetscOptionsGetBool(PETSC_NULL,"-write_pvts",&flg,PETSC_NULL);CHKERRQ(ierr);
    if (flg) {ierr = DAView3DPVTS(da_Stokes,X,"up");CHKERRQ(ierr);}
  }
  {
    PetscBool flg = PETSC_FALSE;
    char filename[PETSC_MAX_PATH_LEN];
    ierr = PetscOptionsGetString(PETSC_NULL,"-write_binary",filename,sizeof filename,&flg);CHKERRQ(ierr);
    if (flg) {
      PetscViewer viewer;
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,filename[0]?filename:"ex42-binaryoutput",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
      ierr = VecView(X,viewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
    }
  }
  ierr = KSPGetIterationNumber(ksp_S,&its);CHKERRQ(ierr);

  /* verify */
  if (model_definition == 1) {
    DM  da_Stokes_analytic;
    Vec X_analytic;

    ierr = DMDACreateManufacturedSolution(mx,my,mz,&da_Stokes_analytic,&X_analytic);CHKERRQ(ierr);
    ierr = DAView3DPVTS(da_Stokes_analytic,X_analytic,"ms");CHKERRQ(ierr);
    ierr = DMDAIntegrateErrors3D(da_Stokes_analytic,X,X_analytic);CHKERRQ(ierr);
    ierr = DAView3DPVTS(da_Stokes,X,"up2");CHKERRQ(ierr);

    ierr = DMDestroy(&da_Stokes_analytic);CHKERRQ(ierr);
    ierr = VecDestroy(&X_analytic);CHKERRQ(ierr);
  }


  ierr = KSPDestroy(&ksp_S);CHKERRQ(ierr);
  ierr = VecDestroy(&X);CHKERRQ(ierr);
  ierr = VecDestroy(&f);CHKERRQ(ierr);
  ierr = MatDestroy(&A);CHKERRQ(ierr);
  ierr = MatDestroy(&B);CHKERRQ(ierr);

  ierr = DMDestroy(&da_Stokes);CHKERRQ(ierr);
  ierr = DMDestroy(&da_prop);CHKERRQ(ierr);

  ierr = VecDestroy(&properties);CHKERRQ(ierr);
  ierr = VecDestroy(&l_properties);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **args)
{
  PetscErrorCode ierr;
  PetscInt       mx,my,mz;

  ierr = PetscInitialize(&argc,&args,(char *)0,help);CHKERRQ(ierr);

  mx = my = mz = 10;
  ierr = PetscOptionsGetInt(PETSC_NULL,"-mx",&mx,PETSC_NULL);CHKERRQ(ierr);
  my = mx; mz = mx;
  ierr = PetscOptionsGetInt(PETSC_NULL,"-my",&my,PETSC_NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(PETSC_NULL,"-mz",&mz,PETSC_NULL);CHKERRQ(ierr);

  ierr = solve_stokes_3d_coupled(mx,my,mz);CHKERRQ(ierr);

  ierr = PetscFinalize();
  return 0;
}