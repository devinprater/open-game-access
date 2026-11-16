import subprocess, struct, os, re
IMG="/mnt/c/Users/Devin Prater/Dropbox/Games/PSP/Dissidia 012 - Duodecim Final Fantasy (USA) (En,Fr,De,Es,It).cso"
env=dict(os.environ); env["PPSSPP_ASSETS"]="/home/devin/oga-work/build-assets/ppsspp-assets"
p=subprocess.Popen(["/home/devin/drv",IMG,"/home/devin/fp-save"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,env=env,bufsize=1)
def send(l): p.stdin.write(l+"\n"); p.stdin.flush()
def pil():
    send("pillar")
    while True:
        s=p.stdout.readline()
        if s.startswith("pillar"):
            m=re.match(r"pillar run=(\d+) x=(\d+) top=(\d+)",s)
            return (int(m.group(1)),int(m.group(2)),int(m.group(3)))
send("loadstate /home/devin/d12s/field2.state"); send("run 120")
for it in range(24):
    run,x,top=pil()
    print("it%02d run=%3d x=%3d top=%d"%(it,run,x,top),flush=True)
    if run<8:
        send("analog 1 0 6"); send("run 12")        # search
    elif x<200:
        send("analog -1 0 3"); send("run 10")      # turn left toward it
    elif x>280:
        send("analog 1 0 3"); send("run 10")       # turn right toward it
    else:
        if run>60: print("CLOSE",flush=True); break
        send("analog 0 -1 18"); send("run 12")     # walk in
send("shot /home/devin/d12s/pil.ppm")
p.stdin.close(); p.wait(timeout=90)
