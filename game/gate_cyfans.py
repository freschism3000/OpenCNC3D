import sys
from PIL import Image, ImageChops
a=Image.open(sys.argv[1]).convert("RGB"); b=Image.open(sys.argv[2]).convert("RGB")
box=(560,300,760,440)   # the FACT fan region at cam 53,51 zoom max
d=ImageChops.difference(a.crop(box),b.crop(box)).convert("L").point(lambda v:255 if v>16 else 0)
print("FANS|%d" % sum(1 for p in d.getdata() if p))
