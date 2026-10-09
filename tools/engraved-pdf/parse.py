import re

NUM = r'[-+]?[\d.]+'

def mul(a,b):
    return (a[0]*b[0]+a[1]*b[2], a[0]*b[1]+a[1]*b[3],
            a[2]*b[0]+a[3]*b[2], a[2]*b[1]+a[3]*b[3],
            a[4]*b[0]+a[5]*b[2]+b[4], a[4]*b[1]+a[5]*b[3]+b[5])

def apply(m,x,y):
    return (m[0]*x+m[2]*y+m[4], m[1]*x+m[3]*y+m[5])

def unescape(s):
    out=[]; i=0
    while i < len(s):
        c=s[i]
        if c=='\\':
            n=s[i+1]
            if n in 'nrtbf': out.append({'n':'\n','r':'\r','t':'\t','b':'\b','f':'\f'}[n]); i+=2
            elif n.isdigit():
                j=i+1; o=''
                while j<len(s) and s[j].isdigit() and len(o)<3: o+=s[j]; j+=1
                out.append(chr(int(o,8))); i=j
            else: out.append(n); i+=2
        else: out.append(c); i+=1
    return ''.join(out)

TOK = re.compile(r'\((?:\\.|[^()\\])*\)|\[(?:\((?:\\.|[^()\\])*\)|[^\]])*\]|/[^\s/\[\]<>()]+|'
                 + NUM + r'|[A-Za-z\'"*]+', re.S)

def parse(cs, fonts):
    """fonts: name -> (basefont, unimap, widths dict cid->w, default width)"""
    toks = TOK.findall(cs)
    ctm = (1,0,0,1,0,0); stack=[]
    tm = tlm = (1,0,0,1,0,0)
    font=None; size=1; lw=1; leading=0
    cur=None; start=None; glyphs=[]; lines=[]; fills=[]
    path=[]
    args=[]
    for t in toks:
        if re.fullmatch(NUM, t): args.append(float(t)); continue
        if t[0] in '/[(': args.append(t); continue
        op=t
        try:
            if op=='q': stack.append(ctm)
            elif op=='Q':
                if stack: ctm=stack.pop()
            elif op=='cm': ctm = mul(tuple(args[-6:]), ctm)
            elif op=='w': lw=args[-1]
            elif op=='m':
                cur=apply(ctm,args[-2],args[-1]); start=cur; path=[cur]
            elif op=='l':
                p=apply(ctm,args[-2],args[-1])
                if cur: lines.append((cur,p,lw*abs(ctm[0]))); path.append(p)
                cur=p
            elif op=='c':
                p=apply(ctm,args[-2],args[-1]); path.append(p); cur=p
            elif op=='h':
                if start: path.append(start); cur=start
            elif op=='re':
                x,y,w,h=args[-4:]
                pts=[apply(ctm,x,y),apply(ctm,x+w,y),apply(ctm,x+w,y+h),apply(ctm,x,y+h)]
                path=pts; cur=pts[0]
            elif op in ('f','f*','B','B*','b','b*'):
                if path: fills.append(path)
                path=[]
            elif op in ('S','s','n'):
                path=[]
            elif op=='Tf': font=args[-2][1:]; size=args[-1]
            elif op=='Tm': tm=tlm=tuple(args[-6:])
            elif op=='TL': leading=args[-1]
            elif op=='Td':
                tlm = mul((1,0,0,1,args[-2],args[-1]), tlm); tm=tlm
            elif op=='TD':
                leading=-args[-1]; tlm=mul((1,0,0,1,args[-2],args[-1]), tlm); tm=tlm
            elif op=='T*':
                tlm=mul((1,0,0,1,0,-leading), tlm); tm=tlm
            elif op in ('Tj','TJ',"'",'"'):
                if op in ("'",'"'):
                    tlm=mul((1,0,0,1,0,-leading), tlm); tm=tlm
                s=args[-1]
                parts=[]
                if s[0]=='[':
                    for m in re.finditer(r'\((?:\\.|[^()\\])*\)|'+NUM, s, re.S):
                        v=m.group(0)
                        parts.append(('s',unescape(v[1:-1])) if v[0]=='(' else ('k',float(v)))
                else:
                    parts.append(('s', unescape(s[1:-1])))
                base, uni, widths, dw = fonts.get(font, ('?',{},{},500))
                for kind,v in parts:
                    if kind=='k':
                        tm = mul((1,0,0,1, -v/1000.0*size, 0), tm)
                        continue
                    for k in range(0,len(v)-1,2):
                        cid=(ord(v[k])<<8)|ord(v[k+1])
                        full=mul(tm,ctm)
                        x,y=apply(full,0,0)
                        adv=widths.get(cid,dw)/1000.0*size*abs(full[0] or full[3])
                        glyphs.append(dict(font=base, ch=uni.get(cid,0), cid=cid,
                                           x=x, y=y, adv=adv,
                                           size=size*abs(full[3] or full[0])))
                        tm = mul((1,0,0,1, widths.get(cid,dw)/1000.0*size, 0), tm)
        except Exception:
            pass
        args=[]
    return glyphs, lines, fills
