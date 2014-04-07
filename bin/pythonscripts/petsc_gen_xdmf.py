#!/usr/bin/env python
import h5py
import numpy as np
import os, sys

class Xdmf:
  def __init__(self, filename):
    self.filename = filename
    self.cellMap  = {1 : {1 : 'Polyvertex', 2 : 'Polyline'}, 2 : {3 : 'Triangle', 4 : 'Quadrilateral'}, 3 : {4 : 'Tetrahedron', 8 : 'Hexahedron'}}
    self.typeMap  = {'scalar' : 'Scalar', 'vector' : 'Vector', 'tensor' : 'Tensor6', 'matrix' : 'Matrix'}
    self.typeExt  = {2 : {'vector' : ['x', 'y'], 'tensor' : ['xx', 'yy', 'xy']}, 3 : {'vector' : ['x', 'y', 'z'], 'tensor' : ['xx', 'yy', 'zz', 'xy', 'yz', 'xz']}}
    return

  def writeHeader(self, fp, hdfFilename):
    fp.write('''\
<?xml version="1.0" ?>
<!DOCTYPE Xdmf SYSTEM "Xdmf.dtd" [
<!ENTITY HeavyData "%s">
]>
''' % os.path.basename(hdfFilename))
    fp.write('\n<Xdmf>\n  <Domain Name="domain">\n')
    return

  def writeCells(self, fp, numCells, numCorners):
    fp.write('''\
    <DataItem Name="cells"
	      ItemType="Uniform"
	      Format="HDF"
	      NumberType="Float" Precision="8"
	      Dimensions="%d %d">
      &HeavyData;:/topology/cells
    </DataItem>
''' % (numCells, numCorners))
    return

  def writeVertices(self, fp, numVertices, spaceDim):
    fp.write('''\
    <DataItem Name="vertices"
	      Format="HDF"
	      Dimensions="%d %d">
      &HeavyData;:/geometry/vertices
    </DataItem>
    <!-- ============================================================ -->
''' % (numVertices, spaceDim))
    return

  def writeTimeGridHeader(self, fp, time):
    fp.write('''\
    <Grid Name="TimeSeries" GridType="Collection" CollectionType="Temporal">
      <Time TimeType="List">
        <DataItem Format="XML" NumberType="Float" Dimensions="%d">
          ''' % (len(time)))
    fp.write(' '.join(map(str, map(int, time))))
    fp.write('''
        </DataItem>
      </Time>
''')
    return

  def writeSpaceGridHeader(self, fp, numCells, numCorners, cellDim, spaceDim):
    fp.write('''\
      <Grid Name="domain" GridType="Uniform">
	<Topology
	   TopologyType="%s"
	   NumberOfElements="%d">
	  <DataItem Reference="XML">
	    /Xdmf/Domain/DataItem[@Name="cells"]
	  </DataItem>
	</Topology>
	<Geometry GeometryType="%s">
	  <DataItem Reference="XML">
	    /Xdmf/Domain/DataItem[@Name="vertices"]
	  </DataItem>
	</Geometry>
''' % (self.cellMap[cellDim][numCorners], numCells, "XYZ" if spaceDim > 2 else "XY"))
    return

  def writeFieldSingle(self, fp, numSteps, timestep, spaceDim, name, f, domain):
    if len(f[1].shape) > 2:
      dof = f[1].shape[1]
      bs  = f[1].shape[2]
    else:
      dof = f[1].shape[0]
      bs  = f[1].shape[1]
    fp.write('''\
	<Attribute
	   Name="%s"
	   Type="%s"
	   Center="%s">
          <DataItem ItemType="HyperSlab"
		    Dimensions="1 %d %d"
		    Type="HyperSlab">
            <DataItem
	       Dimensions="3 3"
	       Format="XML">
              %d 0 0
              1 1 1
              1 %d %d
	    </DataItem>
	    <DataItem
	       DataType="Float" Precision="8"
	       Dimensions="%d %d %d"
	       Format="HDF">
	      &HeavyData;:%s
	    </DataItem>
	  </DataItem>
	</Attribute>
''' % (f[0], self.typeMap[f[1].attrs['vector_field_type']], domain, dof, bs, timestep, dof, bs, numSteps, dof, bs, name))
    return

  def writeFieldComponents(self, fp, numSteps, timestep, spaceDim, name, f, domain):
    vtype = f[1].attrs['vector_field_type']
    if len(f[1].shape) > 2:
      dof = f[1].shape[1]
      bs  = f[1].shape[2]
    else:
      dof = f[1].shape[0]
      bs  = f[1].shape[1]
    for c in range(bs):
      ext = self.typeExt[spaceDim][vtype][c]
      fp.write('''\
	<Attribute
	   Name="%s"
	   Type="Scalar"
	   Center="%s">
          <DataItem ItemType="HyperSlab"
		    Dimensions="1 %d 1"
		    Type="HyperSlab">
            <DataItem
	       Dimensions="3 3"
	       Format="XML">
              %d 0 %d
              1 1 1
              1 %d 1
	    </DataItem>
	    <DataItem
	       DataType="Float" Precision="8"
	       Dimensions="%d %d %d"
	       Format="HDF">
	      &HeavyData;:%s
	    </DataItem>
	  </DataItem>
	</Attribute>
''' % (f[0]+'_'+ext, domain, dof, timestep, c, dof, numSteps, dof, bs, name))
    return

  def writeField(self, fp, numSteps, timestep, spaceDim, name, f, domain):
    vtype = f[1].attrs['vector_field_type']
    if ((spaceDim == 2 and vtype in ['vector', 'tensor', 'matrix']) or (spaceDim == 2 and vtype in ['tensor', 'matrix'])):
      self.writeFieldComponents(fp, numSteps, timestep, spaceDim, name, f, domain)
    else:
      self.writeFieldSingle(fp, numSteps, timestep, spaceDim, name, f, domain)
    return

  def writeSpaceGridFooter(self, fp):
    fp.write('      </Grid>\n')
    return

  def writeTimeGridFooter(self, fp):
    fp.write('    </Grid>\n')
    return

  def writeFooter(self, fp):
    fp.write('  </Domain>\n</Xdmf>\n')
    return

  def write(self, hdfFilename, numCells, numCorners, cellDim, numVertices, spaceDim, time, vfields, cfields):
    useTime = not (len(time) < 2 and time[0] == -1)
    with file(self.filename, 'w') as fp:
      self.writeHeader(fp, hdfFilename)
      self.writeCells(fp, numCells, numCorners)
      self.writeVertices(fp, numVertices, spaceDim)
      if useTime: self.writeTimeGridHeader(fp, time)
      for t in range(len(time)):
        self.writeSpaceGridHeader(fp, numCells, numCorners, cellDim, spaceDim)
        for vf in vfields: self.writeField(fp, len(time), t, spaceDim, '/vertex_fields/'+vf[0], vf, 'Node')
        for cf in cfields: self.writeField(fp, len(time), t, spaceDim, '/cell_fields/'+cf[0], cf, 'Cell')
        self.writeSpaceGridFooter(fp)
      if useTime: self.writeTimeGridFooter(fp)
      self.writeFooter(fp)
    return

def generateXdmf(hdfFilename, xdmfFilename = None):
  if xdmfFilename is None:
    xdmfFilename = os.path.splitext(hdfFilename)[0] + '.xmf'
  # Read mesh
  h5          = h5py.File(hdfFilename, 'r')
  vertices    = h5['geometry']['vertices']
  numVertices = vertices.shape[0]
  spaceDim    = vertices.shape[1]
  cells       = h5['topology']['cells']
  numCells    = cells.shape[0]
  numCorners  = cells.shape[1]
  cellDim     = h5['topology']['cells'].attrs['cell_dim']
  time        = np.array(h5['time']).flatten()
  vfields     = []
  cfields     = []
  if 'vertex_fields' in h5: vfields = h5['vertex_fields'].items()
  if 'cell_fields' in h5: cfields = h5['cell_fields'].items()

  # Write Xdmf
  Xdmf(xdmfFilename).write(hdfFilename, numCells, numCorners, cellDim, numVertices, spaceDim, time, vfields, cfields)
  h5.close()
  return

if __name__ == '__main__':
  generateXdmf(sys.argv[1])