import os, sys
import paths
from ramix import RaMix
from bake_dossidebar import bake_font
D=paths.RADATA+'/'
class Src:
    def __init__(s,res): s.a=RaMix(D+res+'.MIX'); s.l=RaMix(D+'LOCAL.MIX')
    def read(s,n):
        v=s.a.read(n)
        return v if v is not None else s.l.read(n)
def load(res,stem):
    n,mw,mh,widths,ytop,rows,cells=bake_font(Src(res),stem)
    return dict(n=n,mw=mw,mh=mh,widths=list(widths),ytop=list(ytop),rows=list(rows),cells=cells)
if __name__=='__main__':
    for res in ('LORES','HIRES'):
        for st in ('GRAD6FNT','6POINT','8POINT','12METFNT','TYPE','SCOREFNT','HELP'):
            try:
                f=load(res,st)
            except Exception as e:
                print(res,st,'ERR',e); continue
            used=sorted(set(f['cells']))
            print(res,st,'n',f['n'],'max',f['mw'],f['mh'],'classes',used,'A width',f['widths'][65])
