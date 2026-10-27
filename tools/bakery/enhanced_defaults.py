#!/usr/bin/env python3
"""Compile an exact F5 save into shared Enhanced visual defaults.

Display and application-state preferences remain per installation. Enhanced mode
rows override their live copies; Classic/N64 saved rows are never imported.
Unknown optional keys stay in the source snapshot and apply if later supported.
"""
import argparse, hashlib
from pathlib import Path

LOCAL = {'enabled','dos_classic','smooth_anim','show_stats','vsync','display_mode','res_w','res_h'}
MARKERS = {'enh_textures','enh_tree_toggle','enh_world_detail'}

def values(path):
    out = {}
    for line in path.read_text().splitlines():
        parts = line.split('#',1)[0].split()
        if len(parts) == 2:
            out[parts[0]] = float(parts[1])
    return out

def generate(source, output, root):
    raw = values(source)
    shared = {k:v for k,v in raw.items() if '.' not in k and k not in LOCAL | MARKERS}
    for k,v in raw.items():
        if k.startswith('mode.enhanced.') and k[14:] not in LOCAL:
            shared[k[14:]] = v
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    label = source.relative_to(root)
    lines = [f'/* Shared Enhanced visual defaults from {label}.',f'   Source SHA-256: {digest}',
             '   Display/application state stays user-local. Enhanced mode rows win;',
             '   saved Classic/N64 rows are not imported. Optional unknown keys are safe. */',
             '{','    static const struct { const char* key; float value; } preset[] = {']
    lines += [f'        {{"{k}",{v:.5f}f}},' for k,v in shared.items()]
    lines += ['    };','    for (unsigned i=0;i<sizeof preset/sizeof preset[0];++i)',
              '        for (const FxParam* p=FX_PARAMS;p->kind!=FXP_END;++p)',
              '            if (p->kind!=FXP_GROUP && !strcmp(p->key,preset[i].key)) {',
              '                if (p->kind==FXP_BOOL) *fx_iptr(s,p)=preset[i].value!=0;',
              '                else *fx_fptr(s,p)=preset[i].value;',
              '                break;','            }','}','']
    output.write_text('\n'.join(lines))
    print(f'{len(shared)} shared visual values; source {digest}')

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();generate(a.source.resolve(),a.output.resolve(),Path(__file__).resolve().parents[2])
