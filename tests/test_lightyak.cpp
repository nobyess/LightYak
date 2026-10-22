// SPDX-License-Identifier: MIT
#include "LightYak.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using namespace lightyak;
using Bytes=std::vector<uint8_t>;
static bool inUpdate=false;
static size_t updateAllocations=0;
__attribute__((noinline)) void* operator new(std::size_t n) { if(inUpdate)++updateAllocations;void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p; }
__attribute__((noinline)) void operator delete(void* p) noexcept {std::free(p);}
__attribute__((noinline)) void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#define CHECK(condition) do {if(!(condition))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #condition);}while(0)

// Independent, bitwise CRC reference (production uses a flash nibble table).
uint16_t crc(const Bytes& b) {uint16_t c=0xffff;for(auto x:b){c^=uint16_t(x)<<8;for(int i=0;i<8;i++)c=(c&0x8000)?uint16_t((c<<1)^0x1021):uint16_t(c<<1);}return c;}
void u16(Bytes& b,uint16_t v){b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
uint16_t r16(const Bytes& b,size_t p){CHECK(p+1<b.size());return uint16_t(b[p]|(uint16_t(b[p+1])<<8));}
Bytes raw(uint8_t op,uint8_t id,Bytes p={},uint8_t version=1,uint8_t flags=0){Bytes b{version,op,flags,id,uint8_t(p.size())};b.insert(b.end(),p.begin(),p.end());u16(b,crc(b));return b;}
Bytes escape(const Bytes& body){Bytes b{0x7e};for(auto x:body){if(x==0x7e||x==0x7d){b.push_back(0x7d);b.push_back(x^0x20);}else b.push_back(x);}b.push_back(0x7e);return b;}
Bytes frame(uint8_t op,uint8_t id,Bytes p={}){return escape(raw(op,id,p));}
struct Packet{uint8_t op,id;Bytes payload;};
std::vector<Packet> decode(const Bytes& wire){std::vector<Packet> out;Bytes b;bool esc=false;for(auto x:wire){if(x==0x7e){if(!b.empty()){CHECK(!esc);CHECK(b.size()>=7);CHECK(b.size()==size_t(7+b[4]));Bytes checked(b.begin(),b.end()-2);CHECK(crc(checked)==r16(b,b.size()-2));CHECK(b[0]==Version);CHECK(b[2]==0);out.push_back({b[1],b[3],Bytes(b.begin()+5,b.end()-2)});}b.clear();esc=false;}else if(esc){b.push_back(x^0x20);esc=false;}else if(x==0x7d)esc=true;else b.push_back(x);}return out;}

struct Link {
  Bytes input,output;size_t pos=0;uint32_t now=0;bool blocked=false;
  int reads=0,writes=0,clocks=0;int acceptedBeforeBlock=-1;
  Link(){output.reserve(1<<20);input.reserve(1<<16);}
  static int read(void* p){auto& l=*static_cast<Link*>(p);++l.reads;return l.pos<l.input.size()?l.input[l.pos++]:-1;}
  static bool write(void* p,uint8_t b){auto& l=*static_cast<Link*>(p);++l.writes;if(l.blocked||l.acceptedBeforeBlock==0)return false;if(l.acceptedBeforeBlock>0)--l.acceptedBeforeBlock;l.output.push_back(b);return true;}
  static uint32_t clock(void* p){auto& l=*static_cast<Link*>(p);++l.clocks;return l.now;}
  Transport adapter(){return {this,read,write,clock};}
  void feed(const Bytes& b){input.insert(input.end(),b.begin(),b.end());}
};
struct Rig {
  Link link;Device d;bool enabled=false;int16_t rpm=1200;uint32_t count=0x12345678;float temp=22.5f;
  Rig(){CHECK(d.Listen(link.adapter(),"Pump Controller"));CHECK(d.AddTag(enabled,"Enabled",ReadWrite)==1);CHECK(d.AddTag(rpm,"RPM",ReadWrite)==2);CHECK(d.AddTag(count,"Counter")==3);CHECK(d.AddTag(temp,"Temperature",ReadWrite)==4);}
  void tick(){link.reads=link.writes=link.clocks=0;inUpdate=true;d.Update();inUpdate=false;const auto& w=d.LastWork();CHECK(w.received<=LIGHTYAK_RX_BUDGET);CHECK(w.transmitted<=LIGHTYAK_TX_BUDGET);CHECK(w.prepared<=LIGHTYAK_PREP_BUDGET);CHECK(w.actions<=1);CHECK(w.tagCopies<=1);CHECK(link.reads<=LIGHTYAK_RX_BUDGET);CHECK(link.writes<=LIGHTYAK_TX_BUDGET);CHECK(link.clocks<=1);}
  void pump(size_t n=500){for(size_t i=0;i<n;++i)tick();}
  Packet request(uint8_t op,Bytes p={},uint8_t id=17){CHECK(link.pos==link.input.size());link.output.clear();link.feed(frame(op,id,p));pump();auto ps=decode(link.output);CHECK(ps.size()==1);CHECK(ps[0].id==id);return ps[0];}
};
Bytes target(uint16_t ref,uint8_t prop=0){Bytes b;u16(b,ref);b.push_back(prop);return b;}
Bytes readTargets(std::initializer_list<Bytes> targets){Bytes b{uint8_t(targets.size())};for(const auto& t:targets)b.insert(b.end(),t.begin(),t.end());return b;}
Bytes item(uint16_t ref,uint8_t type,Bytes value,uint8_t prop=0){Bytes b=target(ref,prop);b.push_back(type);b.push_back(uint8_t(value.size()));b.insert(b.end(),value.begin(),value.end());return b;}
Bytes writeItems(std::initializer_list<Bytes> items){Bytes b{uint8_t(items.size())};for(const auto& it:items)b.insert(b.end(),it.begin(),it.end());return b;}
void error(const Packet& p,Status s){CHECK(p.op==ErrorResponse);CHECK(p.payload==Bytes{uint8_t(s)});}
struct Result{uint16_t ref;uint8_t prop,status,type;Bytes value;};
std::vector<Result> readResults(const Packet& p){CHECK(p.op==(Read|ResponseBit));CHECK(!p.payload.empty());std::vector<Result> v;size_t pos=1;for(int i=0;i<p.payload[0];++i){CHECK(pos+6<=p.payload.size());uint8_t len=p.payload[pos+5];CHECK(pos+6+len<=p.payload.size());v.push_back({r16(p.payload,pos),p.payload[pos+2],p.payload[pos+3],p.payload[pos+4],Bytes(p.payload.begin()+pos+6,p.payload.begin()+pos+6+len)});pos+=6+len;}CHECK(pos==p.payload.size());return v;}
uint8_t writeStatus(const Packet& p,size_t i=0){CHECK(p.op==(Write|ResponseBit));CHECK(p.payload.size()==size_t(1+4*p.payload[0]));CHECK(i<p.payload[0]);return p.payload[1+4*i+3];}

int main(){
  int passed=0,failed=0;
  auto test=[&](const char* name,const std::function<void()>& fn){try{fn();++passed;std::cout<<"PASS "<<name<<"\n";}catch(const std::exception& e){inUpdate=false;++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<"\n";}};
  test("CRC golden vector",[]{CHECK(crc(Bytes{'1','2','3','4','5','6','7','8','9'})==0x29b1);});
  test("unconfigured Update is harmless",[]{Device d;d.Update();CHECK(!d.Ready());CHECK(d.LastError()==NoTransport);});
  test("idle path skips clock and tag work",[]{Rig r;r.tick();CHECK(r.link.clocks==0);CHECK(r.d.LastWork().actions==0);CHECK(r.d.LastWork().received==0);});
  test("identify fields and metadata",[]{Rig r;r.d.SetCatalogRevision(0x12345678);auto p=r.request(Identify);CHECK(p.op==0x81);CHECK(p.payload[0]==InitOk);CHECK(r16(p.payload,1)==4);CHECK(p.payload[3]==0x78);CHECK(p.payload[7]==MaxPayload);CHECK(p.payload[8]==MaxBatch);CHECK(p.payload[9]==LIGHTYAK_MAX_TAGS);CHECK(std::string(p.payload.begin()+11,p.payload.end())=="Pump Controller");});
  test("browse every system and application entry",[]{Rig r;uint16_t cursor=0;size_t seen=0;do{Bytes q;u16(q,cursor);auto p=r.request(Browse,q);CHECK(p.op==0x82);CHECK(p.payload.size()==size_t(8+p.payload[7]));CHECK(p.payload[7]>0);if(seen<SystemProperties){CHECK(r16(p.payload,2)==0);CHECK(p.payload[4]==seen+1);CHECK(p.payload[6]==ReadOnly);}else{CHECK(r16(p.payload,2)==seen-SystemProperties+1);CHECK(p.payload[4]==Value);}cursor=r16(p.payload,0);++seen;}while(cursor!=EndCursor);CHECK(seen==10);});
  test("browse invalid cursor",[]{Rig r;error(r.request(Browse,{0xff,0xff}),BadCursor);});
  test("read mixed types and little endian",[]{Rig r;auto v=readResults(r.request(Read,readTargets({target(1),target(2),target(3),target(4)})));CHECK(v.size()==4);CHECK(v[0].type==Bool&&v[0].value==Bytes{0});CHECK(v[1].type==Int16&&v[1].value==Bytes({0xb0,0x04}));CHECK(v[2].value==Bytes({0x78,0x56,0x34,0x12}));CHECK(v[3].type==Float32&&v[3].value==Bytes({0,0,0xb4,0x41}));});
  test("read system properties",[]{Rig r;auto v=readResults(r.request(Read,readTargets({target(0,ProtocolVersion),target(0,CatalogRevision),target(0,PayloadLimit),target(0,BatchLimit),target(0,InitializationStatus)})));CHECK(v[0].value==Bytes{1});CHECK(v[1].type==UInt32);CHECK(v[2].value==Bytes{64});CHECK(v[3].value==Bytes{6});CHECK(v[4].value==Bytes{0});});
  test("heartbeat boundaries and clock wrap",[]{Rig r;for(uint32_t n:{0u,499u,500u,999u,1000u,0xfffffff0u,0u}){r.link.now=n;auto v=readResults(r.request(Read,readTargets({target(0,Heartbeat)})));CHECK(v[0].value==Bytes{uint8_t((n/500)&1)});}});
  test("unknown targets retain per-item status",[]{Rig r;auto v=readResults(r.request(Read,readTargets({target(99),target(1,Heartbeat),target(0,99),target(2)})));CHECK(v[0].status==UnknownTarget&&v[0].value.empty());CHECK(v[1].status==UnknownTarget);CHECK(v[2].status==UnknownTarget);CHECK(v[3].status==Ok);});
  test("maximum read batch fits frame",[]{Rig r;auto v=readResults(r.request(Read,readTargets({target(3),target(3),target(3),target(3),target(3),target(3)})));CHECK(v.size()==6);for(auto& x:v)CHECK(x.value.size()==4);});
  test("scalar writes and readback",[]{Rig r;auto p=r.request(Write,writeItems({item(1,Bool,{1}),item(2,Int16,{0x18,0xfc}),item(4,Float32,{0,0,0x28,0x42})}));CHECK(writeStatus(p,0)==Ok&&writeStatus(p,1)==Ok&&writeStatus(p,2)==Ok);CHECK(r.enabled);CHECK(r.rpm==-1000);CHECK(r.temp==42.0f);});
  test("write permissions and system immutability",[]{Rig r;auto p=r.request(Write,writeItems({item(3,UInt32,{1,0,0,0}),item(0,Bool,{1},Heartbeat)}));CHECK(writeStatus(p,0)==NotWritable);CHECK(writeStatus(p,1)==NotWritable);CHECK(r.count==0x12345678);});
  test("write unknown reference and property",[]{Rig r;auto p=r.request(Write,writeItems({item(88,Bool,{1}),item(2,Int16,{1,0},1),item(0,Bool,{1},99)}));for(size_t i=0;i<3;++i)CHECK(writeStatus(p,i)==UnknownTarget);});
  test("strict type and encoded length",[]{Rig r;auto p=r.request(Write,writeItems({item(2,UInt16,{1,0}),item(2,Int16,{1}),item(2,99,{1,0})}));for(size_t i=0;i<3;++i)CHECK(writeStatus(p,i)==TypeMismatch);CHECK(r.rpm==1200);});
  test("invalid bool representation rejected",[]{Rig r;auto p=r.request(Write,writeItems({item(1,Bool,{2})}));CHECK(writeStatus(p)==BadValue);CHECK(!r.enabled);});
  test("partial batch success is explicit",[]{Rig r;auto p=r.request(Write,writeItems({item(2,Int16,{42,0}),item(3,UInt32,{0,0,0,0}),item(1,Bool,{1})}));CHECK(writeStatus(p,0)==Ok);CHECK(writeStatus(p,1)==NotWritable);CHECK(writeStatus(p,2)==Ok);CHECK(r.rpm==42&&r.enabled);});
  test("malformed later write prevents all effects",[]{Rig r;Bytes p=writeItems({item(1,Bool,{1}),item(2,Int16,{1,0})});p.pop_back();error(r.request(Write,p),BadRequest);CHECK(!r.enabled&&r.rpm==1200);});
  test("trailing write bytes prevent all effects",[]{Rig r;Bytes p=writeItems({item(1,Bool,{1})});p.push_back(0);error(r.request(Write,p),BadRequest);CHECK(!r.enabled);});
  test("oversized item prevents all effects",[]{Rig r;error(r.request(Write,writeItems({item(1,Bool,{1}),item(2,Int16,{1,2,3,4,5})})),BadRequest);CHECK(!r.enabled);});
  test("empty and excessive batch counts",[]{Rig r;for(uint8_t op:{uint8_t(Read),uint8_t(Write)}){error(r.request(op,{}),BadRequest);error(r.request(op,{0}),BadRequest);error(r.request(op,{7}),BadRequest);}});
  test("bad read structure",[]{Rig r;error(r.request(Read,{1,1,0}),BadRequest);error(r.request(Read,{1,1,0,0,0}),BadRequest);});
  test("bad identify and browse lengths",[]{Rig r;error(r.request(Identify,{1}),BadRequest);error(r.request(Browse,{1}),BadRequest);});
  test("unknown operation version and flags",[]{Rig r;error(r.request(55),Unsupported);for(int kind=0;kind<2;++kind){r.link.output.clear();r.link.feed(escape(raw(Identify,44,{},kind?1:2,kind?1:0)));r.pump();auto v=decode(r.link.output);CHECK(v.size()==1);error(v[0],kind?BadRequest:Unsupported);}});
  test("corrupt write CRC cannot change values",[]{Rig r;Bytes b=raw(Write,8,writeItems({item(1,Bool,{1})}));b.back()^=1;r.link.feed(escape(b));r.pump();CHECK(r.link.output.empty());CHECK(!r.enabled);CHECK(r.request(Identify).op==0x81);});
  test("no write until complete CRC and delimiter",[]{Rig r;Bytes b=frame(Write,8,writeItems({item(1,Bool,{1})}));for(size_t i=0;i+1<b.size();++i){r.link.feed({b[i]});r.tick();CHECK(!r.enabled);}r.link.feed({b.back()});r.pump();CHECK(r.enabled);});
  test("writes span calls, at most one scalar per call",[]{Rig r;r.link.feed(frame(Write,1,writeItems({item(2,Int16,{42,0}),item(1,Bool,{1})})));bool intermediate=false;for(int i=0;i<300;++i){r.tick();if(r.rpm==42&&!r.enabled)intermediate=true;}CHECK(intermediate);CHECK(r.enabled);});
  test("fragmented input and escaped values",[]{Rig r;Bytes b=frame(Write,0x7e,writeItems({item(2,Int16,{0x7e,0x7d})}));for(auto byte:b){r.link.feed({byte});r.tick();}r.pump();CHECK(uint16_t(r.rpm)==0x7d7e);auto ps=decode(r.link.output);CHECK(ps.size()==1&&ps[0].id==0x7e);});
  test("escaped metadata",[]{Rig r;CHECK(r.d.Listen(r.link.adapter(),"A~}B"));auto p=r.request(Identify);CHECK(std::string(p.payload.begin()+11,p.payload.end())=="A~}B");});
  test("blocked transmitter never waits and resumes",[]{Rig r;r.link.blocked=true;r.link.feed(frame(Identify,1));r.pump();CHECK(r.link.output.empty());r.link.blocked=false;r.pump();auto ps=decode(r.link.output);CHECK(ps.size()==1&&ps[0].op==0x81);});
  test("backpressure at every output byte preserves frame",[]{Rig r;r.link.feed(frame(Read,0x7d,readTargets({target(3),target(4)})));for(int i=0;i<500;++i){r.link.acceptedBeforeBlock=1;r.tick();}CHECK(decode(r.link.output).size()==1);});
  test("busy rejects pipelined write without side effect",[]{Rig r;r.link.blocked=true;r.link.feed(frame(Identify,1));r.pump();r.link.feed(frame(Write,2,writeItems({item(1,Bool,{1})})));r.pump();CHECK(!r.enabled);r.link.blocked=false;r.pump();auto ps=decode(r.link.output);CHECK(ps.size()==2);CHECK(ps[0].id==1);CHECK(ps[1].id==2);error(ps[1],Busy);});
  test("busy storm remains bounded",[]{Rig r;r.link.blocked=true;r.link.feed(frame(Identify,1));r.pump();for(uint8_t id=2;id<50;++id)r.link.feed(frame(Write,id,writeItems({item(1,Bool,{1})})));r.pump(2000);CHECK(!r.enabled);r.link.blocked=false;r.pump();auto ps=decode(r.link.output);CHECK(ps.size()==2);error(ps[1],Busy);});
  test("timeout discards stale partial request",[]{Rig r;Bytes b=frame(Write,1,writeItems({item(1,Bool,{1})}));r.link.feed(Bytes(b.begin(),b.begin()+7));r.pump(5);r.link.now=FrameTimeoutMs;r.tick();r.link.feed(Bytes(b.begin()+7,b.end()));r.pump();CHECK(!r.enabled&&r.link.output.empty());CHECK(r.request(Identify).op==0x81);});
  test("timeout subtraction survives millis wrap",[]{Rig r;r.link.now=0xfffffff0u;r.link.feed({0x7e,1,Write});r.pump(2);r.link.now=0x00000100u;r.tick();CHECK(r.request(Identify).op==0x81);});
  test("oversized frame resynchronizes",[]{Rig r;Bytes bad{0x7e,1,Write,0,1,65};bad.insert(bad.end(),200,0x11);bad.push_back(0x7e);r.link.feed(bad);r.pump();CHECK(r.link.output.empty());CHECK(r.request(Identify).op==0x81);});
  test("malformed escape resynchronizes",[]{Rig r;r.link.feed({0x7e,1,0x7d,0,0x7e});r.pump();CHECK(r.link.output.empty());CHECK(r.request(Identify).op==0x81);});
  test("truncated escape resynchronizes",[]{Rig r;r.link.feed({0x7e,1,0x7d,0x7e});r.pump();CHECK(r.link.output.empty());CHECK(r.request(Identify).op==0x81);});
  test("frame length mismatch rejected",[]{Rig r;Bytes b=raw(Write,1,writeItems({item(1,Bool,{1})}));b[4]--;r.link.feed(escape(b));r.pump();CHECK(!r.enabled&&r.link.output.empty());});
  test("shared delimiters and repeated delimiters",[]{Rig r;r.link.feed({0x7e,0x7e,0x7e});r.pump();CHECK(r.request(Identify).op==0x81);});
  test("names are bounded and nonempty",[]{Link l;Device d;CHECK(!d.Listen(l.adapter(),""));CHECK(d.LastError()==InvalidName);std::string max(MaxName,'x');CHECK(d.Listen(l.adapter(),max.c_str()));uint8_t n=0;CHECK(d.AddTag(n,max.c_str())==1);max.push_back('x');CHECK(d.AddTag(n,max.c_str())==0);CHECK(d.LastError()==InvalidName);});
  test("invalid name never read during identify",[]{Rig r;CHECK(!r.d.Listen(r.link.adapter(),static_cast<const char*>(nullptr)));auto p=r.request(Identify);CHECK(p.payload[0]==InvalidName);CHECK(p.payload[10]==0);error(r.request(Read,readTargets({target(0,Heartbeat)})),NotReady);});
  test("registry overflow is sticky and reported",[]{Rig r;uint8_t n=0;for(int i=4;i<LIGHTYAK_MAX_TAGS;++i)CHECK(r.d.AddTag(n,"N"));CHECK(!r.d.AddTag(n,"Overflow"));CHECK(!r.d.Ready());auto p=r.request(Identify);CHECK(p.payload[0]==RegistryFull);error(r.request(Read,readTargets({target(1)})),NotReady);});
  test("registry freezes on first Update",[]{Rig r;r.tick();uint8_t n=0;CHECK(!r.d.AddTag(n,"Late"));CHECK(r.d.LastError()==RegistryFrozen);CHECK(!r.d.SetCatalogRevision(2));});
  test("Listen reinitializes device state",[]{Rig r;r.tick();CHECK(r.d.Listen(r.link.adapter(),"New Device"));CHECK(r.d.TagCount()==0);CHECK(r.d.Ready());CHECK(r.d.AddTag(r.enabled,"New Tag",ReadWrite)==1);CHECK(r.request(Identify).payload[0]==InitOk);});
  test("volatile scalars are supported",[]{Rig r;volatile uint16_t x=0xabcd;CHECK(r.d.AddTag(x,"ISR value",ReadWrite)==5);CHECK(readResults(r.request(Read,readTargets({target(5)})))[0].value==Bytes({0xcd,0xab}));CHECK(writeStatus(r.request(Write,writeItems({item(5,UInt16,{0x34,0x12})})))==Ok);CHECK(x==0x1234);});
  test("all scalar widths roundtrip",[]{Rig r;int8_t a=-128;uint8_t b=255;uint16_t c=65535;int32_t d=INT32_MIN;uint32_t e=UINT32_MAX;char f='A';CHECK(r.d.AddTag(a,"i8",ReadWrite)==5);CHECK(r.d.AddTag(b,"u8",ReadWrite)==6);CHECK(r.d.AddTag(c,"u16",ReadWrite)==7);CHECK(r.d.AddTag(d,"i32",ReadWrite)==8);CHECK(r.d.AddTag(e,"u32",ReadWrite)==9);CHECK(r.d.AddTag(f,"char",ReadWrite)==10);auto v=readResults(r.request(Read,readTargets({target(5),target(6),target(7),target(8),target(9),target(10)})));CHECK(v[0].value==Bytes{128});CHECK(v[1].value==Bytes{255});CHECK(v[2].value==Bytes({255,255}));CHECK(v[3].value==Bytes({0,0,0,128}));CHECK(v[4].value==Bytes({255,255,255,255}));CHECK(v[5].type==Char);auto p=r.request(Write,writeItems({item(5,Int8,{127}),item(6,UInt8,{0}),item(7,UInt16,{0,0}),item(8,Int32,{255,255,255,127}),item(9,UInt32,{0,0,0,0}),item(10,Char,{'Z'})}));for(size_t i=0;i<6;i++)CHECK(writeStatus(p,i)==Ok);CHECK(a==127&&b==0&&c==0&&d==INT32_MAX&&e==0&&f=='Z');});
  test("float special bit patterns are preserved",[]{Rig r;for(Bytes b: {Bytes{0,0,0,128},Bytes{0,0,128,127},Bytes{0,0,192,127}}){CHECK(writeStatus(r.request(Write,writeItems({item(4,Float32,b)})))==Ok);CHECK(readResults(r.request(Read,readTargets({target(4)})))[0].value==b);}});
  test("deterministic malformed-input fuzz and recovery",[]{Rig r;std::mt19937 rng(0x1a17);for(int round=0;round<1000;++round){Bytes noise;int n=int(rng()%128);for(int i=0;i<n;++i)noise.push_back(uint8_t(rng()));noise.push_back(0x7e);r.link.feed(noise);r.pump(noise.size()+200);CHECK(!r.enabled&&r.rpm==1200);r.link.output.clear();CHECK(r.request(Identify,{},uint8_t(round)).op==0x81);}});
  test("valid random typed writes and readbacks",[]{Rig r;std::mt19937 rng(42);for(int i=0;i<500;++i){uint32_t bits=rng();Bytes b{uint8_t(bits),uint8_t(bits>>8),uint8_t(bits>>16),uint8_t(bits>>24)};CHECK(writeStatus(r.request(Write,writeItems({item(4,Float32,b)})))==Ok);CHECK(readResults(r.request(Read,readTargets({target(4)})))[0].value==b);}});
  test("full registry browses every configured slot",[]{
    Rig r;
    uint32_t values[LIGHTYAK_MAX_TAGS] = {};
    for(uint16_t i=4;i<LIGHTYAK_MAX_TAGS;++i) {
      values[i]=1000+i;
      CHECK(r.d.AddTag(values[i],"Extra",ReadWrite)==i+1);
    }
    CHECK(r.d.TagCount()==LIGHTYAK_MAX_TAGS);
    auto identity=r.request(Identify);
    CHECK(r16(identity.payload,1)==LIGHTYAK_MAX_TAGS);
    uint16_t cursor=0,seen=0;
    do {
      Bytes q;u16(q,cursor);
      auto page=r.request(Browse,q);
      CHECK(page.op==(Browse|ResponseBit));
      if(seen>=SystemProperties) CHECK(r16(page.payload,2)==seen-SystemProperties+1);
      cursor=r16(page.payload,0);++seen;
    } while(cursor!=EndCursor);
    CHECK(seen==SystemProperties+LIGHTYAK_MAX_TAGS);
  });
  test("last reserved slot reads and writes without extra scalar work",[]{
    Rig r;
    uint32_t values[LIGHTYAK_MAX_TAGS] = {};
    for(uint16_t i=4;i<LIGHTYAK_MAX_TAGS;++i) CHECK(r.d.AddTag(values[i],"Value",ReadWrite)==i+1);
    auto p=r.request(Write,writeItems({item(LIGHTYAK_MAX_TAGS,UInt32,{0x78,0x56,0x34,0x12})}));
    CHECK(writeStatus(p)==Ok);
    CHECK(values[LIGHTYAK_MAX_TAGS-1]==0x12345678);
    r.link.output.clear();
    r.link.feed(frame(Read,17,readTargets({target(LIGHTYAK_MAX_TAGS)})));
    unsigned copies=0;
    for(int i=0;i<500;++i) {r.tick();copies+=r.d.LastWork().tagCopies;}
    auto packets=decode(r.link.output);CHECK(packets.size()==1);
    CHECK(readResults(packets[0])[0].value==Bytes({0x78,0x56,0x34,0x12}));
    CHECK(copies==1);
  });
  test("idle work is unchanged when the registry is full",[]{
    Rig r;uint8_t value=0;
    for(uint16_t i=4;i<LIGHTYAK_MAX_TAGS;++i) CHECK(r.d.AddTag(value,"Value")==i+1);
    r.tick();
    CHECK(r.link.reads==1 && r.link.clocks==0 && r.link.writes==0);
    CHECK(r.d.LastWork().actions==0 && r.d.LastWork().tagCopies==0);
  });
  test("Update never allocated heap memory",[]{CHECK(updateAllocations==0);});
  std::cout<<passed<<" passed, "<<failed<<" failed; sizeof(Device)="<<sizeof(Device)<<" native bytes\n";
  return failed?1:0;
}
