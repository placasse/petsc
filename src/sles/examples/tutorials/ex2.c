#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: ex2.c,v 1.77 1999/01/12 23:16:17 bsmith Exp bsmith $";
#endif

/* Program usage:  mpirun -np <procs> ex2 [-help] [all PETSc options] */ 

static char help[] = "Solves a linear system in parallel with SLES.\n\
Input parameters include:\n\
  -random_exact_sol : use a random exact solution vector\n\
  -view_exact_sol   : write exact solution vector to stdout\n\
  -m <mesh_x>       : number of mesh points in x-direction\n\
  -n <mesh_n>       : number of mesh points in y-direction\n\n";

/*T
   Concepts: SLES^Solving a system of linear equations (basic parallel example);
   Concepts: SLES^Laplacian, 2d
   Concepts: Laplacian, 2d
   Routines: SLESCreate(); SLESSetOperators(); SLESSetFromOptions();
   Routines: SLESSolve(); SLESGetKSP(); SLESGetPC();
   Routines: KSPSetTolerances(); PCSetType();
   Routines: PetscRandomCreate(); PetscRandomDestroy(); VecSetRandom();
   Processors: n
T*/

/* 
  Include "sles.h" so that we can use SLES solvers.  Note that this file
  automatically includes:
     petsc.h  - base PETSc routines   vec.h - vectors
     sys.h    - system routines       mat.h - matrices
     is.h     - index sets            ksp.h - Krylov subspace methods
     viewer.h - viewers               pc.h  - preconditioners
*/
#include "sles.h"

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **args)
{
  Vec         x, b, u;  /* approx solution, RHS, exact solution */
  Mat         A;        /* linear system matrix */
  SLES        sles;     /* linear solver context */
  PetscRandom rctx;     /* random number generator context */
  double      norm;     /* norm of solution error */
  int         i, j, I, J, Istart, Iend, ierr, m = 8, n = 7, its, flg;
  Scalar      v, one = 1.0, neg_one = -1.0;
  KSP         ksp;

  PetscInitialize(&argc,&args,(char *)0,help);
  ierr = OptionsGetInt(PETSC_NULL,"-m",&m,&flg); CHKERRA(ierr);
  ierr = OptionsGetInt(PETSC_NULL,"-n",&n,&flg); CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
         Compute the matrix and right-hand-side vector that define
         the linear system, Ax = b.
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* 
     Create parallel matrix, specifying only its global dimensions.
     When using MatCreate(), the matrix format can be specified at
     runtime. Also, the parallel partitioning of the matrix is
     determined by PETSc at runtime.

     Performance tuning note:  For problems of substantial size,
     preallocation of matrix memory is crucial for attaining good 
     performance.  Since preallocation is not possible via the generic
     matrix creation routine MatCreate(), we recommend for practical 
     problems instead to use the creation routine for a particular matrix
     format, e.g.,
         MatCreateMPIAIJ() - parallel AIJ (compressed sparse row)
         MatCreateMPIBAIJ() - parallel block AIJ
     See the matrix chapter of the users manual for details.
  */
  ierr = MatCreate(PETSC_COMM_WORLD,m*n,m*n,&A); CHKERRA(ierr);

  /* 
     Currently, all PETSc parallel matrix formats are partitioned by
     contiguous chunks of rows across the processors.  Determine which
     rows of the matrix are locally owned. 
  */
  ierr = MatGetOwnershipRange(A,&Istart,&Iend); CHKERRA(ierr);

  /* 
     Set matrix elements for the 2-D, five-point stencil in parallel.
      - Each processor needs to insert only elements that it owns
        locally (but any non-local elements will be sent to the
        appropriate processor during matrix assembly). 
      - Always specify global rows and columns of matrix entries.

     Note: this uses the less common natural ordering that orders first
     all the unknowns for x = h then for x = 2h etc; Hence you see J = I +- n
     instead of J = I +- m as you might expect. The more standard ordering
     would first do all variables for y = h, then y = 2h etc.

   */
  for ( I=Istart; I<Iend; I++ ) { 
    v = -1.0; i = I/n; j = I - i*n;  
    if ( i>0 )   {J = I - n; MatSetValues(A,1,&I,1,&J,&v,INSERT_VALUES); CHKERRA(ierr);}
    if ( i<m-1 ) {J = I + n; MatSetValues(A,1,&I,1,&J,&v,INSERT_VALUES); CHKERRA(ierr);}
    if ( j>0 )   {J = I - 1; MatSetValues(A,1,&I,1,&J,&v,INSERT_VALUES); CHKERRA(ierr);}
    if ( j<n-1 ) {J = I + 1; MatSetValues(A,1,&I,1,&J,&v,INSERT_VALUES); CHKERRA(ierr);}
    v = 4.0; MatSetValues(A,1,&I,1,&I,&v,INSERT_VALUES);
  }

  /* 
     Assemble matrix, using the 2-step process:
       MatAssemblyBegin(), MatAssemblyEnd()
     Computations can be done while messages are in transition
     by placing code between these two statements.
  */
  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY); CHKERRA(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY); CHKERRA(ierr);

  /* 
     Create parallel vectors.
      - We form 1 vector from scratch and then duplicate as needed.
      - When using VecCreate() and VecSetFromOptions() in this example, we specify only the
        vector's global dimension; the parallel partitioning is determined
        at runtime. 
      - When solving a linear system, the vectors and matrices MUST
        be partitioned accordingly.  PETSc automatically generates
        appropriately partitioned matrices and vectors when MatCreate()
        and VecCreate() are used with the same communicator.  
      - The user can alternatively specify the local vector and matrix
        dimensions when more sophisticated partitioning is needed
        (replacing the PETSC_DECIDE argument in the VecCreate() statement
        below).
  */
  ierr = VecCreate(PETSC_COMM_WORLD,PETSC_DECIDE,m*n,&u); CHKERRA(ierr);
  ierr = VecSetFromOptions(u);CHKERRA(ierr);
  ierr = VecDuplicate(u,&b); CHKERRA(ierr); 
  ierr = VecDuplicate(b,&x); CHKERRA(ierr);

  /* 
     Set exact solution; then compute right-hand-side vector.
     By default we use an exact solution of a vector with all
     elements of 1.0;  Alternatively, using the runtime option
     -random_sol forms a solution vector with random components.
  */
  ierr = OptionsHasName(PETSC_NULL,"-random_exact_sol",&flg); CHKERRA(ierr);
  if (flg) {
    ierr = PetscRandomCreate(PETSC_COMM_WORLD,RANDOM_DEFAULT,&rctx); CHKERRA(ierr);
    ierr = VecSetRandom(rctx,u); CHKERRA(ierr);
    ierr = PetscRandomDestroy(rctx); CHKERRA(ierr);
  } else {
    ierr = VecSet(&one,u); CHKERRA(ierr);
  }
  ierr = MatMult(A,u,b); CHKERRA(ierr);

  /*
     View the exact solution vector if desired
  */
  ierr = OptionsHasName(PETSC_NULL,"-view_exact_sol",&flg); CHKERRA(ierr);
  if (flg) {ierr = VecView(u,VIEWER_STDOUT_WORLD); CHKERRA(ierr);}

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
                Create the linear solver and set various options
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /* 
     Create linear solver context
  */
  ierr = SLESCreate(PETSC_COMM_WORLD,&sles); CHKERRA(ierr);

  /* 
     Set operators. Here the matrix that defines the linear system
     also serves as the preconditioning matrix.
  */
  ierr = SLESSetOperators(sles,A,A,DIFFERENT_NONZERO_PATTERN); CHKERRA(ierr);

  /* 
     Set linear solver defaults for this problem (optional).
     - By extracting the KSP and PC contexts from the SLES context,
       we can then directly call any KSP and PC routines to set
       various options.
     - The following two statements are optional; all of these
       parameters could alternatively be specified at runtime via
       SLESSetFromOptions().  All of these defaults can be
       overridden at runtime, as indicated below.
  */

  ierr = SLESGetKSP(sles,&ksp); CHKERRA(ierr);
  ierr = KSPSetTolerances(ksp,1.e-2/((m+1)*(n+1)),1.e-50,PETSC_DEFAULT,
                          PETSC_DEFAULT); CHKERRA(ierr);

  /* 
    Set runtime options, e.g.,
        -ksp_type <type> -pc_type <type> -ksp_monitor -ksp_rtol <rtol>
    These options will override those specified above as long as
    SLESSetFromOptions() is called _after_ any other customization
    routines.
  */
  ierr = SLESSetFromOptions(sles); CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
                      Solve the linear system
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = SLESSolve(sles,b,x,&its); CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
                      Check solution and clean up
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /* 
     Check the error
  */
  ierr = VecAXPY(&neg_one,u,x); CHKERRA(ierr);
  ierr = VecNorm(x,NORM_2,&norm); CHKERRA(ierr);

  /* Scale the norm */
  /*  norm *= sqrt(1.0/((m+1)*(n+1))); */

  /*
     Print convergence information.  PetscPrintf() produces a single 
     print statement from all processes that share a communicator.
     An alternative is PetscFPrintf(), which prints to a file.
  */
  if (norm > 1.e-12)
    PetscPrintf(PETSC_COMM_WORLD,"Norm of error %g iterations %d\n",norm,its);
  else 
    PetscPrintf(PETSC_COMM_WORLD,"Norm of error < 1.e-12 Iterations %d\n",its);

  /* 
     Free work space.  All PETSc objects should be destroyed when they
     are no longer needed.
  */
  ierr = SLESDestroy(sles); CHKERRA(ierr);
  ierr = VecDestroy(u); CHKERRA(ierr);  ierr = VecDestroy(x); CHKERRA(ierr);
  ierr = VecDestroy(b); CHKERRA(ierr);  ierr = MatDestroy(A); CHKERRA(ierr);

  /*
     Always call PetscFinalize() before exiting a program.  This routine
       - finalizes the PETSc libraries as well as MPI
       - provides summary and diagnostic information if certain runtime
         options are chosen (e.g., -log_summary). 
  */
  PetscFinalize();
  return 0;
}
