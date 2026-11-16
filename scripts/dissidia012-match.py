import struct,sys,re,collections
H='/home/devin/dis012/out/'
O='/mnt/c/Users/Devin Prater/oga-ghidra-dissidia/EBOOT.dec'
def segs(p):
    d=open(p,'rb').read()
    phoff=struct.unpack_from('<I',d,0x1c)[0]; n=struct.unpack_from('<H',d,0x2c)[0]
    out=[]
    for i in range(n):
        t,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',d,phoff+i*32)
        out.append((t,off,va,fs))
    return d,out
def mask(w):
    op=w>>26
    if op in (2,3): return (w&0xFC000000)          # j/jal target
    if op in (0x0F,0x09,0x0D,0x0C,0x08,0x0A,0x0B): return w&0xFFFF0000  # lui/addiu/ori/andi.. imm
    if op in (0x20,0x21,0x23,0x24,0x25,0x28,0x29,0x2B,0x31,0x39,0x22,0x26): return w&0xFFFF0000 # loads/stores offset
    if op in (4,5,6,7,1): return w&0xFFFF0000
    return w
def code(d,s):
    t,off,va,fs=s[0]
    return va,d[off:off+fs]
od,os_=segs(O); print('orig segs',[(hex(a),hex(b),hex(c),hex(e)) for a,b,c,e in os_])
nd,ns=segs(H+'EBOOT.dec'); print('012 segs',[(hex(a),hex(b),hex(c),hex(e)) for a,b,c,e in ns])
ova,oc=code(od,os_); nva,nc=code(nd,ns)
ow=struct.unpack('<%dI'%(len(oc)//4),oc[:len(oc)//4*4]); nw=struct.unpack('<%dI'%(len(nc)//4),nc[:len(nc)//4*4])
nm=[mask(w) for w in nw]
W=12
idx=collections.defaultdict(list)
for i in range(len(nm)-W):
    idx[tuple(nm[i:i+W])].append(i)
# overlay words
ovl={}
for f in ['OVL_BATTLE_APP.ELF','OVL_MENU_APP.ELF','OVL_EXTRA_APP.ELF']:
    d=open(H+f,'rb').read(); 
    t,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',d,struct.unpack_from('<I',d,0x1c)[0])
    w=struct.unpack('<%dI'%(fs//4),d[off:off+fs//4*4]); ovl[f]=(va,[mask(x) for x in w])
oidx={}
for f,(va,m) in ovl.items():
    ix=collections.defaultdict(list)
    for i in range(len(m)-W): ix[tuple(m[i:i+W])].append(i)
    oidx[f]=ix
# original functions of interest (ELF vaddr in ghidra = vaddr-0x08804000?) read function-index
funcs={'DP adjuster':0x1b6084,'menu sel validate':0x250538,'board ctor':0x1b8ef4,'avail-dir':0x1b8728,'board tick':0x1c218c,
'battle event disp':0x97778,'enemy lock':0xb64f8,'ex check':0xba0c8,'fighter ctor':0xbd3e4,'menu mgr ctor':0x2489d0,'pad ctrl read':0xf68e0,
'converted input sink':0xf6694,'xor decoder':0xfc7dc,'evex loader':0x1b4df0,'talk init':0x1c3654,'hud draw':0x1d5438,'big UI disp':0x1b97c8,
'input consumer':0x267cb8,'event poster':0x1accb4,'sfx poster':0x9b930,'heap alloc':0xe5710,'node lookup':0x25468c,'ex pickup':0xce824,'dispatcher':0x24932c,'commit id':0x1bf078,'rmfd parse':0x1c4944,'tile class':0x1becac}
# base: ghidra addr = va_seg0 + (addr - ?) ; Ghidra addrs in index are 'entry' like 001b6084 => maybe vaddr directly minus seg
for name,a in funcs.items():
    foff=None
    # orig segment 0 va
    off=a-0+ (os_[0][1]) if False else None
    # try treating index addr as file-relative-to-seg0 va offset from 0x08804000
    va0=os_[0][2]
    idxw=a//4
    if idxw<0 or idxw+W>len(ow): print(name,'out of range');continue
    key=tuple(mask(w) for w in ow[idxw:idxw+W])
    hits=[('EBOOT',nva+4*i) for i in idx.get(key,[])]
    for f,ix in oidx.items():
        hits+=[(f,ovl[f][0]+4*i) for i in ix.get(key,[])]
    print('%-22s orig+%06x -> %s'%(name,a,[(h[0],hex(h[1])) for h in hits[:3]] or 'NO MATCH'))
