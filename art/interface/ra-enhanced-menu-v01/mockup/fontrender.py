from PIL import Image
import rafont
_cache={}
def font(res,stem):
    k=(res,stem)
    if k not in _cache: _cache[k]=rafont.load(res,stem)
    return _cache[k]
def width(s,res='LORES',stem='GRAD6FNT',scale=2,xsp=None):
    f=font(res,stem)
    if xsp is None: xsp=-1 if stem=='GRAD6FNT' else 0
    w=0
    for ch in s:
        c=ord(ch)
        if c>=f['n']: continue
        w+=max(0,f['widths'][c]+xsp)
    return w*scale
def draw(im,s,x,y,col,res='LORES',stem='GRAD6FNT',scale=2,shadow=(0,0,0),grad=None,xsp=None,center=False):
    """col: body colour (classes 4..15 flat) or grad: list of 16 colours by class."""
    f=font(res,stem)
    if xsp is None: xsp=-1 if stem=='GRAD6FNT' else 0
    if center: x-=width(s,res,stem,scale,xsp)//2
    px=im.load(); W,H=im.size
    for ch in s:
        c=ord(ch)
        if c>=f['n']: continue
        mw,mh=f['mw'],f['mh']; base=c*mw*mh
        for yy in range(mh):
            for xx in range(mw):
                v=f['cells'][base+yy*mw+xx]
                if not v: continue
                if grad: colr=grad[v]
                elif v<4: colr=shadow
                else: colr=col
                if colr is None: continue
                for sy in range(scale):
                    for sx in range(scale):
                        X,Y=x+xx*scale+sx,y+(yy)*scale+sy
                        if 0<=X<W and 0<=Y<H:
                            if len(colr)==4:
                                a=colr[3]/255; o=px[X,Y]; px[X,Y]=tuple(int(o[i]*(1-a)+colr[i]*a) for i in range(3))+((255,) if len(o)==4 else ())
                            else: px[X,Y]=colr+((255,) if im.mode=='RGBA' else ())
        x+=max(0,f['widths'][c]+xsp)*scale
    return x
