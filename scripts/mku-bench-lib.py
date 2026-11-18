import time,json,struct,base64,importlib.util
sp=importlib.util.spec_from_file_location("c","" + str(__import__("pathlib").Path(__file__).resolve().parent) + "/psp-ppsspp-client.py")
c=importlib.util.module_from_spec(sp);sp.loader.exec_module(c)
d=c.Debugger(timeout=8)
_tk=[0]
def rd(a,n=4):
    for _ in range(4):
        _tk[0]+=1; tk="b%d"%_tk[0]
        d.ws.send(json.dumps({"event":"memory.read","address":a,"size":n,"ticket":tk}))
        end=time.time()+3
        while time.time()<end:
            try: raw=d.ws.recv()
            except Exception: break
            if ("\"ticket\":\"%s\""%tk) not in raw: continue
            try: j=json.loads(raw)
            except Exception: break
            if "base64" in j:
                b=base64.b64decode(j["base64"])
                if len(b)==n: return b
            break
        time.sleep(0.05)
    return b""
def u32(a):
    r=rd(a,4); return struct.unpack("<I",r)[0] if r else None
def f32(a):
    r=rd(a,4); return struct.unpack("<f",r)[0] if r else None
def btn(b,dn): d.ws.send(json.dumps({"event":"input.buttons.send","buttons":{b:dn}}))
def tap(b,dw=0.12,up=0.3): btn(b,True);time.sleep(dw);btn(b,False);time.sleep(up)
