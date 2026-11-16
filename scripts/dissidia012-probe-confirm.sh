cd $HOME/oga-work; export PPSSPP_ASSETS=$PWD/build-assets/ppsspp-assets
IMG="/mnt/c/Users/Devin Prater/Dropbox/Games/PSP/Dissidia 012 - Duodecim Final Fantasy (USA) (En,Fr,De,Es,It).cso"
S=$HOME/d12s
( echo run 1500; echo press cross; echo run 300; echo press start; echo run 600
  echo press cross; echo run 200; echo press cross; echo run 300; echo press cross; echo run 300
  echo press start; echo run 300
  echo press cross; echo run 300; echo press cross; echo run 300; echo press cross; echo run 300; echo press cross; echo run 300
  echo ram $S/y0; echo press right; echo ram $S/y1; echo press left; echo ram $S/y2; echo press right; echo ram $S/y3; echo press left; echo ram $S/y4
  echo press right; echo shot $S/yn.ppm
) | timeout 590 $HOME/drv "$IMG" $HOME/fp-save 2>&1 | grep -cE "^ram"
python3 - <<'PY'
import array
R=[array.array("I",open("/home/devin/d12s/y%d"%i,"rb").read()) for i in range(5)]
hits=[i for i in range(len(R[0])) if [R[k][i] for k in range(5)] in ([0,1,0,1,0],[1,0,1,0,1])]
print(len(hits),[hex(0x08800000+i*4) for i in hits[:40]])
PY
ffmpeg -loglevel error -y -i $S/yn.ppm /mnt/c/Users/Public/d12/s_yn.png
