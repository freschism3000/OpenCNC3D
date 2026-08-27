import sys
from PIL import Image, ImageChops
b=Image.open(sys.argv[1]).convert("RGB")
m=Image.open(sys.argv[2]).convert("RGB")
a=Image.open(sys.argv[3]).convert("RGB")
# the marker sits in a small window around the ordered cell; measure only there so the
# live scene outside it cannot mask or fake the result
box=(637,350,727,420)
def px(x,y):
    d=ImageChops.difference(x.crop(box),y.crop(box)).convert("L").point(lambda v:255 if v>18 else 0)
    return sum(1 for p in d.getdata() if p)
print("MARK|mid=%d|after=%d" % (px(b,m), px(b,a)))
