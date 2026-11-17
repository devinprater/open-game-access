// Host test for the 012 adapter: synthetic RAM from the measured layout.
#include "adapter.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace oga { extern const Adapter kDissidia012; }
namespace oga { namespace dissidia012 { bool FieldLiveForTest(void); } }
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
  // Data Config
  ram[0x096ae20c]=9; a->on_frame();  // title cursor out of range: title off
  ram[0x096aea6c]=8; ram[0x096aea78]=1; ram[0x096aea70]=0; a->on_frame(); CHECK(said.back()=="Create New Data","config row 0");
  ram[0x096aea70]=1; a->on_frame(); CHECK(said.back()=="Transfer save data","config row 1");
  k=said.size(); ram[0x096aea78]=0; ram[0x096aea70]=0; a->on_frame(); CHECK(said.size()==k,"config stale silent");
  ram[0x096aea78]=1; ram[0x096aea70]=1; a->command(oga::Command::WhereAmI); CHECK(said.back()=="Transfer save data","config query");
  // Setup screens
  ram[0x096aeaa0]=0;
  struct T{uint32_t id,mx;const char*r0;const char*r1;} ts[]={{10,2,"Casual","Average"},{12,6,"Mon","Tue"},{14,1,"Action-style","RPG Mode"},{16,2,"Standard","Strong"},{18,1,"Yes","No"}};
  for (auto&t:ts){ ram[0x096aec1c]=t.id; ram[0x096aea78]=t.mx; ram[0x096aea70]=0; a->on_frame(); CHECK(said.back()==t.r0,t.r0);
    ram[0x096aea70]=1; a->on_frame(); CHECK(said.back()==t.r1,t.r1); }
  ram[0x096aec1c]=16; ram[0x096aea78]=6; k=said.size(); ram[0x096aea70]=2; a->on_frame(); CHECK(said.size()==k,"id/max mismatch silent");
  // Main menu
  ram[0x096aec1c]=0; ram[0x096aea70]=0; ram[0x096aea6c]=0; ram[0x096aebe8]=8;
  ram[0x096aebec]=0; ram[0x096aec14]=0; a->on_frame(); CHECK(said.back()=="Story Mode","main row 0");
  k=said.size(); ram[0x096aebec]=3; ram[0x096aec14]=3; a->on_frame(); CHECK(said.size()==k,"greyed PP Catalog silent");
  ram[0x096aebec]=6; ram[0x096aec14]=6; a->on_frame(); CHECK(said.back()=="Options","main row 6");
  k=said.size(); ram[0x096aec14]=2; a->on_frame(); CHECK(said.size()==k,"mirror mismatch silent");
  ram[0x096aec14]=6; ram[0x096aebec]=6; a->command(oga::Command::WhereAmI); CHECK(said.back()=="Options","main query");
  // Field reader (static chain 0x089D83D8 -> controller -> +0x6D0)
  ram[0x096aebe8]=0; ram[0x096aebec]=0; ram[0x096aec14]=0; ram[0x096aec1c]=0; ram[0x096aea6c]=0; ram[0x096aea78]=0;
  ram[0x089d83d8]=0; k=said.size();
  a->command(oga::Command::WhereAmI); CHECK(said.size()>k && said.back()=="Not on the field.","no field: refuses");
  ram[0x089d83d8]=0x08b83714u; ram[0x08b83714+0x6d0]=0;   // controller, no player (cutscene)
  CHECK(!oga::dissidia012::FieldLiveForTest(),"cutscene not live");
  ram[0x08b83714+0x6d0]=0x0976b250u;
  ram[0x08b83714+0x38]=8; ram[0x08b83714+0x40]=0xffffffffu;   // cell (8,-1)
  ram[0x0976b250+0x30]=0x42680000u; ram[0x0976b250+0x34]=0xbf333333u; ram[0x0976b250+0x38]=0x41a00000u;
  CHECK(oga::dissidia012::FieldLiveForTest(),"field live via chain");
  a->command(oga::Command::WhereAmI); CHECK(said.back()=="On the field. Position 58, 20.","field position spoken");
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"field stays quiet on frame");
  ram[0x08b83714+0x6d0]=0; CHECK(!oga::dissidia012::FieldLiveForTest(),"player gone -> not live");
  // Gateway board (ctrl state 20; cells per session 10). Controller chain as the field.
  ram[0x089d83d8]=0x08b83714u; ram[0x08b83714+0x6d0]=0;   // no field player object on the board
  ram[0x08b83714+0x68]=20;
  // Board identity (session 14): the disc-exists fingerprint of the prologue board,
  // plus the entry (B) and Stigma (H) disc objects' coordinates for the spot-check.
  for (int g : {1,11,20,21,22,23,24,25,26,27,33,34,43,53}) ram[0x08b82a30+8*g]=1;
  ram[0x095cd570+0x40*21]=0xC1600000u; ram[0x095cd570+0x40*21+8]=0xC0800000u;   // B at (-14,-4)
  ram[0x095cd570+0x40*27]=0x41200000u; ram[0x095cd570+0x40*27+8]=0xC0800000u;   // H at (10,-4)
  ram[0x08b82a24]=21; ram[0x095d123c]=0;                  // entry transition: manager still null
  ram[0x095cd410]=0xFFFFFFFFu; ram[0x095cd41c]=0xFFFFFFFFu;
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"board transition silent");
  ram[0x095d123c]=0x08b82ad8u; ram[0x095cd410]=0x08b82ad8u;   // settled on B, focus at rest
  a->on_frame(); CHECK(said.back()=="On B, the entry disc. Left: A, empty. Right: C, empty. Up: T1, empty. Down: nothing.","board entry B");
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"board steady silent");
  ram[0x08b82a24]=22; ram[0x095d123c]=0x08b82ae0u; ram[0x095cd410]=0x08b82ae0u;
  a->on_frame(); CHECK(said.back()=="On C. Left: B, the entry disc. Right: chest on D. Up: nothing. Down: nothing.","hop to C");
  a->command(oga::Command::WhereAmI); CHECK(said.back()=="On C. Left: B, the entry disc. Right: chest on D. Up: nothing. Down: nothing.","board query");
  ram[0x095cd410]=0x08b82ae8u;                            // chest prompt opens (target D)
  a->on_frame(); CHECK(said.back()=="Treasure Chest — Contains helpful items. Engage / Cancel","chest prompt");
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"prompt no repeat");
  ram[0x095cd410]=0x08b82ae0u; a->on_frame();             // cancel: focus back to her disc
  ram[0x095cd410]=0x08b82ae8u; a->on_frame(); CHECK(said.back()=="Treasure Chest — Contains helpful items. Engage / Cancel","chest prompt again");
  ram[0x095cd410]=0x08b82ae0u; a->on_frame();
  ram[0x095cd41c]=0x08b82ae0u; a->on_frame(); CHECK(said.back()=="Scan: C.","scan on C");
  ram[0x095cd41c]=0x08b82ae8u; a->on_frame(); CHECK(said.back()=="Scan: D. Treasure Chest — Contains helpful items.","scan chest");
  ram[0x095cd41c]=0x08b82af0u; a->on_frame(); CHECK(said.back()=="Scan: E. Enemy manikin.","scan manikin");
  ram[0x095cd41c]=0xFFFFFFFFu; k=said.size(); a->on_frame(); CHECK(said.size()==k,"scan off silent");
  ram[0x08b82a24]=24; ram[0x095d123c]=0x08b82af0u; ram[0x095cd410]=0x08b82af0u;
  a->on_frame(); CHECK(said.back()=="On E. Enemy manikin here. Left: chest on D. Right: F, empty. Up: nothing. Down: Kain on J.","on manikin disc");
  ram[0x08b82a24]=26; ram[0x095d123c]=0x08b82b00u; ram[0x095cd410]=0x08b82b00u;
  a->on_frame(); CHECK(said.back()=="On G. Left: F, empty. Right: Stigma of Chaos on H. Up: nothing. Down: nothing.","hop to G");
  ram[0x095cd410]=0x08b82b08u; a->on_frame(); CHECK(said.back()=="Stigma of Chaos — Engaging this piece finishes the level. Engage / Cancel","stigma prompt");
  ram[0x08b83714+0x68]=21; a->on_frame(); CHECK(said.back()=="Level finished. Story Mode chapter select.","stigma exit");
  ram[0x095d123c]=0xFFFFFFFFu;   // the shell's measured manager value (session 13): cells broken
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"exit no repeat / shell silent");
  ram[0x08b83714+0x68]=20; ram[0x08b82a24]=30; ram[0x095d123c]=0x08b82a30u+8u*30u;  // unknown board
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"unknown grid silent");
  ram[0x08b82a24]=22; ram[0x095d123c]=0x08b82ad8u;        // grid/manager disagreement
  a->on_frame(); CHECK(said.size()==k,"disagreement silent");
  // Session 14: KP/Gil change announcements on the board (change-triggered only).
  ram[0x095d123c]=0x08b82ae0u; ram[0x095cd410]=0x08b82ae0u;   // settled on C again, focus at rest
  a->on_frame(); CHECK(said.back()=="On C. Left: B, the entry disc. Right: chest on D. Up: nothing. Down: nothing.","board re-settles on C");
  ram[0x08bbfa00]=50; ram[0x08bbfa28]=50; a->on_frame(); CHECK(said.back()=="Gil: 50.","gil change speaks");
  ram[0x089d65a8]=0x09ae3b04u; ram[0x089d85bc]=0x09ae3b04u; ram[0x09ae3d64]=0;
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"kp baseline silent");
  ram[0x09ae3d64]=1; a->on_frame(); CHECK(said.back()=="KP: 1.","kp change speaks");
  ram[0x08bbfa00]=64; ram[0x08bbfa28]=64; a->on_frame(); CHECK(said.back()=="Gil: 64.","gil change 2");
  ram[0x09ae3d64]=0xFFFFFF9Eu; k=said.size(); a->on_frame(); CHECK(said.size()==k,"kp garbage silent");
  ram[0x089d85bc]=0x09ae3c04u; ram[0x09ae3d64]=2; a->on_frame(); CHECK(said.size()==k,"kp twin mismatch silent");
  ram[0x089d85bc]=0x09ae3b04u; a->on_frame(); CHECK(said.back()=="KP: 2.","kp sane again speaks");
  ram[0x08bbfa28]=999; k=said.size(); a->on_frame(); CHECK(said.size()==k,"gil pair disagreement silent");
  ram[0x08bbfa00]=78; ram[0x08bbfa28]=78; a->on_frame(); CHECK(said.back()=="Gil: 78.","gil pair restored speaks");
  // Session 14: Chapter 1's Crescent Lake board plays at ctrl state 21.
  for (int g=0;g<60;g++) ram[0x08b82a30+8*g]=0;
  for (int g : {2,12,16,20,22,23,25,26,30,31,32,33,34,35,36,42,45,52,55}) ram[0x08b82a30+8*g]=1;
  ram[0x095cd570+0x40*20]=0xC1900000u; ram[0x095cd570+0x40*20+8]=0xC0800000u;   // entry disc 20 at (-18,-4)
  ram[0x095cd570+0x40*55]=0x40000000u; ram[0x095cd570+0x40*55+8]=0x41000000u;   // Stigma disc 55 at (2,8)
  ram[0x08b82a24]=20; ram[0x095d123c]=0x08b82a30u+8u*20u; ram[0x095cd410]=0x08b82a30u+8u*20u; ram[0x095cd41c]=0xFFFFFFFFu;
  ram[0x08b83714+0x68]=21; a->on_frame(); CHECK(said.back()=="Exited the gateway. Story Mode chapter select.","state 20 visit closes at 21");
  a->on_frame(); CHECK(said.back()=="On disc 20, the entry disc. Left: nothing. Right: nothing. Up: nothing. Down: disc 30, empty.","lake entry disc 20");
  ram[0x08b82a24]=32; ram[0x095d123c]=0x08b82a30u+8u*32u; ram[0x095cd410]=0x08b82a30u+8u*32u;
  a->on_frame(); CHECK(said.back()=="On disc 32. Left: Fleeting Flash on disc 31. Right: Fleeting Flash on disc 33. Up: Capricious Reaper on disc 22. Down: disc 42, empty.","lake disc 32");
  ram[0x095cd410]=0x08b82a30u+8u*33u; a->on_frame(); CHECK(said.back()=="Fleeting Flash — Lv 2. Battle stage: Old Chaos Shrine. KP chance: Win battle. Engage / Cancel","flash prompt");
  ram[0x095cd410]=0x08b82a30u+8u*32u; ram[0x095d123c]=0;   // battle: the board cells break
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"battle break silent, no exit");
  ram[0x095d123c]=0x08b82a30u+8u*32u; ram[0x09ae3d64]=3; ram[0x08bbfa00]=92; ram[0x08bbfa28]=92;
  a->on_frame(); CHECK(said[said.size()-2]=="KP: 3."&&said.back()=="Gil: 92.","battle deltas speak on return");
  ram[0x095cd41c]=0x08b82a30u+8u*36u; a->on_frame(); CHECK(said.back()=="Scan: disc 36. Laguna — Assist Character.","scan laguna");
  ram[0x095cd41c]=0xFFFFFFFFu;
  ram[0x08b82a24]=45; ram[0x095d123c]=0x08b82a30u+8u*45u; ram[0x095cd410]=0x08b82a30u+8u*45u;
  a->on_frame(); CHECK(said.back()=="On disc 45. Left: nothing. Right: nothing. Up: disc 35, empty. Down: Stigma of Chaos on disc 55.","lake disc 45");
  ram[0x095cd410]=0x08b82a30u+8u*55u; a->on_frame(); CHECK(said.back()=="Stigma of Chaos — Engaging this piece finishes the level. Engage / Cancel","lake stigma prompt");
  ram[0x08b83714+0x68]=3; a->on_frame(); CHECK(said.back()=="Level finished.","lake exit to field");
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"lake exit no repeat");
  // Session 14: an unrecognised board is announced generically, pieces never named.
  for (int g=0;g<60;g++) ram[0x08b82a30+8*g]=0;
  ram[0x08b82a30+8*40]=1; ram[0x08b82a30+8*41]=1; ram[0x08b82a30+8*50]=1;
  ram[0x095cd570+0x40*41]=0x40800000u;   // disc 41 at (4,0); disc 40 at (0,0), disc 50 at (0,4)
  ram[0x095cd570+0x40*50+8]=0x40800000u;
  ram[0x08b83714+0x68]=21; ram[0x08b82a24]=40; ram[0x095d123c]=0x08b82a30u+8u*40u; ram[0x095cd410]=0x08b82a30u+8u*40u; ram[0x095cd41c]=0xFFFFFFFFu;
  a->on_frame(); CHECK(said.back()=="On disc 40. Left: nothing. Right: disc 41, empty. Up: nothing. Down: disc 50, empty.","generic board arrival");
  ram[0x095cd41c]=0x08b82a30u+8u*41u; a->on_frame(); CHECK(said.back()=="Scan: disc 41.","generic scan");
  ram[0x095cd41c]=0xFFFFFFFFu; ram[0x095cd410]=0x08b82a30u+8u*41u;
  k=said.size(); a->on_frame(); CHECK(said.size()==k,"generic prompt silent");
  ram[0x08b83714+0x68]=2; a->on_frame(); CHECK(said.back()=="Exited the gateway.","generic exit");
  printf("dissidia012 adapter: %d passed, %d failed\n",n-fails,fails); return fails?1:0; }
