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
    c=rd(0x089d83d8); pl=rd(c+0x6d0)
    return (f(rd(pl+0x30)),f(rd(pl+0x38)))
def state(): return rd(rd(0x089d83d8)+0x68)
def fwd():
    a=pos(); send("analog 0 -1 6"); send("run 8"); b=pos()
    dx,dz=b[0]-a[0],b[1]-a[1]; m=math.hypot(dx,dz)
    return (dx/m,dz/m) if m>1e-6 else None
def turn_frames(deg):
    n=int(round(abs(deg)/3.0))
    return max(2,min(34,n))
send("loadstate /home/devin/d12s/field2.state"); send("run 120")
TX,TZ=float(sys.argv[1]),float(sys.argv[2])
print("start",[round(v,1) for v in pos()],"target",TX,TZ,flush=True)
for it in range(50):
    x,z=pos(); dx,dz=TX-x,TZ-z; d=math.hypot(dx,dz)
    if it%5==0: print("it%02d d=%.1f pos=(%.1f,%.1f) state=%d"%(it,d,x,z,state()),flush=True)
    if d<2.5: print("ARRIVED",[round(v,1) for v in pos()],flush=True); break
    if state()!=2: print("STATE",state(),flush=True); break
    u=fwd()
    if not u: continue
    t=(dx/d,dz/d)
    ang=math.atan2(u[0]*t[1]-u[1]*t[0], u[0]*t[0]+u[1]*t[1])   # signed
    if abs(ang)>0.10:
        n=turn_frames(math.degrees(abs(ang)))
        send("press %s %d"%("r" if ang>0 else "l",n)); send("run 10")
    else:
        send("analog 0 -1 22"); send("run 10")
print("end",[round(v,1) for v in pos()],flush=True)
send("shot /home/devin/d12s/nav3.ppm")
p.stdin.close(); p.wait(timeout=90)
