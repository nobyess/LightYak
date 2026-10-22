// SPDX-License-Identifier: MIT
#include "LightYak.h"
#include <string.h>
#ifdef __AVR__
#include <util/atomic.h>
#define LY_ATOMIC ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
#define LY_TEXT(s) Text(PSTR(s), true)
#else
#define LY_ATOMIC for (bool ly_once = true; ly_once; ly_once = false)
#define LY_TEXT(s) Text(s)
#endif

lightyak::Device LightYak;

namespace lightyak {
#if defined(ARDUINO_ARCH_ESP32)
bool IRAM_ATTR Device::TryLockTags() { return portTRY_ENTER_CRITICAL_SAFE(&tagMux_, 0); }
void IRAM_ATTR Device::UnlockTags() { portEXIT_CRITICAL_SAFE(&tagMux_); }
#endif
namespace {
#ifdef __AVR__
const uint16_t crcTable[16] PROGMEM = {
#else
const uint16_t crcTable[16] = {
#endif
  0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
  0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef
};
uint16_t lookup(uint8_t i) {
#ifdef __AVR__
  return pgm_read_word(&crcTable[i]);
#else
  return crcTable[i];
#endif
}
bool littleEndian() { const uint16_t n = 1; return *reinterpret_cast<const uint8_t*>(&n) == 1; }
Text propertyName(uint8_t p) {
  switch (p) {
    case Heartbeat: return LY_TEXT("Heartbeat");
    case ProtocolVersion: return LY_TEXT("Protocol Version");
    case CatalogRevision: return LY_TEXT("Catalog Revision");
    case PayloadLimit: return LY_TEXT("Payload Limit");
    case BatchLimit: return LY_TEXT("Batch Limit");
    default: return LY_TEXT("Initialization Status");
  }
}
uint8_t propertyType(uint8_t p) { return p == Heartbeat ? Bool : p == CatalogRevision ? UInt32 : UInt8; }
uint8_t propertyNameLength(uint8_t p) {
  switch(p) {
    case Heartbeat: return sizeof("Heartbeat")-1;
    case ProtocolVersion: return sizeof("Protocol Version")-1;
    case CatalogRevision: return sizeof("Catalog Revision")-1;
    case PayloadLimit: return sizeof("Payload Limit")-1;
    case BatchLimit: return sizeof("Batch Limit")-1;
    default: return sizeof("Initialization Status")-1;
  }
}
}

Device::Device() : transport_{0,0,0,0}, deviceName_(), revision_(1), lastRx_(0), tagCount_(0),
  init_(NoTransport), frozen_(false), active_(false), pendingBusy_(false), busyId_(0),
  rxSlot_(0), commandSlot_(1), rxLength_(0), rxCrc_(0xffff), rxOpen_(false),
  rxEscaped_(false), rxDiscard_(false), txLength_(0), txPosition_(0), txStage_(0),
  txCrc_(0xffff), crcPosition_(0), requestId_(0), phase_(Dispatch), itemCount_(0),
  itemIndex_(0), commandPosition_(0), copyText_(), copyLength_(0), copyPosition_(0),
  deviceNameLength_(0), work_{0,0,0,0,0} {}

uint8_t Device::textByte(Text t, uint8_t pos) {
#ifdef __AVR__
  if (t.flash) return pgm_read_byte(t.data + pos);
#endif
  return static_cast<uint8_t>(t.data[pos]);
}
bool Device::nameLength(Text text, uint8_t& length) {
  length = 0;
  if (!text.data) return false;
  while (length <= MaxName && textByte(text, length)) ++length;
  return length > 0 && length <= MaxName;
}
uint8_t Device::width(uint8_t type) {
  switch(type) {
    case Bool: case Char: case Int8: case UInt8: return 1;
    case Int16: case UInt16: return 2;
    case Int32: case UInt32: case Float32: return 4;
    default: return 0;
  }
}
uint16_t Device::crcByte(uint16_t crc, uint8_t b) {
  crc = static_cast<uint16_t>((crc << 4) ^ lookup(static_cast<uint8_t>((crc >> 12) ^ (b >> 4))));
  return static_cast<uint16_t>((crc << 4) ^ lookup(static_cast<uint8_t>((crc >> 12) ^ (b & 15))));
}
uint16_t Device::get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }
void Device::put16(uint8_t* p, uint16_t n) { p[0] = static_cast<uint8_t>(n); p[1] = static_cast<uint8_t>(n >> 8); }
void Device::put32(uint8_t* p, uint32_t n) { for(uint8_t i=0;i<4;++i) p[i]=static_cast<uint8_t>(n >> (8*i)); }

bool Device::Listen(const Transport& t, const char* name) { return Listen(t, Text(name)); }
bool Device::Listen(const Transport& t, Text name) {
  transport_ = t; deviceName_ = name; tagCount_ = 0; revision_ = 1;
  frozen_ = false; active_ = false; pendingBusy_ = false;
  rxSlot_ = 0; commandSlot_ = 1; resetInput(false);
  work_ = WorkReport{0,0,0,0,0};
  init_ = (!t.read || !t.tryWrite || !t.millis) ? NoTransport : InitOk;
  if (!nameLength(name, deviceNameLength_) && init_ == InitOk) init_ = InvalidName;
  // Never expose an invalid name, including when another initialization error exists.
  if (deviceNameLength_ > MaxName) deviceNameLength_ = 0;
  return Ready();
}
uint16_t Device::registerTag(volatile uint8_t* value, uint8_t type, Text name, Access access) {
  if (frozen_) { init_ = RegistryFrozen; return 0; }
  if (!Ready()) return 0;
  uint8_t length = 0;
  if (!nameLength(name,length) || (access != ReadOnly && access != ReadWrite)) { init_=InvalidName;return 0; }
  if (tagCount_ >= LIGHTYAK_MAX_TAGS) { init_=RegistryFull;return 0; }
  Tag& tag = tags_[tagCount_++];
  tag.value=value;tag.type=type;tag.name=name;tag.nameLength=length;tag.access=access;
  return tagCount_;
}
bool Device::SetCatalogRevision(uint32_t revision) {
  if (frozen_) { init_=RegistryFrozen;return false; }
  revision_=revision;return true;
}
void Device::resetInput(bool open) {
  rxLength_=0;rxCrc_=0xffff;rxOpen_=open;rxEscaped_=false;rxDiscard_=false;
}
void Device::scheduleBusy(uint8_t id) { if (!pendingBusy_) { pendingBusy_=true;busyId_=id; } }
void Device::finishInput() {
  if (rxDiscard_ || rxEscaped_ || rxLength_ < HeaderSize+2) return;
  const uint8_t* f=rx_[rxSlot_];
  if (f[4] > MaxPayload || rxLength_ != HeaderSize+f[4]+2) return;
  if (rxCrc_ != get16(f+HeaderSize+f[4])) return;
  if (active_ || pendingBusy_) { scheduleBusy(f[3]);return; }
  commandSlot_=rxSlot_;rxSlot_^=1;
  requestId_=f[3];active_=true;phase_=Dispatch;
}
void Device::input(uint8_t b, uint32_t now) {
  if (b == 0x7e) {
    if (rxOpen_ && rxLength_) finishInput();
    resetInput(true);lastRx_=now;return;
  }
  if (!rxOpen_ || rxDiscard_) return;
  lastRx_=now;
  if (b == 0x7d && !rxEscaped_) { rxEscaped_=true;return; }
  if (rxEscaped_) {
    // Only canonical escaped delimiter/escape bytes are accepted.
    if (b != 0x5e && b != 0x5d) { rxDiscard_=true;return; }
    b ^= 0x20;rxEscaped_=false;
  }
  if (rxLength_ >= FrameSize) { rxDiscard_=true;return; }
  uint8_t* f=rx_[rxSlot_];f[rxLength_]=b;
  if (rxLength_ < HeaderSize || rxLength_ < HeaderSize+f[4]) rxCrc_=crcByte(rxCrc_,b);
  ++rxLength_;
  if (rxLength_ == HeaderSize && f[4] > MaxPayload) rxDiscard_=true;
}
void Device::startResponse(uint8_t op) {
  tx_[0]=Version;tx_[1]=op;tx_[2]=0;tx_[3]=requestId_;tx_[4]=0;
  txLength_=HeaderSize;txPosition_=0;txStage_=0;
}
void Device::sealResponse() {
  tx_[4]=txLength_-HeaderSize;txCrc_=0xffff;crcPosition_=0;phase_=Checksum;
}
void Device::fail(Status s) { startResponse(ErrorResponse);tx_[txLength_++]=s;sealResponse(); }

void Device::describe(uint16_t cursor) {
  if (cursor >= SystemProperties+tagCount_) { fail(BadCursor);return; }
  startResponse(Browse|ResponseBit);
  put16(tx_+txLength_, cursor+1 == SystemProperties+tagCount_ ? EndCursor : cursor+1);txLength_+=2;
  uint16_t ref=0;uint8_t property=0,type=0,access=0;
  if (cursor < SystemProperties) {
    property=static_cast<uint8_t>(cursor+1);type=propertyType(property);copyText_=propertyName(property);
    copyLength_=propertyNameLength(property);
  } else {
    ref=cursor-SystemProperties+1;
    const Tag& tag=tags_[ref-1];type=tag.type;access=tag.access;
    copyText_=tag.name;copyLength_=tag.nameLength;
  }
  put16(tx_+txLength_,ref);txLength_+=2;
  tx_[txLength_++]=property;tx_[txLength_++]=type;tx_[txLength_++]=access;
  tx_[txLength_++]=copyLength_;copyPosition_=0;phase_=CopyName;
}

Status Device::readValue(uint16_t ref, uint8_t prop, uint8_t& type, uint8_t* out, uint8_t& length, uint32_t now) {
  type=0;length=0;
  if (ref==0) {
    if (prop < Heartbeat || prop > InitializationStatus) return UnknownTarget;
    type=propertyType(prop);length=width(type);
    switch(prop) {
      case Heartbeat: out[0]=static_cast<uint8_t>((now/500UL)&1);break;
      case ProtocolVersion: out[0]=Version;break;
      case CatalogRevision: put32(out,revision_);break;
      case PayloadLimit: out[0]=MaxPayload;break;
      case BatchLimit: out[0]=MaxBatch;break;
      default: out[0]=init_;break;
    }
    return Ok;
  }
  if (ref>tagCount_ || prop!=Value) return UnknownTarget;
  const Tag& tag=tags_[ref-1];type=tag.type;length=width(type);
#if defined(ARDUINO_ARCH_ESP32)
  if (!TryLockTags()) return Deferred;
#endif
  LY_ATOMIC {
    for(uint8_t i=0;i<length;++i) out[littleEndian()?i:length-1-i]=tag.value[i];
  }
#if defined(ARDUINO_ARCH_ESP32)
  UnlockTags();
#endif
  ++work_.tagCopies;return Ok;
}
Status Device::writeValue(uint16_t ref, uint8_t prop, uint8_t type, const uint8_t* data, uint8_t length) {
  if(ref==0) return prop>=Heartbeat && prop<=InitializationStatus ? NotWritable : UnknownTarget;
  if(ref>tagCount_ || prop!=Value) return UnknownTarget;
  Tag& tag=tags_[ref-1];
  if(tag.access!=ReadWrite) return NotWritable;
  if(type!=tag.type || !width(type) || length!=width(type)) return TypeMismatch;
  if(type==Bool && data[0]>1) return BadValue;
#if defined(ARDUINO_ARCH_ESP32)
  if (!TryLockTags()) return Deferred;
#endif
  LY_ATOMIC {
    for(uint8_t i=0;i<length;++i) tag.value[i]=data[littleEndian()?i:length-1-i];
  }
#if defined(ARDUINO_ARCH_ESP32)
  UnlockTags();
#endif
  ++work_.tagCopies;return Ok;
}

void Device::advance(uint32_t now) {
  ++work_.actions;
  const uint8_t* command=rx_[commandSlot_];
  const uint8_t* p=command+HeaderSize;
  const uint8_t length=command[4];
  switch(phase_) {
    case Dispatch:
      if(command[0]!=Version) { fail(Unsupported);break; }
      if(command[2]!=0) { fail(BadRequest);break; }
      if(command[1]!=Identify && !Ready()) { fail(NotReady);break; }
      switch(command[1]) {
        case Identify:
          if(length!=0) { fail(BadRequest);break; }
          startResponse(Identify|ResponseBit);
          tx_[txLength_++]=init_;put16(tx_+txLength_,tagCount_);txLength_+=2;
          put32(tx_+txLength_,revision_);txLength_+=4;
          tx_[txLength_++]=MaxPayload;tx_[txLength_++]=MaxBatch;
          tx_[txLength_++]=LIGHTYAK_MAX_TAGS;tx_[txLength_++]=deviceNameLength_;
          copyText_=deviceName_;copyLength_=deviceNameLength_;copyPosition_=0;phase_=CopyName;break;
        case Browse:
          if(length!=2) { fail(BadRequest);break; }
          describe(get16(p));break;
        case Read: case Write:
          if(length<1 || p[0]==0 || p[0]>MaxBatch) { fail(BadRequest);break; }
          itemCount_=p[0];itemIndex_=0;commandPosition_=1;
          if(command[1]==Read) {
            if(length!=1+3*itemCount_) { fail(BadRequest);break; }
            startResponse(Read|ResponseBit);tx_[txLength_++]=itemCount_;phase_=ExecuteRead;
          } else phase_=ValidateWrites;
          break;
        default: fail(Unsupported);break;
      }
      break;
    case ValidateWrites: {
      // Validate ALL item boundaries before applying even the first value.
      if(commandPosition_+5>length) { fail(BadRequest);break; }
      uint8_t n=p[commandPosition_+4];
      if(n>4 || commandPosition_+5+n>length) { fail(BadRequest);break; }
      commandPosition_=static_cast<uint8_t>(commandPosition_+5+n);
      if(++itemIndex_==itemCount_) {
        if(commandPosition_!=length) { fail(BadRequest);break; }
        itemIndex_=0;commandPosition_=1;
        startResponse(Write|ResponseBit);tx_[txLength_++]=itemCount_;phase_=ExecuteWrite;
      }
      break;
    }
    case ExecuteRead: {
      uint16_t ref=get16(p+commandPosition_);uint8_t prop=p[commandPosition_+2];
      uint8_t type=0,n=0,value[4]={0,0,0,0};
      Status status=readValue(ref,prop,type,value,n,now);
#if defined(ARDUINO_ARCH_ESP32)
      if (status==Deferred) break;
#endif
      put16(tx_+txLength_,ref);txLength_+=2;tx_[txLength_++]=prop;
      tx_[txLength_++]=status;tx_[txLength_++]=type;tx_[txLength_++]=n;
      for(uint8_t i=0;i<n;++i) tx_[txLength_++]=value[i];
      commandPosition_+=3;
      if(++itemIndex_==itemCount_) sealResponse();
      break;
    }
    case ExecuteWrite: {
      const uint8_t* item=p+commandPosition_;
      uint16_t ref=get16(item);uint8_t prop=item[2],n=item[4];
      Status status=writeValue(ref,prop,item[3],item+5,n);
#if defined(ARDUINO_ARCH_ESP32)
      if (status==Deferred) break;
#endif
      put16(tx_+txLength_,ref);txLength_+=2;tx_[txLength_++]=prop;tx_[txLength_++]=status;
      commandPosition_=static_cast<uint8_t>(commandPosition_+5+n);
      if(++itemIndex_==itemCount_) sealResponse();
      break;
    }
    case CopyName:
      for(uint8_t i=0;i<LIGHTYAK_PREP_BUDGET && copyPosition_<copyLength_;++i) {
        tx_[txLength_++]=textByte(copyText_,copyPosition_++);++work_.prepared;
      }
      if(copyPosition_==copyLength_) sealResponse();
      break;
    case Checksum:
      for(uint8_t i=0;i<LIGHTYAK_PREP_BUDGET && crcPosition_<txLength_;++i) {
        txCrc_=crcByte(txCrc_,tx_[crcPosition_++]);++work_.prepared;
      }
      if(crcPosition_==txLength_) {
        put16(tx_+txLength_,txCrc_);txLength_+=2;txPosition_=0;txStage_=0;phase_=Send;
      }
      break;
    case Send: transmit();break;
  }
}

void Device::transmit() {
  // Stage 0 opening delimiter; 1 data; 2 escaped second byte; 3 closing delimiter.
  for(uint8_t i=0;i<LIGHTYAK_TX_BUDGET;++i) {
    uint8_t b=0x7e;
    if(txStage_==1) b=tx_[txPosition_];
    else if(txStage_==2) b=tx_[txPosition_]^0x20;
    if(txStage_==1 && (b==0x7e || b==0x7d)) b=0x7d;
    if(!transport_.tryWrite(transport_.context,b)) return;
    ++work_.transmitted;
    if(txStage_==0) txStage_=1;
    else if(txStage_==3) { active_=false;return; }
    else if(txStage_==1 && b==0x7d) txStage_=2;
    else { ++txPosition_;txStage_=txPosition_==txLength_ ? 3 : 1; }
  }
}

void Device::Update() {
  work_=WorkReport{0,0,0,0,0};frozen_=true;
  if(!transport_.read || !transport_.tryWrite || !transport_.millis) return;
  // One empty read is the idle fast path: no clock query, registry scan, or work queue.
  int b=transport_.read(transport_.context);
  if(b<0 && !active_ && !pendingBusy_ && rxLength_==0 && !rxEscaped_ && !rxDiscard_) return;
  const uint32_t now=transport_.millis(transport_.context);
  if((rxLength_ || rxEscaped_ || rxDiscard_) && static_cast<uint32_t>(now-lastRx_)>=FrameTimeoutMs) resetInput(false);
  for(uint8_t i=0;i<LIGHTYAK_RX_BUDGET && b>=0;++i) {
    input(static_cast<uint8_t>(b),now);++work_.received;
    if(i+1<LIGHTYAK_RX_BUDGET) b=transport_.read(transport_.context);
  }
  if(!active_ && pendingBusy_) {
    pendingBusy_=false;active_=true;requestId_=busyId_;fail(Busy);
    // A response is queued; its checksum starts on the next call.
    work_.actions=1;return;
  }
  if(active_) advance(now);
}
} // namespace lightyak
