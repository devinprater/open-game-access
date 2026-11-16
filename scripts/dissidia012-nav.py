import subprocess, struct, os, math, sys
IMG="/mnt/c/Users/Devin Prater/Dropbox/Games/PSP/Dissidia 012 - Duodecim Final Fantasy (USA) (En,Fr,De,Es,It).cso"
env=dict(os.environ); env["PPSSPP_ASSETS"]="/home/devin/oga-work/build-assets/ppsspp-assets"
p=subprocess.Popen(["/home/devin/drv",IMG,"/home/devin/fp-save"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,env=env,bufsize=1)
def send(l): p.stdin.write(l+"\n"); p.stdin.flush()
def rd(a):
    send("rd %08x"%a)
    while True:
        s=p.stdout.readline()
        if s.startswith("%08x:"%a): return int(s.split(":")[1])
f=lambda v:struct.unpack("<f",struct.pack("<I",v))[0]
def pos():
    q=rd(0x8b83de4)
    return (f(rd(q+0x30)), f(rd(q+0x38)))
def push(x,y,n=12): send("analog %g %g %d"%(x,y,n))
def norm(v):
    m=math.hypot(*v); return (v[0]/m,v[1]/m) if m>1e-6 else (0,0)
def calib():
    a=pos(); push(0,-1,8); b=pos(); u=norm((b[0]-a[0],b[1]-a[1]))
    a=pos(); push(1,0,8);  b=pos(); r=norm((b[0]-a[0],b[1]-a[1]))
    return u,r
send("loadstate /home/devin/d12s/field2.state"); send("run 120")
tx,tz=float(sys.argv[1]),float(sys.argv[2])
print("start",[round(v,2) for v in pos()],"target",tx,tz,flush=True)
u,r=calib()
print("basis up=%s right=%s"%([round(v,2) for v in u],[round(v,2) for v in r]),flush=True)
for it in range(60):
    x,z=pos(); dx,dz=tx-x,tz-z; d=math.hypot(dx,dz)
    if d<3.0: print("ARRIVED after",it,"pos",[round(v,2) for v in (x,z)],flush=True); break
    t=norm((dx,dz))
    cands=[((0,-1),u),((0,1),(-u[0],-u[1])),((1,0),r),((-1,0),(-r[0],-r[1]))]
    best=max(cands,key=lambda c:c[1][0]*t[0]+c[1][1]*t[1])
    a=pos(); push(best[0][0],best[0][1],12); b=pos()
    moved=(b[0]-a[0],b[1]-a[1]); got=norm(moved)
    ok=got[0]*best[1][0]+got[1]*best[1][1]
    if it%8==0 or ok<0.6:
        u,r=calib()
    print("it%02d d=%.1f push=%s moved=%.2f align=%.2f"%(it,d,best[0],math.hypot(*moved),ok),flush=True)
else:
    print("GAVE UP pos",[round(v,2) for v in pos()],flush=True)
send("shot /home/devin/d12s/nav.ppm")
p.stdin.close(); p.wait(timeout=60)
