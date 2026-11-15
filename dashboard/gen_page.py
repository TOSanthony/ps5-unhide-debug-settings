#!/usr/bin/env python3
"""Regenere dashboard_page.h depuis dashboard.html (python3 gen_page.py)."""
import os
d=os.path.dirname(os.path.abspath(__file__))
s=open(os.path.join(d,'dashboard.html'),encoding='utf-8').read()
out=['/* Genere par dashboard/gen_page.py depuis dashboard.html : ne pas modifier a la main. */','static const char PAGE[] =']
for ln in s.split('\n'):
    out.append('    "'+ln.replace('\\','\\\\').replace('"','\\"')+'\\n"')
out[-1]+=';'
open(os.path.join(d,'dashboard_page.h'),'w',encoding='utf-8').write('\n'.join(out)+'\n')
