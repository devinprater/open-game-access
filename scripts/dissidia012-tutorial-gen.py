pre = """run 1500
press cross
run 300
press start
run 600
press cross
run 200
press cross
run 300
press cross
run 300
press start
run 300
press cross
run 300
press cross
run 300
press cross
run 300
press cross
run 300
press cross
run 600
press cross
run 600
press cross
run 900
press cross
run 900
press circle
run 900
press cross
run 1200
""".splitlines()
out = list(pre)
seq = ["square", "circle", "cross", "square", "circle", "cross", "cross", "triangle"]
for c in range(30):
    for i in range(16):
        out.append("press " + seq[i % len(seq)] + " 8")
    out.append("run 120")
    out.append("shot /home/devin/d12s/g%02d.ppm" % c)
open("/mnt/c/Users/Public/d12/g.cmds", "w").write("\n".join(out) + "\n")
print(len(out))
