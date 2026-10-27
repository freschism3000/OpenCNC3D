"""Lift the RA3D lettering off the title: inside the screen circle the black field and its
grid; over the bezel ring the ring itself, rotated in from just below the wing about the
circle's centre so its texture runs on."""
import numpy as np, cv2, math, sys, os
import paths
from PIL import Image, ImageDraw
POLY=[(221,13),(406,13),(412,40),(430,41),(517,55),(520,63),(508,84),(503,121),(375,141),(321,151),(266,141),(138,121),(129,85),(127,63),(131,55),(208,41),(221,40)]
def clean(src, k, out):
    im=np.array(Image.open(src).convert('RGB')); H,W,_=im.shape
    poly=[(x*k,y*k) for x,y in POLY]
    mask=Image.new('L',(W,H),0); ImageDraw.Draw(mask).polygon(poly,fill=255); mk=np.array(mask)>0
    mk=cv2.dilate(mk.astype(np.uint8),np.ones((5,5),np.uint8)).astype(bool)
    cx,cy,r=332.7*k,169.2*k,195.6*k
    yy,xx=np.mgrid[0:H,0:W]; d=np.hypot(xx-cx,yy-cy); inside=d<r-2*k
    # a mask pixel the 1996 art shows as bare bezel steel above the lettering is bezel too
    inside&=~((yy<40*k)&(im.max(-1)>70)&(np.abs(im[...,0].astype(int)-im[...,2].astype(int))<30)&(xx<226*k))
    over=mk&~inside
    res=im.copy(); res[mk&inside]=0
    red=(im[...,0]>40)&(im[...,1]<25)&(im[...,2]<25)
    redish=(im[...,0].astype(int)>im[...,1].astype(int)+12)&(im[...,0]>18)
    cols=[x for x in range(int(190*k),int(450*k)) if red[0:int(12*k),x].mean()>0.6]
    for x in cols: sel=mk[:,x]&inside[:,x]; res[sel,x]=(72,2,0)
    row=int(round(42*k)); sel=mk[row]&inside[row]; res[row,sel]=(72,2,0)
    ys,xs=np.where(over)
    for y,x in zip(ys,xs):
        a=math.atan2(y-cy,x-cx)
        for step in range(1,40):
            t=math.radians(2*step)*(-1 if x<cx else 1)   # left wing: rotate down (ccw on screen)
            sx=int(round(cx+(x-cx)*math.cos(t)-(y-cy)*math.sin(t))); sy=int(round(cy+(x-cx)*math.sin(t)+(y-cy)*math.cos(t)))
            if 0<=sx<W and 0<=sy<H and not mk[sy,sx] and not redish[sy,sx] and d[sy,sx]>r-1*k:
                res[y,x]=im[sy,sx]; break
    Image.fromarray(res).save(out)
if __name__=='__main__':
    art = os.path.join(paths.ROOT, 'menu', 'art')
    clean(os.path.join(art, 'ra3d-title-win95.png'), 1, os.path.join(paths.WORK, 'ra_title_backdrop_clean.png'))
    clean(os.path.join(art, 'ra3d-title.png'), 0.5, os.path.join(paths.WORK, 'ra_title_backdrop_clean_lo.png'))
