import re, json

import os
QDF = os.environ.get('MPX_QDF', 'qdf.pdf')
d = open(QDF, 'rb').read()
objs = {}
for m in re.finditer(rb'\n(\d+) 0 obj\n(.*?)\nendobj', d, re.S):
    objs[int(m.group(1))] = m.group(2)

def stream_of(n):
    b = objs[n]; s = b.find(b'stream\n')
    if s < 0: return None
    return b[s+7:b.rfind(b'endstream')]

def parse_tounicode(n):
    cs = stream_of(n).decode('latin-1'); mp = {}
    for blk in re.findall(r'beginbfchar(.*?)endbfchar', cs, re.S):
        for a,b in re.findall(r'<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>', blk):
            mp[int(a,16)] = int(b[:4],16)
    for blk in re.findall(r'beginbfrange(.*?)endbfrange', cs, re.S):
        for a,b,c in re.findall(r'<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>', blk):
            lo,hi,dst = int(a,16),int(b,16),int(c[:4],16)
            for i in range(lo,hi+1): mp[i] = dst+(i-lo)
    return mp

def page_objs():
    out = []
    for m in re.finditer(rb'%% Page (\d+)\n%% Original object ID: \d+ 0\n(\d+) 0 obj\n', d):
        out.append((int(m.group(1)), int(m.group(2))))
    return out

def page_form(pobj):
    body = objs[pobj]
    cont = int(re.search(rb'/Contents (\d+) 0 R', body).group(1))
    resn = int(re.search(rb'/Resources (\d+) 0 R', body).group(1))
    cs = stream_of(cont).decode('latin-1')
    name = re.search(r'/(TPL\d+) Do', cs).group(1)
    xo = re.search(rb'/XObject <<(.*?)>>', objs[resn], re.S).group(1)
    formn = int(re.search((r'/%s\s+(\d+) 0 R'%name).encode(), xo).group(1))
    return formn

def form_fonts(formn):
    fres = int(re.search(rb'/Font (\d+) 0 R', objs[formn]).group(1))
    fonts = dict((k.decode(), int(v)) for k,v in re.findall(rb'/(R\d+)\s+(\d+) 0 R', objs[fres]))
    fi = {}
    for name, fn in fonts.items():
        base = re.search(rb'/BaseFont /(\S+)', objs[fn]).group(1).decode().split('+')[-1]
        tun = int(re.search(rb'/ToUnicode (\d+) 0 R', objs[fn]).group(1))
        fi[name] = (base, parse_tounicode(tun))
    return fi
