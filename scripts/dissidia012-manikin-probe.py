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
def pose():
    c=rd(0x089d83d8); pl=rd(c+0x6d0)
    return c,(f(rd(pl+0x30)),f(rd(pl+0x38)))
send("loadstate /home/devin/d12s/encounter.state"); send("run 60")
c,_=pose(); print("state at load",rd(c+0x68),flush=True)
for k in range(3):
    send("press cross"); send("run 25")
    c,pos=pose(); print("after cross%d state=%d pos=(%.1f,%.1f)"%(k,rd(c+0x68),pos[0],pos[1]),flush=True)
    if rd(c+0x68)==2: break
for i in range(16):
    c,pos=pose(); st=rd(c+0x68)
    print("walk%02d pos=(%.1f,%.1f) state=%d"%(i,pos[0],pos[1],st),flush=True)
    if st!=2:
        send("savestate /home/devin/d12s/battle.state"); send("ram /home/devin/d12s/bat"); send("shot /home/devin/d12s/bat.ppm")
        send("run 150"); send("shot /home/devin/d12s/bat2.ppm"); print("CAPTURED",st,flush=True); break
    send("analog 0 -1 16"); send("run 10")
p.stdin.close(); p.wait(timeout=60)
