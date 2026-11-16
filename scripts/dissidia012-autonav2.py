import subprocess, struct, os, re
IMG="/mnt/c/Users/Devin Prater/Dropbox/Games/PSP/Dissidia 012 - Duodecim Final Fantasy (USA) (En,Fr,De,Es,It).cso"
env=dict(os.environ); env["PPSSPP_ASSETS"]="/home/devin/oga-work/build-assets/ppsspp-assets"
p=subprocess.Popen(["/home/devin/drv",IMG,"/home/devin/fp-save"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,env=env,bufsize=1)
def send(l): p.stdin.write(l+"\n"); p.stdin.flush()
def blob():
    send("blob")
    while True:
        s=p.stdout.readline()
        if s.startswith("blob"):
            m=re.match(r"blob n=(-?\d+) x=(\d+) y=(\d+)",s)
            return (int(m.group(1)),int(m.group(2)),int(m.group(3)))
send("loadstate /home/devin/d12s/field2.state"); send("run 120")
best=(0,0,0); walk=0
for it in range(60):
    n,x,y=blob()
    if n>best[0]: best=(n,x,y)
    if it%4==0: print("it%02d n=%3d (%d,%d) best n=%d y=%d walked=%d"%(it,n,x,y,best[0],best[2],walk),flush=True)
    if n>=6 and 200<=x<=285:
        send("analog 0 -1 8"); send("run 8"); walk+=1
    elif n>=6 and x<200:
        send("analog -1 0 2"); send("run 8")
    elif n>=6 and x>285:
        send("analog 1 0 2"); send("run 8")
    else:
        send("analog 1 0 3"); send("run 8")
send("shot /home/devin/d12s/e1.ppm"); send("ram /home/devin/d12s/el0")
for i in range(4): send("press square"); send("run 25")
send("ram /home/devin/d12s/el1"); send("shot /home/devin/d12s/e2.ppm")
p.stdin.close(); p.wait(timeout=90)
print("best blob",best,"walks",walk)
