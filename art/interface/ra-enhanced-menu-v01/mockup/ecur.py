"""Rasterise the approved CUR00 pointer the way menu/enhanced_menu_cursor.h draws it."""
import struct, numpy as np
from PIL import Image
def load(path):
    b=open(path,'rb').read(); p=0
    def rd(n):
        nonlocal p; v=b[p:p+n]; p+=n; return v
    assert rd(4)==b'ECUR'; ver,nt,nm=struct.unpack('<III',rd(12))
    tex=[]
    for i in range(nt):
        w,h=struct.unpack('<HH',rd(4)); tex.append(np.frombuffer(rd(w*h*4),np.uint8).reshape(h,w,4).astype(np.float32)/255)
    mesh=[]
    for i in range(nm):
        name=rd(16).split(b'\0')[0]; (count,)=struct.unpack('<I',rd(4))
        for j in range(count):
            t,mode,part=struct.unpack('<hBB',rd(4)); vs=[]
            for k in range(3):
                x,y,z,u,v=struct.unpack('<5f',rd(20)); r,g,bb,a=rd(4); vs.append((x,y,z,u,v,r/255,g/255,bb/255,a/255))
            if name==b'CUR00': mesh.append((t,mode,vs))
    return tex,mesh
def draw(img,x0,y0,scale,tex,mesh):
    """img: float32 HxWx3 array (0..1) composited in place."""
    unit=40.0*scale/(770.949*.819152)
    H,W,_=img.shape
    for t,mode,vs in mesh:
        P=[(x0+(v[0]-16.9984)*unit, y0+((v[2]-7.00416)*.819152-(v[1]-88.4019)*.573576)*unit) for v in vs]
        xs=[q[0] for q in P]; ys=[q[1] for q in P]
        minx,maxx=max(0,int(np.floor(min(xs)))),min(W-1,int(np.ceil(max(xs)))); miny,maxy=max(0,int(np.floor(min(ys)))),min(H-1,int(np.ceil(max(ys))))
        if minx>maxx or miny>maxy: continue
        (ax,ay),(bx,by),(cx,cy)=P
        den=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy)
        if abs(den)<1e-9: continue
        yy,xx=np.mgrid[miny:maxy+1,minx:maxx+1]; px=xx+.5; py=yy+.5
        l0=((by-cy)*(px-cx)+(cx-bx)*(py-cy))/den; l1=((cy-ay)*(px-cx)+(ax-cx)*(py-cy))/den; l2=1-l0-l1
        m=(l0>=0)&(l1>=0)&(l2>=0)
        if not m.any(): continue
        A=np.array(vs)
        attr=lambda i: l0*A[0,i]+l1*A[1,i]+l2*A[2,i]
        col=np.stack([attr(5),attr(6),attr(7)],-1); al=attr(8)
        if t>=0:
            T=tex[t]; th,tw,_=T.shape
            u=np.clip((attr(3)*tw).astype(int),0,tw-1); v=np.clip((attr(4)*th).astype(int),0,th-1)
            s=T[v,u]; col=col*s[...,:3]; al=al*s[...,3]
        dst=img[miny:maxy+1,minx:maxx+1]
        if mode==3:
            dst[m]=np.clip(dst[m]+col[m]*1.0,0,1)
        else:
            a=al[m][:,None]; dst[m]=dst[m]*(1-a)+col[m]*a
