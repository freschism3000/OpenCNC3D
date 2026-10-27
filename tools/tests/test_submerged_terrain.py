"""Calibrate actual-buffer gates against missing water, wrong depth and open banks."""
from pathlib import Path
import gzip,importlib.util,json,struct,tempfile,unittest
import numpy as np
spec=importlib.util.spec_from_file_location('bed_gate',Path(__file__).resolve().parents[1]/'gate-submerged-terrain.py')
G=importlib.util.module_from_spec(spec);spec.loader.exec_module(G)

class SubmergedTerrain(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.o=Path(self.tmp.name);self.h,self.w=24,32
        self.depth=np.full((self.h,self.w,1),.2,dtype='<f4')
        self.normal=np.empty((self.h,self.w,4),dtype='<f4');self.normal[:]=[.5,1,.5,1];self.normal[:,16:]=[.5,1,.5,.28]
        self.buffer('depth',self.depth);self.buffer('normals',self.normal)
        M=np.array([[4,0,0,4],[0,0,1,.6],[0,-4,0,4],[0,0,0,1]],float)
        (self.o/'capture-matrices.json').write_text(json.dumps({'inverse_vp':M.flatten(order='F').tolist()}))
        yy,xx=np.indices((32,32));F=np.zeros((32,32,4),dtype='<u2');F[...,0]=np.rint((((xx+.5)/4-4)/32+.5)*65535).astype('u2');F[...,3]=np.where(xx>=16,65535,0)
        (self.o/'field16.bin').write_bytes(b'C3DFLD16'+struct.pack('<5I',1,32,32,4,16)+F.tobytes())
        opaque=np.zeros((self.h,self.w,4),dtype='u1');opaque[:,:16,3]=255
        (self.o/'opaque.bin').write_bytes(b'C3DOPAQ1'+struct.pack('<3I',1,self.w,self.h)+opaque[::-1].tobytes())
        z,x=np.indices((65,65));x=x.ravel()/8;z=z.ravel()/8;dry=x<4
        self.bed=np.stack([x,z,np.where(dry,-.006,-3),np.zeros_like(x),np.zeros_like(x),np.where(dry,-1,4),np.zeros_like(x)],-1).astype('<f4');self.write_bed()
        (self.o/'run.log').write_text('fixture\n')
    def buffer(self,n,a):
        (self.o/f'capture-{n}.bin.gz').write_bytes(gzip.compress(struct.pack('<3I',self.w,self.h,1 if a.shape[-1]==1 else 2)+a[::-1].tobytes()))
    def write_bed(self):
        (self.o/'bed.bin').write_bytes(b'C3DBED1\0'+struct.pack('<I',len(self.bed))+self.bed.tobytes())
    def tearDown(self):self.tmp.cleanup()
    def test_flat_surface_and_closed_bank(self):self.assertGreater(G.check(self.o)['gpu_water_pixels'],100)
    def plane(self):
        corners=np.zeros((8,8,4),dtype='<f4');body=np.zeros((8,8),dtype='<i4')
        (self.o/'plane.bin').write_bytes(b'C3DPLN1\0'+struct.pack('<2I',8,8)+corners.tobytes()+body.tobytes())
    def test_authoritative_ray_plane_selection(self):
        self.plane();self.assertGreater(G.check(self.o)['gpu_water_pixels'],100)
    def test_ray_plane_still_detects_missing_surface(self):
        self.plane();self.normal[8:16,20:26,3]=1;self.buffer('normals',self.normal)
        with self.assertRaises(AssertionError):G.check(self.o)
    def test_ray_plane_still_detects_zero_normal_hole(self):
        self.plane();self.normal[8:16,20:26]=0;self.buffer('normals',self.normal)
        with self.assertRaises(AssertionError):G.check(self.o)
    def test_ray_plane_still_detects_wrong_depth(self):
        self.plane();self.depth[8:16,20:26,0]+=.03;self.buffer('depth',self.depth)
        with self.assertRaises(AssertionError):G.check(self.o)
    def test_missing_water_material_fails(self):
        self.normal[8:16,20:26,3]=1;self.buffer('normals',self.normal)
        with self.assertRaises(AssertionError):G.check(self.o)
    def test_wrong_water_depth_fails(self):
        self.depth[8:16,20:26,0]+=.03;self.buffer('depth',self.depth)
        with self.assertRaises(AssertionError):G.check(self.o)
    def test_open_bank_attachment_fails(self):
        self.bed[10,2]-=.02;self.write_bed()
        with self.assertRaises(AssertionError):G.check(self.o)
    def test_unlevel_basin_fails(self):
        self.bed[-1,2]+=.03;self.write_bed()
        with self.assertRaises(AssertionError):G.check(self.o)
if __name__=='__main__':unittest.main()
