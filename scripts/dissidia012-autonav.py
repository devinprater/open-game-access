import subprocess, struct, os, re
IMG="/mnt/c/Users/Devin Prater/Dropbox/Games/PSP/Dissidia 012 - Duodecim Final Fantasy (USA) (En,Fr,De,Es,It).cso"
env=dict(os.environ); env["PPSSPP_ASSETS"]="/home/devin/oga-work/build-assets/ppsspp-assets"
p=subprocess.Popen(["/home/devin/drv",IMG,"/home/devin/fp-save"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,env=env,bufsize=1)
def send(l): p.stdin.write(l+"\n"); p.stdin.flush()
def bright():
    send("bright")
    while True:
        s=p.stdout.readline()
        if s.startswith("bright"):
            m=re.match(r"bright n=(\d+)(?: cx=(\d+) cy=(\d+) box=(\d+),(\d+),(\d+),(\d+))?",s)
            if not m or int(m.group(1))==0: return (0,0,0,0,0)
            return (int(m.group(1)),int(m.group(2)),int(m.group(3)),int(m.group(6))-int(m.group(4)),int(m.group(7))-int(m.group(5)))
send("loadstate /home/devin/d12s/field2.state"); send("run 120")
best=None
for it in range(26):
    n,cx,cy,bw,bh=bright()
    cand = 4<=n<=250 and bw<70 and bh<90 and cy>95
    print("it%02d n=%4d cx=%3d cy=%3d box=%dx%d %s"%(it,n,cx,cy,bw,bh,"CAND" if cand else ""),flush=True)
    if cand:
        if n>120: print("CLOSE",flush=True); break
        if cx<205: send("analog -1 0 5"); send("run 15")
        elif cx>275: send("analog 1 0 5"); send("run 15")
        else: send("analog 0 -1 12"); send("run 15")
    else:
        send("analog 1 0 5"); send("run 15")
send("shot /home/devin/d12s/c1.ppm"); send("ram /home/devin/d12s/cl0")
for i in range(3): send("press square"); send("run 25")
send("ram /home/devin/d12s/cl1"); send("shot /home/devin/d12s/c2.ppm")
p.stdin.close(); p.wait(timeout=90)
