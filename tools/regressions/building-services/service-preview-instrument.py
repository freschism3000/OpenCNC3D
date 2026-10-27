#!/usr/bin/env python3
"""Install/restore a clearly marked snapshot-only preview hook in a PRIVATE export.
This changes no engine state and is deliberately excluded from production delivery.
"""
from pathlib import Path
import argparse,hashlib
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--restore',action='store_true');a=p.parse_args();s=a.source.resolve()
if Path('/private/tmp') not in s.parents:raise SystemExit('Only private /private/tmp source exports may be instrumented.')
b=s.with_name('cnc_eyes.cpp.service-preview-original')
if a.restore:
 if not b.exists():raise SystemExit('No private preview backup.')
 s.write_bytes(b.read_bytes());print('restored',s);raise SystemExit(0)
x=s.read_text();needle='    } else if (!strcmp(cmd, "bservicedump")) {'
if x.count(needle)!=1 or 'PRIVATE SERVICE PREVIEW' in x:raise SystemExit('Expected exactly one uninjected production bservicedump hook.')
if b.exists():raise SystemExit('Restore or remove the old private backup first.')
b.write_bytes(s.read_bytes())
hook='''    } else if (!strcmp(cmd, "bservicepreview")) {
        /* PRIVATE SERVICE PREVIEW; never part of production runtime delivery. */
        char type[24]={0};int doing=1,stage=0,poweroff=0;
        if(sscanf(arg,"%23s %d %d %d",type,&doing,&stage,&poweroff)>=3){
            const int oi=find_object(type,0);
            if(oi>=0){SimObject& o=g_objects[oi];o.doing=doing;o.dostage=stage;o.poweroff=poweroff;
                const long long key=(long long)smooth_key(o);
                auto it=g_ebldBld.find(key);if(it!=g_ebldBld.end()){
                    it->second.serviceDoing=doing;it->second.serviceStage=stage;it->second.servicePowerOff=poweroff!=0;}
                printf("BSERVICE-PREVIEW|injected|%s|doing=%d|stage=%d|poweroff=%d\\n",type,doing,stage,poweroff);
            }else g_scriptFails++;
        }else g_scriptFails++;
'''
s.write_text(x.replace(needle,hook+needle));print('instrumented private source',s,'original SHA256',hashlib.sha256(b.read_bytes()).hexdigest())
