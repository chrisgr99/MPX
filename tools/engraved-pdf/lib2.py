import re, lib, parse

def fonts_for(formn):
    fres=int(re.search(rb'/Font (\d+) 0 R', lib.objs[formn]).group(1))
    out={}
    for name,fn in re.findall(rb'/(R\d+)\s+(\d+) 0 R', lib.objs[fres]):
        fn=int(fn)
        base=re.search(rb'/BaseFont /(\S+)', lib.objs[fn]).group(1).decode().split('+')[-1]
        tun=int(re.search(rb'/ToUnicode (\d+) 0 R', lib.objs[fn]).group(1))
        uni=lib.parse_tounicode(tun)
        df=int(re.search(rb'/DescendantFonts \[\s*(\d+) 0 R', lib.objs[fn], re.S).group(1))
        dw=re.search(rb'/DW (\d+)', lib.objs[df]); dw=int(dw.group(1)) if dw else 500
        widths={}
        wm=re.search(rb'/W \[(.*?)\]\s*\n>>', lib.objs[df], re.S) or re.search(rb'/W \[(.*)\]', lib.objs[df], re.S)
        if wm:
            nums=re.findall(rb'[\d.]+|\[|\]', wm.group(1))
            i=0
            while i < len(nums):
                if nums[i]==b'[': i+=1; continue
                start=int(float(nums[i])); i+=1
                if i<len(nums) and nums[i]==b'[':
                    i+=1; k=start
                    while i<len(nums) and nums[i] not in (b'[',b']'):
                        widths[k]=float(nums[i]); k+=1; i+=1
                    if i<len(nums) and nums[i]==b']': i+=1
        out[name.decode()]=(base,uni,widths,dw)
    return out

def page(pg):
    po=dict(lib.page_objs())[pg]; f=lib.page_form(po)
    fonts=fonts_for(f)
    return parse.parse(lib.stream_of(f).decode('latin-1'), fonts)
