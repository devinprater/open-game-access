// drv: boot a PSP image headless and run a command script. Commands on stdin lines:
//  run N | press NAME [frames] | ram FILE | shot FILE.ppm | rd HEXADDR
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "psp_core.h"
static PspCore *c;
static void run(int n){ for(int i=0;i<n;i++) psp_frame(c); }
int main(int argc,char**argv){
  c=psp_create(); if(getenv("CLASSIC")) psp_set_classic_interpreter(c); char code[16]={0};
  if(!psp_load_rom(c,argv[1],argv[2],code)){fprintf(stderr,"load fail\n");return 1;}
  printf("gameid=%s\n",code); if(!psp_start(c))return 1;
  const char*names[]={"select","start","up","right","down","left","triangle","circle","cross","square","l","r"};
  char line[512];
  while(fgets(line,sizeof line,stdin)){
    char a[64]={0},b[256]={0}; int n=0; sscanf(line,"%63s %255s %d",a,b,&n);
    if(!strcmp(a,"run")) run(atoi(b));
    else if(!strcmp(a,"press")){ int k=-1; for(int i=0;i<12;i++) if(!strcmp(b,names[i])) k=i; if(k<0)continue; if(!n)n=6;
      psp_set_button(c,k,true); run(n); psp_set_button(c,k,false); run(30); }
    else if(!strcmp(a,"ram")){ FILE*f=fopen(b,"wb"); for(uint32_t ad=0x08800000;ad<0x0A000000;ad+=4){uint32_t v=psp_debug_read(c,ad,4);fwrite(&v,4,1,f);} fclose(f); printf("ram %s\n",b);}
    else if(!strcmp(a,"shot")){ int w,h; if(psp_framebuffer(c,&w,&h)){ FILE*f=fopen(b,"wb"); fprintf(f,"P6\n%d %d\n255\n",w,h); const uint8_t*p=psp_framebuffer_ptr(c); for(int i=0;i<w*h;i++){fwrite(p+i*4,1,3,f);} fclose(f); printf("shot %s %dx%d\n",b,w,h);} }
    else if(!strcmp(a,"savestate")){ printf("savestate %d\n",(int)psp_save_state(c,b)); }
    else if(!strcmp(a,"loadstate")){ printf("loadstate %d\n",(int)psp_load_state(c,b)); }
    else if(!strcmp(a,"analog")){ float x=0,y=0; sscanf(line,"%*s %f %f %d",&x,&y,&n); if(!n)n=60; psp_set_analog(c,x,y); run(n); psp_set_analog(c,0,0); run(20); }
    else if(!strcmp(a,"watch")){ uint32_t s0=strtoul(b,0,16); uint32_t e0=0; char t[64]; sscanf(line,"%*s %*s %63s",t); e0=strtoul(t,0,16); if(getenv("WLOG")) psp_watch_read_log(c,s0,e0); else psp_watch_read(c,s0,e0); printf("watch %08x %08x\n",s0,e0); }
    else if(!strcmp(a,"poll")){ uint32_t s0=strtoul(b,0,16),e0=0,h=0,pc=0,ad=0; int sz=0; char t[64]; sscanf(line,"%*s %*s %63s",t); e0=strtoul(t,0,16); psp_watch_poll(c,s0,e0,&h,&pc,&ad,&sz); printf("poll hits=%u pc=%08x addr=%08x size=%d\n",h,pc,ad,sz); }
    else if(!strcmp(a,"bright")){ int w=0,h=0; if(psp_framebuffer(c,&w,&h)){ const unsigned char*fb=psp_framebuffer_ptr(c); long n=0,sx=0,sy=0; int x0=w,y0=h,x1=-1,y1=-1;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){ const unsigned char*p=fb+(y*w+x)*4; int lum=p[0]+p[1]+p[2];
          if(lum>720){ n++; sx+=x; sy+=y; if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; } }
        if(n>0) printf("bright n=%ld cx=%ld cy=%ld box=%d,%d,%d,%d\n",n,sx/n,sy/n,x0,y0,x1,y1);
        else printf("bright n=0\n"); } else printf("bright n=0\n"); }
    else if(!strcmp(a,"blob")){ int w=0,h=0; if(psp_framebuffer(c,&w,&h)){ const unsigned char*fb=psp_framebuffer_ptr(c); int TW=12,best=-1,bx=0,by=0;
        for(int ty=40; ty+TW<h-48; ty+=TW) for(int tx=56; tx+TW<w-20; tx+=TW){
          if(tx+TW>168 && tx<296 && ty+TW>140) continue;   /* mask the player, centre-bottom */
          int cnt=0;
          for(int y=ty;y<ty+TW;y++) for(int x=tx;x<tx+TW;x++){ const unsigned char*q=fb+(y*w+x)*4; if((int)q[0]+q[1]+q[2]>720) cnt++; }
          if(cnt>best){best=cnt;bx=tx;by=ty;} }
        printf("blob n=%d x=%d y=%d\n",best,bx+TW/2,by+TW/2); } else printf("blob n=-1 x=0 y=0\n"); }
    else if(!strcmp(a,"pillar")){ int w=0,h=0; if(psp_framebuffer(c,&w,&h)){ const unsigned char*fb=psp_framebuffer_ptr(c);
        int bestrun=0,bx=0,btop=0;
        for(int x=20;x<w-20;x++){ int run=0,top=0;
          for(int y=30;y<h-50;y++){ const unsigned char*q=fb+(y*w+x)*4;
            if((int)q[0]+q[1]+q[2]>700){ if(run==0) top=y; run++; if(run>bestrun){bestrun=run;bx=x;btop=top;} }
            else run=0; } }
        printf("pillar run=%d x=%d top=%d\n",bestrun,bx,btop); } else printf("pillar run=0 x=0 top=0\n"); }
    else if(!strcmp(a,"rd")){ uint32_t ad=strtoul(b,0,16); printf("%08x: %u\n",ad,psp_debug_read(c,ad,4)); }
    fflush(stdout);
  }
  psp_stop(c); psp_destroy(c); return 0; }
