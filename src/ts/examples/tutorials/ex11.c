static char help[] = "Second Order TVD Finite Volume Example.\n";
/*
  The mesh is read in from an ExodusII file generated by Cubit.
*/
#include <petscts.h>
#include <petscdmcomplex.h>
#ifdef PETSC_HAVE_EXODUSII
#include <exodusII.h>
#else
#error This example requires ExodusII support. Reconfigure using --download-exodusii
#endif

#define DIM 2                   /* Geometric dimension */

typedef struct {
  PetscInt  numGhostCells;
  PetscInt  cEndInterior;  /* First boundary ghost cell */
  Vec       cellgeom, facegeom;
  PetscReal wind[DIM];
  PetscReal inflowState[1];
  PetscReal minradius;
  PetscInt  vtkInterval;        /* For monitor */
} AppCtx;

typedef struct {
  PetscScalar normal[DIM];              /* Area-scaled normals */
  PetscScalar centroid[DIM];            /* Location of centroid (quadrature point) */
} FaceGeom;

typedef struct {
  PetscScalar centroid[DIM];
  PetscScalar volume;
} CellGeom;

PETSC_STATIC_INLINE PetscScalar Dot2(const PetscScalar *x,const PetscScalar *y) { return x[0]*y[0] + x[1]*y[1];}
PETSC_STATIC_INLINE PetscReal Norm2(const PetscScalar *x) { return PetscSqrtReal(PetscAbsScalar(Dot2(x,x)));}
PETSC_STATIC_INLINE void Waxpy2(PetscScalar a,const PetscScalar *x,const PetscScalar *y,PetscScalar *w) { w[0] = a*x[0] + y[0]; w[1] = a*x[1] + y[1]; }

#undef __FUNCT__
#define __FUNCT__ "ConstructGhostCells"
PetscErrorCode ConstructGhostCells(DM *dmGhosted, AppCtx *user)
{
  DM                 dm = *dmGhosted, gdm;
  PetscSection       coordSection, newCoordSection;
  Vec                coordinates;
  const char        *name = "Face Sets";
  IS                 idIS;
  const PetscInt    *ids;
  PetscInt          *newpoints;
  PetscSF            sfPoint, gsfPoint;
  const PetscInt    *leafLocal;
  const PetscSFNode *leafRemote;
  PetscInt           dim, depth, d, maxConeSize, maxSupportSize, numLabels, l;
  PetscInt           numFS, fs, pStart, pEnd, p, cStart, cEnd, c, ghostCell, vStart, vEnd, v;
  PetscMPIInt        rank;
  PetscErrorCode     ierr;

  PetscFunctionBegin;
  ierr = DMCreate(((PetscObject) dm)->comm, &gdm);CHKERRQ(ierr);
  ierr = DMSetType(gdm, DMCOMPLEX);CHKERRQ(ierr);
  ierr = DMComplexGetDimension(dm, &dim);CHKERRQ(ierr);
  ierr = DMComplexSetDimension(gdm, dim);CHKERRQ(ierr);

  ierr = DMComplexGetLabelIdIS(dm, name, &idIS);CHKERRQ(ierr);
  ierr = ISGetLocalSize(idIS, &numFS);CHKERRQ(ierr);
  ierr = ISGetIndices(idIS, &ids);CHKERRQ(ierr);
  user->numGhostCells = 0;
  for(fs = 0; fs < numFS; ++fs) {
    PetscInt numBdFaces;

    ierr = DMComplexGetStratumSize(dm, name, ids[fs], &numBdFaces);CHKERRQ(ierr);
    user->numGhostCells += numBdFaces;
  }
  ierr = DMComplexGetChart(dm, &pStart, &pEnd);CHKERRQ(ierr);
  pEnd += user->numGhostCells;
  ierr = DMComplexSetChart(gdm, pStart, pEnd);CHKERRQ(ierr);
  /* Set cone and support sizes */
  ierr = DMComplexGetHeightStratum(dm, 0, &cStart, &cEnd);CHKERRQ(ierr);
  user->cEndInterior = cEnd;
  for(c = cStart; c < cEnd; ++c) {
    PetscInt size;

    ierr = DMComplexGetConeSize(dm, c, &size);CHKERRQ(ierr);
    ierr = DMComplexSetConeSize(gdm, c, size);CHKERRQ(ierr);
  }
  for(c = cEnd; c < cEnd+user->numGhostCells; ++c) {
    ierr = DMComplexSetConeSize(gdm, c, 1);CHKERRQ(ierr);
  }
  ierr = DMComplexGetDepth(dm, &depth);CHKERRQ(ierr);
  for(d = 0; d < depth; ++d) {
    ierr = DMComplexGetDepthStratum(dm, d, &pStart, &pEnd);CHKERRQ(ierr);
    for(p = pStart; p < pEnd; ++p) {
      PetscInt newp = p+user->numGhostCells;
      PetscInt size;

      ierr = DMComplexGetConeSize(dm, p, &size);CHKERRQ(ierr);
      ierr = DMComplexSetConeSize(gdm, newp, size);CHKERRQ(ierr);
      ierr = DMComplexGetSupportSize(dm, p, &size);CHKERRQ(ierr);
      ierr = DMComplexSetSupportSize(gdm, newp, size);CHKERRQ(ierr);
    }
  }
  for(fs = 0; fs < numFS; ++fs) {
    IS              faceIS;
    const PetscInt *faces;
    PetscInt        numFaces, f;

    ierr = DMComplexGetStratumIS(dm, name, ids[fs], &faceIS);CHKERRQ(ierr);
    ierr = ISGetLocalSize(faceIS, &numFaces);CHKERRQ(ierr);
    ierr = ISGetIndices(faceIS, &faces);CHKERRQ(ierr);
    for(f = 0; f < numFaces; ++f) {
      PetscInt size;

      ierr = DMComplexGetSupportSize(dm, faces[f], &size);CHKERRQ(ierr);
      if (size != 1) SETERRQ2(((PetscObject) dm)->comm, PETSC_ERR_ARG_WRONG, "DM has boundary face %d with %d support cells", faces[f], size);
      ierr = DMComplexSetSupportSize(gdm, faces[f]+user->numGhostCells, 2);CHKERRQ(ierr);
    }
    ierr = ISRestoreIndices(faceIS, &faces);CHKERRQ(ierr);
    ierr = ISDestroy(&faceIS);CHKERRQ(ierr);
  }
  ierr = DMSetUp(gdm);CHKERRQ(ierr);
  /* Set cones and supports, may have to orient supports here */
  ierr = DMComplexGetMaxSizes(dm, &maxConeSize, &maxSupportSize);CHKERRQ(ierr);
  ierr = PetscMalloc(PetscMax(maxConeSize, maxSupportSize) * sizeof(PetscInt), &newpoints);CHKERRQ(ierr);
  ierr = DMComplexGetChart(dm, &pStart, &pEnd);CHKERRQ(ierr);
  for(p = pStart; p < pEnd; ++p) {
    const PetscInt *points, *orientations;
    PetscInt        size, i, newp = p >= cEnd ? p+user->numGhostCells : p;

    ierr = DMComplexGetConeSize(dm, p, &size);CHKERRQ(ierr);
    ierr = DMComplexGetCone(dm, p, &points);CHKERRQ(ierr);
    ierr = DMComplexGetConeOrientation(dm, p, &orientations);CHKERRQ(ierr);
    for(i = 0; i < size; ++i) {
      newpoints[i] = points[i] >= cEnd ? points[i]+user->numGhostCells : points[i];
    }
    ierr = DMComplexSetCone(gdm, newp, newpoints);CHKERRQ(ierr);
    ierr = DMComplexSetConeOrientation(gdm, newp, orientations);CHKERRQ(ierr);
    ierr = DMComplexGetSupportSize(dm, p, &size);CHKERRQ(ierr);
    ierr = DMComplexGetSupport(dm, p, &points);CHKERRQ(ierr);
    for(i = 0; i < size; ++i) {
      newpoints[i] = points[i] >= cEnd ? points[i]+user->numGhostCells : points[i];
    }
    ierr = DMComplexSetSupport(gdm, newp, newpoints);CHKERRQ(ierr);
  }
  ierr = PetscFree(newpoints);CHKERRQ(ierr);
  ghostCell = cEnd;
  for(fs = 0; fs < numFS; ++fs) {
    IS              faceIS;
    const PetscInt *faces;
    PetscInt        numFaces, f;

    ierr = DMComplexGetStratumIS(dm, name, ids[fs], &faceIS);CHKERRQ(ierr);
    ierr = ISGetLocalSize(faceIS, &numFaces);CHKERRQ(ierr);
    ierr = ISGetIndices(faceIS, &faces);CHKERRQ(ierr);
    for(f = 0; f < numFaces; ++f, ++ghostCell) {
      PetscInt newFace = faces[f] + user->numGhostCells;

      ierr = DMComplexSetCone(gdm, ghostCell, &newFace);CHKERRQ(ierr);
      ierr = DMComplexInsertSupport(gdm, newFace, 1, ghostCell);CHKERRQ(ierr);
    }
    ierr = ISRestoreIndices(faceIS, &faces);CHKERRQ(ierr);
    ierr = ISDestroy(&faceIS);CHKERRQ(ierr);
  }
  ierr = ISRestoreIndices(idIS, &ids);CHKERRQ(ierr);
  ierr = ISDestroy(&idIS);CHKERRQ(ierr);
  ierr = DMComplexStratify(gdm);CHKERRQ(ierr);
  /* Convert coordinates */
  ierr = DMComplexGetDepthStratum(dm, 0, &vStart, &vEnd);CHKERRQ(ierr);
  ierr = DMComplexGetCoordinateSection(dm, &coordSection);CHKERRQ(ierr);
  ierr = PetscSectionCreate(((PetscObject) dm)->comm, &newCoordSection);CHKERRQ(ierr);
  ierr = PetscSectionSetNumFields(newCoordSection, 1);CHKERRQ(ierr);
  ierr = PetscSectionSetFieldComponents(newCoordSection, 0, dim);CHKERRQ(ierr);
  ierr = PetscSectionSetChart(newCoordSection, vStart+user->numGhostCells, vEnd+user->numGhostCells);CHKERRQ(ierr);
  for(v = vStart; v < vEnd; ++v) {
    ierr = PetscSectionSetDof(newCoordSection, v+user->numGhostCells, dim);CHKERRQ(ierr);
    ierr = PetscSectionSetFieldDof(newCoordSection, v+user->numGhostCells, 0, dim);CHKERRQ(ierr);
  }
  ierr = PetscSectionSetUp(newCoordSection);CHKERRQ(ierr);
  ierr = DMComplexSetCoordinateSection(gdm, newCoordSection);CHKERRQ(ierr);
  ierr = DMGetCoordinatesLocal(dm, &coordinates);CHKERRQ(ierr);
  ierr = DMSetCoordinatesLocal(gdm, coordinates);CHKERRQ(ierr);
  /* Convert labels */
  ierr = DMComplexGetNumLabels(dm, &numLabels);CHKERRQ(ierr);
  for(l = 0; l < numLabels; ++l) {
    const char *lname;
    PetscBool   isDepth;

    ierr = DMComplexGetLabelName(dm, l, &lname);CHKERRQ(ierr);
    ierr = PetscStrcmp(lname, "depth", &isDepth);CHKERRQ(ierr);
    if (isDepth) continue;
    ierr = DMComplexGetLabelIdIS(dm, lname, &idIS);CHKERRQ(ierr);
    ierr = ISGetLocalSize(idIS, &numFS);CHKERRQ(ierr);
    ierr = ISGetIndices(idIS, &ids);CHKERRQ(ierr);
    for(fs = 0; fs < numFS; ++fs) {
      IS              pointIS;
      const PetscInt *points;
      PetscInt        numPoints;

      ierr = DMComplexGetStratumIS(dm, lname, ids[fs], &pointIS);CHKERRQ(ierr);
      ierr = ISGetLocalSize(pointIS, &numPoints);CHKERRQ(ierr);
      ierr = ISGetIndices(pointIS, &points);CHKERRQ(ierr);
      for(p = 0; p < numPoints; ++p) {
        PetscInt newpoint = points[p] >= cEnd ? points[p]+user->numGhostCells : points[p];

        ierr = DMComplexSetLabelValue(gdm, lname, newpoint, ids[fs]);CHKERRQ(ierr);
      }
      ierr = ISRestoreIndices(pointIS, &points);CHKERRQ(ierr);
      ierr = ISDestroy(&pointIS);CHKERRQ(ierr);
    }
    ierr = ISRestoreIndices(idIS, &ids);CHKERRQ(ierr);
    ierr = ISDestroy(&idIS);CHKERRQ(ierr);
  }
  /* Convert pointSF */
  const PetscSFNode *remotePoints;
  PetscSFNode       *gremotePoints;
  const PetscInt    *localPoints;
  PetscInt          *glocalPoints;
  PetscInt           numRoots, numLeaves;

  ierr = DMGetPointSF(dm, &sfPoint);CHKERRQ(ierr);
  ierr = DMGetPointSF(gdm, &gsfPoint);CHKERRQ(ierr);
  ierr = PetscSFGetGraph(sfPoint, &numRoots, &numLeaves, &localPoints, &remotePoints);CHKERRQ(ierr);
  if (numLeaves >= 0) {
    ierr = PetscMalloc(numLeaves * sizeof(PetscInt),    &glocalPoints);CHKERRQ(ierr);
    ierr = PetscMalloc(numLeaves * sizeof(PetscSFNode), &gremotePoints);CHKERRQ(ierr);
    for(l = 0; l < numLeaves; ++l) {
      glocalPoints[l]        = localPoints[l] >= cEnd ? localPoints[l] + user->numGhostCells : localPoints[l];
      gremotePoints[l].rank  = remotePoints[l].rank;
      gremotePoints[l].index = remotePoints[l].index >= cEnd ? remotePoints[l].index + user->numGhostCells : remotePoints[l].index;
    }
    ierr = PetscSFSetGraph(gsfPoint, numRoots+user->numGhostCells, numLeaves, glocalPoints, PETSC_OWN_POINTER, gremotePoints, PETSC_OWN_POINTER);CHKERRQ(ierr);
  }
  /* Make label for VTK output */
  ierr = MPI_Comm_rank(((PetscObject) dm)->comm, &rank);CHKERRQ(ierr);
  ierr = PetscSFGetGraph(sfPoint, PETSC_NULL, &numLeaves, &leafLocal, &leafRemote);CHKERRQ(ierr);
  for(l = 0, c = cStart; l < numLeaves && c < cEnd; ++l, ++c) {
    for(; c < leafLocal[l] && c < cEnd; ++c) {
      ierr = DMComplexSetLabelValue(gdm, "vtk", c, 1);CHKERRQ(ierr);
    }
    if (leafLocal[l] >= cEnd) break;
    if (leafRemote[c].rank == rank) {
      ierr = DMComplexSetLabelValue(gdm, "vtk", c, 1);CHKERRQ(ierr);
    }
  }
  for(; c < cEnd; ++c) {
    ierr = DMComplexSetLabelValue(gdm, "vtk", c, 1);CHKERRQ(ierr);
  }
  /* Make a label for ghost faces */
  PetscInt fStart, fEnd, f;

  ierr = DMComplexGetHeightStratum(gdm, 1, &fStart, &fEnd);CHKERRQ(ierr);
  ierr = DMComplexCreateLabel(gdm, "ghost");CHKERRQ(ierr);
  for(f = fStart; f < fEnd; ++f) {
    PetscInt numCells;

    ierr = DMComplexGetSupportSize(gdm, f, &numCells);CHKERRQ(ierr);
    if (numCells < 2) {
      ierr = DMComplexSetLabelValue(gdm, "ghost", f, 1);CHKERRQ(ierr);
    } else {
      const PetscInt *cells;
      PetscInt        vA, vB;

      ierr = DMComplexGetSupport(gdm, f, &cells);CHKERRQ(ierr);
      ierr = DMComplexGetLabelValue(gdm, "vtk", cells[0], &vA);CHKERRQ(ierr);
      ierr = DMComplexGetLabelValue(gdm, "vtk", cells[1], &vB);CHKERRQ(ierr);
      if (!vA && !vB) {ierr = DMComplexSetLabelValue(gdm, "ghost", f, 1);CHKERRQ(ierr);}
    }
  }

  ierr = DMSetFromOptions(gdm);CHKERRQ(ierr);
  ierr = DMDestroy(dmGhosted);CHKERRQ(ierr);
  *dmGhosted = gdm;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "ConstructGeometry"
/* Set up face data and cell data */
PetscErrorCode ConstructGeometry(DM dm, Vec *facegeom, Vec *cellgeom, AppCtx *user)
{
  DM             dmFace, dmCell;
  PetscSection   sectionFace, sectionCell;
  PetscSection   coordSection;
  Vec            coordinates;
  PetscReal      minradius;
  PetscScalar   *fgeom, *cgeom;
  PetscInt       dim, cStart, cEnd, c, fStart, fEnd, f;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMComplexGetDimension(dm, &dim);CHKERRQ(ierr);
  ierr = DMComplexGetCoordinateSection(dm, &coordSection);CHKERRQ(ierr);
  ierr = DMGetCoordinatesLocal(dm, &coordinates);CHKERRQ(ierr);

  /* Make cell centroids and volumes */
  ierr = DMComplexClone(dm, &dmCell);CHKERRQ(ierr);
  ++coordSection->refcnt;
  ierr = DMComplexSetCoordinateSection(dmCell, coordSection);CHKERRQ(ierr);
  ierr = DMSetCoordinatesLocal(dmCell, coordinates);CHKERRQ(ierr);
  ierr = PetscSectionCreate(((PetscObject) dm)->comm, &sectionCell);CHKERRQ(ierr);
  ierr = DMComplexGetHeightStratum(dm, 0, &cStart, &cEnd);CHKERRQ(ierr);
  ierr = PetscSectionSetChart(sectionCell, cStart, cEnd);CHKERRQ(ierr);
  for(c = cStart; c < cEnd; ++c) {
    ierr = PetscSectionSetDof(sectionCell, c, sizeof(CellGeom)/sizeof(PetscScalar));CHKERRQ(ierr);
  }
  ierr = PetscSectionSetUp(sectionCell);CHKERRQ(ierr);
  ierr = DMSetDefaultSection(dmCell, sectionCell);CHKERRQ(ierr);

  ierr = DMCreateLocalVector(dmCell, cellgeom);CHKERRQ(ierr);
  ierr = VecGetArray(*cellgeom, &cgeom);CHKERRQ(ierr);
  for(c = cStart; c < user->cEndInterior; ++c) {
    const PetscScalar *coords = PETSC_NULL;
    PetscInt           coordSize, numCorners, p;
    PetscScalar        sx = 0, sy = 0;
    CellGeom          *cg;

    ierr = DMComplexVecGetClosure(dm, coordSection, coordinates, c, &coordSize, &coords);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRef(dmCell, c, cgeom, &cg);CHKERRQ(ierr);
    ierr = PetscMemzero(cg,sizeof(*cg));CHKERRQ(ierr);
    numCorners = coordSize/dim;
    for(p = 0; p < numCorners; ++p) {
      const PetscScalar *x = coords+p*dim, *y = coords+((p+1)%numCorners)*dim;
      const PetscScalar cross = x[0]*y[1] - x[1]*y[0];
      cg->volume += 0.5*cross;
      sx += (x[0] + y[0])*cross;
      sy += (x[1] + y[1])*cross;
    }
    cg->centroid[0] = sx / (6*cg->volume);
    cg->centroid[1] = sy / (6*cg->volume);
    cg->volume = PetscAbsScalar(cg->volume);
    ierr = DMComplexVecRestoreClosure(dm, coordSection, coordinates, c, &coordSize, &coords);CHKERRQ(ierr);
  }

  /* Make normals and fill in ghost centroids */
  ierr = DMComplexClone(dm, &dmFace);CHKERRQ(ierr);
  ierr = PetscSectionCreate(((PetscObject) dm)->comm, &sectionFace);CHKERRQ(ierr);
  ierr = DMComplexGetHeightStratum(dm, 1, &fStart, &fEnd);CHKERRQ(ierr);
  ierr = PetscSectionSetChart(sectionFace, fStart, fEnd);CHKERRQ(ierr);
  for(f = fStart; f < fEnd; ++f) {
    ierr = PetscSectionSetDof(sectionFace, f, sizeof(FaceGeom)/sizeof(PetscScalar));CHKERRQ(ierr);
  }
  ierr = PetscSectionSetUp(sectionFace);CHKERRQ(ierr);
  ierr = DMSetDefaultSection(dmFace, sectionFace);CHKERRQ(ierr);
  ierr = DMCreateLocalVector(dmFace, facegeom);CHKERRQ(ierr);
  ierr = VecGetArray(*facegeom, &fgeom);CHKERRQ(ierr);
  minradius = PETSC_MAX_REAL;
  for(f = fStart; f < fEnd; ++f) {
    const PetscScalar *coords = PETSC_NULL;
    const PetscInt    *cells;
    PetscInt           ghost,i,coordSize;
    PetscScalar        v[2];
    FaceGeom          *fg;
    CellGeom          *cL,*cR;

    ierr = DMComplexGetLabelValue(dm, "ghost", f, &ghost);CHKERRQ(ierr);
    if (ghost >= 0) continue;
    ierr = DMComplexVecGetClosure(dm, coordSection, coordinates, f, &coordSize, &coords);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRef(dmFace, f, fgeom, &fg);CHKERRQ(ierr);
    /* Only support edges right now */
    if (coordSize != dim*2) SETERRQ(((PetscObject) dm)->comm, PETSC_ERR_SUP, "We only support edges right now");
    fg->centroid[0] = 0.5*(coords[0] + coords[dim]);
    fg->centroid[1] = 0.5*(coords[1] + coords[dim+1]);
    fg->normal[0] =  (coords[1] - coords[dim+1]);
    fg->normal[1] = -(coords[0] - coords[dim+0]);
    ierr = DMComplexVecRestoreClosure(dm, coordSection, coordinates, f, &coordSize, &coords);CHKERRQ(ierr);
    ierr = DMComplexGetSupport(dm, f, &cells);CHKERRQ(ierr);
    /* Reflect ghost centroid across plane of face */
    for (i=0; i<2; i++) {
      if (cells[i] >= user->cEndInterior) {
        const CellGeom *ci;
        CellGeom       *cg;
        PetscScalar    c2f[2],a;
        ierr = DMComplexPointLocalRead(dmCell, cells[(i+1)%2], cgeom, &ci);CHKERRQ(ierr);
        Waxpy2(-1,ci->centroid,fg->centroid,c2f); /* cell to face centroid */
        a = Dot2(c2f,fg->normal)/Dot2(fg->normal,fg->normal);
        ierr = DMComplexPointLocalRef(dmCell, cells[i], cgeom, &cg);CHKERRQ(ierr);
        Waxpy2(2*a,fg->normal,ci->centroid,cg->centroid);
        cg->volume = ci->volume;
      }
    }
    /* Flip face orientation if necessary to match ordering in support */
    ierr = DMComplexPointLocalRead(dmCell, cells[0], cgeom, &cL);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRead(dmCell, cells[1], cgeom, &cR);CHKERRQ(ierr);
    Waxpy2(-1,cL->centroid,cR->centroid,v);
    if (Dot2(fg->normal, v) < 0) {
      fg->normal[0] = -fg->normal[0];
      fg->normal[1] = -fg->normal[1];
    }
    if (Dot2(fg->normal,v) <= 0) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_PLIB,"Face direction could not be fixed");
    /* Update minimum radius */
    Waxpy2(-1,fg->centroid,cL->centroid,v);
    minradius = PetscMin(minradius,Norm2(v));
    Waxpy2(-1,fg->centroid,cR->centroid,v);
    minradius = PetscMin(minradius,Norm2(v));
  }
  ierr = VecRestoreArray(*facegeom, &fgeom);CHKERRQ(ierr);
  ierr = VecRestoreArray(*cellgeom, &cgeom);CHKERRQ(ierr);
  ierr = MPI_Allreduce(&minradius, &user->minradius, 1, MPIU_SCALAR, MPI_MIN, ((PetscObject) dm)->comm);CHKERRQ(ierr);
  ierr = DMDestroy(&dmCell);CHKERRQ(ierr);
  ierr = DMDestroy(&dmFace);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SetUpLocalSpace"
PetscErrorCode SetUpLocalSpace(DM dm, AppCtx *user)
{
  PetscSection   stateSection;
  PetscInt       dof = 1, *cind, d, stateSize, cStart, cEnd, c;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMComplexGetHeightStratum(dm, 0, &cStart, &cEnd);CHKERRQ(ierr);
  ierr = PetscSectionCreate(((PetscObject) dm)->comm, &stateSection);CHKERRQ(ierr);
  ierr = PetscSectionSetChart(stateSection, cStart, cEnd);CHKERRQ(ierr);
  for(c = cStart; c < cEnd; ++c) {
    PetscInt val;

    ierr = PetscSectionSetDof(stateSection, c, dof);CHKERRQ(ierr);
#if 0
    ierr = DMComplexGetLabelValue(dm, "vtk", c, &val);CHKERRQ(ierr);
    if (!val) {ierr = PetscSectionSetConstraintDof(stateSection, c, dof);CHKERRQ(ierr);}
#endif
  }
  for(c = user->cEndInterior; c < cEnd; ++c) {
    ierr = PetscSectionSetConstraintDof(stateSection, c, dof);CHKERRQ(ierr);
  }
  ierr = PetscSectionSetUp(stateSection);CHKERRQ(ierr);
  ierr = PetscMalloc(dof * sizeof(PetscInt), &cind);CHKERRQ(ierr);
  for(d = 0; d < dof; ++d) cind[d] = d;
#if 0
  for(c = cStart; c < cEnd; ++c) {
    PetscInt val;

    ierr = DMComplexGetLabelValue(dm, "vtk", c, &val);CHKERRQ(ierr);
    if (!val) {ierr = PetscSectionSetConstraintIndices(stateSection, c, cind);CHKERRQ(ierr);}
  }
#endif
  for(c = user->cEndInterior; c < cEnd; ++c) {
    ierr = PetscSectionSetConstraintIndices(stateSection, c, cind);CHKERRQ(ierr);
  }
  ierr = PetscFree(cind);CHKERRQ(ierr);
  ierr = PetscSectionGetStorageSize(stateSection, &stateSize);CHKERRQ(ierr);
  ierr = DMSetDefaultSection(dm,stateSection);CHKERRQ(ierr);
  if (0) {                      /* Crazy that DMSetDefaultSection does not increment refct */
    ierr = PetscSectionDestroy(&stateSection);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SetInitialCondition"
PetscErrorCode SetInitialCondition(DM dm, Vec X, AppCtx *user)
{
  DM                 dmCell;
  Vec                locX;
  const PetscScalar  *cellgeom;
  PetscScalar        *x;
  PetscInt           cStart, cEnd, cEndInterior = user->cEndInterior, c;
  PetscErrorCode     ierr;

  PetscFunctionBegin;
  ierr = VecGetDM(user->cellgeom, &dmCell);CHKERRQ(ierr);
  ierr = DMGetLocalVector(dm, &locX);CHKERRQ(ierr);
  ierr = DMComplexGetHeightStratum(dm, 0, &cStart, &cEnd);CHKERRQ(ierr);
  ierr = VecGetArrayRead(user->cellgeom, &cellgeom);CHKERRQ(ierr);
  ierr = VecGetArray(locX, &x);CHKERRQ(ierr);
  for(c = cStart; c < cEndInterior; ++c) {
    const CellGeom *cg;
    PetscScalar *xc;

    ierr = DMComplexPointLocalRead(dmCell,c,cellgeom,&cg);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRef(dm,c,x,&xc);CHKERRQ(ierr);
    xc[0] = cg->centroid[0] + 3*cg->centroid[1];
  }
  ierr = VecRestoreArrayRead(user->cellgeom, &cellgeom);CHKERRQ(ierr);
  ierr = VecRestoreArray(locX, &x);CHKERRQ(ierr);
  ierr = DMLocalToGlobalBegin(dm, locX, INSERT_VALUES, X);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(dm, locX, INSERT_VALUES, X);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(dm, &locX);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "Boundary_Inflow"
static PetscErrorCode Boundary_Inflow(AppCtx *user, const PetscReal *n, const PetscScalar *xI, PetscScalar *xG)
{
  PetscFunctionBegin;
  xG[0] = user->inflowState[0];
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "Boundary_Outflow"
static PetscErrorCode Boundary_Outflow(AppCtx *user, const PetscReal *n, const PetscScalar *xI, PetscScalar *xG)
{
  PetscFunctionBegin;
  xG[0] = xI[0];
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "Boundary_Wall"
static PetscErrorCode Boundary_Wall(AppCtx *user, const PetscReal *n, const PetscScalar *xI, PetscScalar *xG)
{
  /* PetscReal vn = Dot2(user->wind,n); */
  PetscFunctionBegin;
  xG[0] = xI[0];
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "Riemann"
static PetscErrorCode Riemann(AppCtx *user, const PetscReal *n, const PetscScalar *xL, const PetscScalar *xR, PetscScalar *flux)
{
  PetscReal wn;

  PetscFunctionBegin;
  wn = Dot2(user->wind, n);
  if (wn > 0) {
    flux[0] = xL[0]*wn;
  } else {
    flux[0] = xR[0]*wn;
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "ApplyBC"
static PetscErrorCode ApplyBC(DM dm, Vec locX, AppCtx *user)
{
  const char     *name = "Face Sets";
  DM             dmFace;
  IS             idIS;
  const PetscInt *ids;
  PetscScalar    *x;
  const PetscScalar *facegeom;
  PetscInt       numFS, fs;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecGetDM(user->facegeom,&dmFace);CHKERRQ(ierr);
  ierr = DMComplexGetLabelIdIS(dm, name, &idIS);CHKERRQ(ierr);
  ierr = ISGetLocalSize(idIS, &numFS);CHKERRQ(ierr);
  ierr = ISGetIndices(idIS, &ids);CHKERRQ(ierr);
  ierr = VecGetArrayRead(user->facegeom, &facegeom);CHKERRQ(ierr);
  ierr = VecGetArray(locX, &x);CHKERRQ(ierr);
  for(fs = 0; fs < numFS; ++fs) {
    PetscErrorCode (*bcFunc)(AppCtx*,const PetscReal*,const PetscScalar*,PetscScalar*);
    IS              faceIS;
    const PetscInt *faces;
    PetscInt        numFaces, f;

    switch (ids[fs]) {
    case 100:                   /* bottom */
      bcFunc = Boundary_Inflow;
      break;
    case 101:                   /* top */
    case 200:                   /* left */
      bcFunc = Boundary_Wall;
      break;
    case 300:                   /* right */
      bcFunc = Boundary_Outflow;
      break;
    default: SETERRQ1(((PetscObject)dm)->comm,PETSC_ERR_USER,"Invalid boundary Id: %D",ids[fs]);
    }
    ierr = DMComplexGetStratumIS(dm, name, ids[fs], &faceIS);CHKERRQ(ierr);
    ierr = ISGetLocalSize(faceIS, &numFaces);CHKERRQ(ierr);
    ierr = ISGetIndices(faceIS, &faces);CHKERRQ(ierr);
    for(f = 0; f < numFaces; ++f) {
      const PetscInt    face = faces[f], *cells;
      const PetscScalar *xI;
      PetscScalar       *xG;
      const FaceGeom    *fg;

      ierr = DMComplexPointLocalRead(dmFace, face, facegeom, &fg);CHKERRQ(ierr);
      ierr = DMComplexGetSupport(dm, face, &cells);CHKERRQ(ierr);
      ierr = DMComplexPointLocalRead(dm, cells[0], x, &xI);CHKERRQ(ierr);
      ierr = DMComplexPointLocalRef(dm, cells[1], x, &xG);CHKERRQ(ierr);
      ierr = (*bcFunc)(user, fg->normal, xI, xG);CHKERRQ(ierr);
    }
    ierr = ISRestoreIndices(faceIS, &faces);CHKERRQ(ierr);
    ierr = ISDestroy(&faceIS);CHKERRQ(ierr);
  }
  ierr = VecRestoreArray(locX, &x);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(user->facegeom,&facegeom);CHKERRQ(ierr);
  ierr = ISRestoreIndices(idIS, &ids);CHKERRQ(ierr);
  ierr = ISDestroy(&idIS);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "RHSFunction"
static PetscErrorCode RHSFunction(TS ts,PetscReal time,Vec X,Vec F,void *ctx)
{
  AppCtx            *user = (AppCtx *) ctx;
  DM                 dm, dmFace, dmCell;
  PetscSection       section;
  Vec                locX, locF;
  const PetscScalar *facegeom, *cellgeom, *x;
  PetscScalar       *f;
  PetscInt           fStart, fEnd, face;
  PetscErrorCode     ierr;

  PetscFunctionBegin;
  ierr = TSGetDM(ts,&dm);CHKERRQ(ierr);
  ierr = VecGetDM(user->facegeom,&dmFace);CHKERRQ(ierr);
  ierr = VecGetDM(user->cellgeom,&dmCell);CHKERRQ(ierr);
  ierr = DMGetLocalVector(dm,&locX);CHKERRQ(ierr);
  ierr = DMGetLocalVector(dm,&locF);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(dm, X, INSERT_VALUES, locX);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(dm, X, INSERT_VALUES, locX);CHKERRQ(ierr);
  ierr = DMGetDefaultSection(dm, &section);CHKERRQ(ierr);

  ierr = ApplyBC(dm, locX, user);CHKERRQ(ierr);

  ierr = VecZeroEntries(locF);CHKERRQ(ierr);
  ierr = VecGetArrayRead(user->facegeom,&facegeom);CHKERRQ(ierr);
  ierr = VecGetArrayRead(user->cellgeom,&cellgeom);CHKERRQ(ierr);
  ierr = VecGetArrayRead(locX,&x);CHKERRQ(ierr);
  ierr = VecGetArray(locF,&f);CHKERRQ(ierr);
  ierr = DMComplexGetHeightStratum(dm, 1, &fStart, &fEnd);CHKERRQ(ierr);
  for(face = fStart; face < fEnd; ++face) {
    const PetscInt    *cells,dof = 1;
    PetscInt           ghost,i;
    PetscScalar        flux[1],*fL,*fR;
    const FaceGeom    *fg;
    const CellGeom    *cgL,*cgR;
    const PetscScalar *xL,*xR;

    ierr = DMComplexGetLabelValue(dm, "ghost", face, &ghost);CHKERRQ(ierr);
    if (ghost >= 0) continue;
    ierr = DMComplexGetSupport(dm, face, &cells);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRead(dmFace,face,facegeom,&fg);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRead(dmCell,cells[0],cellgeom,&cgL);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRead(dmCell,cells[1],cellgeom,&cgR);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRead(dm,cells[0],x,&xL);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRead(dm,cells[1],x,&xR);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRef(dm,cells[0],f,&fL);CHKERRQ(ierr);
    ierr = DMComplexPointLocalRef(dm,cells[1],f,&fR);CHKERRQ(ierr);
    ierr = Riemann(user, fg->normal, xL, xR, flux);CHKERRQ(ierr);
    for (i=0; i<dof; i++) {
      fL[i] -= flux[i] / cgL->volume;
      fR[i] += flux[i] / cgR->volume;
    }
  }
  ierr = VecRestoreArrayRead(locX,&x);CHKERRQ(ierr);
  ierr = VecRestoreArray(locF,&f);CHKERRQ(ierr);
  ierr = DMLocalToGlobalBegin(dm,locF,INSERT_VALUES,F);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(dm,locF,INSERT_VALUES,F);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(dm,&locX);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(dm,&locF);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "OutputVTK"
static PetscErrorCode OutputVTK(DM dm, const char *filename, PetscViewer *viewer)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscViewerCreate(((PetscObject) dm)->comm, viewer);CHKERRQ(ierr);
  ierr = PetscViewerSetType(*viewer, PETSCVIEWERVTK);CHKERRQ(ierr);
  ierr = PetscViewerFileSetName(*viewer, filename);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(*viewer, PETSC_VIEWER_ASCII_VTK);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "MonitorVTK"
static PetscErrorCode MonitorVTK(TS ts,PetscInt stepnum,PetscReal time,Vec X,void *ctx)
{
  AppCtx         *user = (AppCtx*)ctx;
  DM             dm;
  PetscViewer    viewer;
  char           filename[PETSC_MAX_PATH_LEN];
  PetscReal      xnorm;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscObjectSetName((PetscObject) X, "solution");CHKERRQ(ierr);
  ierr = VecGetDM(X,&dm);CHKERRQ(ierr);
  ierr = VecNorm(X,NORM_INFINITY,&xnorm);CHKERRQ(ierr);
  ierr = PetscPrintf(((PetscObject)ts)->comm,"% 3D  time %8.2G  |x| %8.2G\n",stepnum,time,xnorm);CHKERRQ(ierr);
  if (user->vtkInterval < 1) PetscFunctionReturn(0);
  if ((stepnum == -1) ^ (stepnum % user->vtkInterval == 0)) {
    if (stepnum == -1) {        /* Final time is not multiple of normal time interval, write it anyway */
      ierr = TSGetTimeStepNumber(ts,&stepnum);CHKERRQ(ierr);
    }
    ierr = PetscSNPrintf(filename,sizeof filename,"ex11-%03D.vtk",stepnum);CHKERRQ(ierr);
    ierr = OutputVTK(dm,filename,&viewer);CHKERRQ(ierr);
    ierr = VecView(X,viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc, char **argv)
{
  MPI_Comm       comm;
  AppCtx         user;
  DM             dm, dmDist;
  PetscReal      ftime,cfl,dt;
  PetscInt       nsteps;
  int            CPU_word_size = 0, IO_word_size = 0, exoid;
  float          version;
  TS             ts;
  TSConvergedReason reason;
  Vec            X;
  PetscViewer    viewer;
  PetscMPIInt    rank;
  char           filename[PETSC_MAX_PATH_LEN] = "sevenside.exo";
  PetscBool      vtkCellGeom;
  PetscErrorCode ierr;

  ierr = PetscInitialize(&argc, &argv, (char *) 0, help);CHKERRQ(ierr);
  comm = PETSC_COMM_WORLD;
  ierr = MPI_Comm_rank(comm, &rank);CHKERRQ(ierr);

  ierr = PetscOptionsBegin(comm,PETSC_NULL,"Unstructured Finite Volume Options","");CHKERRQ(ierr);
  {
    PetscInt two = 2,dof;
    user.wind[0] = 0.0;
    user.wind[1] = 1.0;
    ierr = PetscOptionsRealArray("-ufv_wind","background wind vx,vy","",user.wind,&two,PETSC_NULL);CHKERRQ(ierr);
    dof = 1;
    user.inflowState[0] = -2.0;
    ierr = PetscOptionsRealArray("-ufv_inflow","Inflow state","",user.inflowState,&dof,PETSC_NULL);CHKERRQ(ierr);
    cfl = 0.9 * 4;              /* default SSPRKS2 with s=5 stages is stable for CFL number s-1 */
    ierr = PetscOptionsReal("-ufv_cfl","CFL number per step","",cfl,&cfl,PETSC_NULL);CHKERRQ(ierr);
    ierr = PetscOptionsString("-f","Exodus.II filename to read","",filename,filename,sizeof(filename),PETSC_NULL);CHKERRQ(ierr);
    user.vtkInterval = 1;
    ierr = PetscOptionsInt("-ufv_vtk_interval","VTK output interval (0 to disable)","",user.vtkInterval,&user.vtkInterval,PETSC_NULL);CHKERRQ(ierr);
    vtkCellGeom = PETSC_FALSE;
    ierr = PetscOptionsBool("-ufv_vtk_cellgeom","Write cell geometry (for debugging)","",vtkCellGeom,&vtkCellGeom,PETSC_NULL);CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);

  if (!rank) {
    exoid = ex_open(filename, EX_READ, &CPU_word_size, &IO_word_size, &version);
    if (exoid <= 0) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"ex_open(\"%s\",...) did not return a valid file ID",filename);
  } else exoid = -1;                 /* Not used */
  ierr = DMComplexCreateExodus(comm, exoid, PETSC_TRUE, &dm);CHKERRQ(ierr);
  if (!rank) {ierr = ex_close(exoid);CHKERRQ(ierr);}
  /* Distribute mesh */
  ierr = DMComplexDistribute(dm, "chaco", 1, &dmDist);CHKERRQ(ierr);
  if (dmDist) {
    ierr = DMDestroy(&dm);CHKERRQ(ierr);
    dm   = dmDist;
  }
  ierr = DMSetFromOptions(dm);CHKERRQ(ierr);

  ierr = ConstructGhostCells(&dm, &user);CHKERRQ(ierr);
  ierr = ConstructGeometry(dm, &user.facegeom, &user.cellgeom, &user);CHKERRQ(ierr);
  if (0) {ierr = VecView(user.facegeom, PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);}

  /* Set up DM with section describing local vector and configure local vector. */
  ierr = SetUpLocalSpace(dm, &user);CHKERRQ(ierr);

  ierr = DMCreateGlobalVector(dm, &X);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) X, "solution");CHKERRQ(ierr);
  ierr = SetInitialCondition(dm, X, &user);CHKERRQ(ierr);
  if (vtkCellGeom) {
    ierr = OutputVTK(dm, "ex11-cellgeom.vtk", &viewer);CHKERRQ(ierr);
    ierr = VecView(user.cellgeom, viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }

  ierr = TSCreate(comm, &ts);CHKERRQ(ierr);
  ierr = TSSetType(ts, TSSSP);CHKERRQ(ierr);
  ierr = TSSetDM(ts, dm);CHKERRQ(ierr);
  ierr = TSMonitorSet(ts,MonitorVTK,&user,PETSC_NULL);CHKERRQ(ierr);
  ierr = TSSetRHSFunction(ts,PETSC_NULL,RHSFunction,&user);CHKERRQ(ierr);
  ierr = TSSetDuration(ts,1000,2.0);CHKERRQ(ierr);
  dt = cfl * user.minradius / Norm2(user.wind);
  ierr = TSSetInitialTimeStep(ts,0.0,dt);CHKERRQ(ierr);
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);
  ierr = TSSolve(ts,X);CHKERRQ(ierr);
  ierr = TSGetSolveTime(ts,&ftime);CHKERRQ(ierr);
  ierr = TSGetTimeStepNumber(ts,&nsteps);CHKERRQ(ierr);
  ierr = TSGetConvergedReason(ts,&reason);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%s at time %G after %D steps\n",TSConvergedReasons[reason],ftime,nsteps);CHKERRQ(ierr);
  ierr = TSDestroy(&ts);CHKERRQ(ierr);

  ierr = VecDestroy(&user.cellgeom);CHKERRQ(ierr);
  ierr = VecDestroy(&user.facegeom);CHKERRQ(ierr);
  ierr = VecDestroy(&X);CHKERRQ(ierr);
  ierr = DMDestroy(&dm);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return(0);
}
