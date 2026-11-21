import subprocess, struct, os, math
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
def aim(tx,tz):
    for _ in range(8):
        x,z=pos(); d=math.hypot(tx-x,tz-z)
        if d<0.2: return
        u=fwd()
        if not u: return
        t=((tx-x)/d,(tz-z)/d)
        ang=math.atan2(u[0]*t[1]-u[1]*t[0],u[0]*t[0]+u[1]*t[1])
        if abs(ang)<0.12: return
        send("press %s %d"%("r" if ang>0 else "l",max(2,min(34,int(round(math.degrees(abs(ang))/3.0)))))); send("run 10")
send("loadstate /home/devin/d12s/field2.state"); send("run 120")
for step in ((95,16),(105,17),(118,27),(128,36)):
    for k in range(10):
        x,z=pos(); d=math.hypot(step[0]-x,step[1]-z)
        if d<4: break
        aim(*step); send("analog 0 -1 16"); send("run 8")
print("staging",[round(v,1) for v in pos()],flush=True)
TX,TZ=152.0,54.0
for it in range(30):
    x0,z0=pos(); d0=math.hypot(TX-x0,TZ-z0)
    if d0<3: print("ARRIVED",[round(v,1) for v in pos()],flush=True); break
    aim(TX,TZ); send("analog 0 -1 16"); send("run 8")
    x1,z1=pos(); gain=d0-math.hypot(TX-x1,TZ-z1)
    if gain<0.6:
        aim(TX,TZ); send("press cross"); send("run 20")
    print("it%02d d=%.1f pos=(%.1f,%.1f) gain=%.1f state=%d"%(it,math.hypot(TX-x1,TZ-z1),x1,z1,gain,state()),flush=True)
    if state()!=2: print("STATE CHANGE",state(),flush=True); break
send("shot /home/devin/d12s/portal_go.ppm"); send("ram /home/devin/d12s/portal_go")
for nm in ("cross","circle","square"):
    send("press %s"%nm); send("run 25")
    print("  %s -> state=%d pos=%s"%(nm,state(),[round(v,1) for v in pos()]),flush=True)
send("shot /home/devin/d12s/portal_go2.ppm")
p.stdin.close(); p.wait(timeout=90)
