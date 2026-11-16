// Host test for the 012 adapter: synthetic RAM from the measured layout.
#include "adapter.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace oga { extern const Adapter kDissidia012; }
namespace oga {
static bool NoAttach(const Host*){return false;} static void NoFrame(){} static void NoCmd(Command){} static bool NoReady(){return false;} static void NoDetach(){}
extern const Adapter kFireEmblemShadowDragon, kGameBoyAdvance, kDragonBallZSaiyans;
const Adapter kFireEmblemShadowDragon={"fe11","stub","YFEE",NoAttach,NoFrame,NoCmd,NoReady,NoDetach};
const Adapter kGameBoyAdvance={"gba","stub","AGB",NoAttach,NoFrame,NoCmd,NoReady,NoDetach};
const Adapter kDragonBallZSaiyans={"dbz","stub","YDBE",NoAttach,NoFrame,NoCmd,NoReady,NoDetach};
}
static std::map<uint32_t,uint32_t> ram; static std::vector<std::string> said; static int fails=0, n=0;
#define CHECK(c,m) do{n++; if(!(c)){fails++; printf("FAIL: %s\n",m);}}while(0)
static uint32_t r32(void*,uint32_t a){auto i=ram.find(a);return i==ram.end()?0:i->second;}
static uint16_t r16(void*,uint32_t a){return r32(0,a)&0xffff;} static uint8_t r8(void*,uint32_t a){return r32(0,a)&0xff;}
static void spk(void*,const char*s,bool){said.push_back(s);} static void lg(void*,const char*){}
static void setfp(){ ram[0x08871e28]=1007028381u; ram[0x08871e2c]=3301742380u; ram[0x088a9dec]=2946826240u; ram[0x088a9df0]=16797733u; ram[0x088d75f4]=2947547136u; ram[0x088d75f8]=2947612676u; }
int main(){
  oga::Host h={}; h.read8=r8;h.read16=r16;h.read32=r32;h.speak=spk;h.log=lg;
  const oga::Adapter*a=&oga::kDissidia012;
  CHECK(!strcmp(a->game_code,"ULUS10566"),"game code");
  CHECK(oga::find_by_game_code("ULUS10566")==a,"registry");
  CHECK(!a->attach(&h),"wrong build refused");
  setfp(); CHECK(a->attach(&h),"right build attaches"); CHECK(a->ready(),"ready");
  ram[0x096ae208]=65793; ram[0x096ae210]=16447; ram[0x096ae214]=0xFFFFFFFFu;
  ram[0x096ae20c]=0; a->on_frame(); CHECK(said.size()==1&&said[0]=="New Game","speaks New Game");
  a->on_frame(); CHECK(said.size()==1,"no repeat");
  ram[0x096ae20c]=1; a->on_frame(); CHECK(said.back()=="Load Game","Load Game");
  ram[0x096ae20c]=2; a->on_frame(); CHECK(said.back()=="Extras","Extras");
  size_t k=said.size(); ram[0x096ae214]=0; a->on_frame(); CHECK(said.size()==k,"stale struct silent");
  ram[0x096ae214]=0xFFFFFFFFu; ram[0x096ae20c]=7; a->on_frame(); CHECK(said.size()==k,"out of range silent");
  ram[0x096ae20c]=1; a->command(oga::Command::WhereAmI); CHECK(said.back()=="Load Game","query speaks");
  printf("dissidia012 adapter: %d passed, %d failed\n",n-fails,fails); return fails?1:0; }
