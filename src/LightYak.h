// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_H
#define LIGHTYAK_H

#include <stdint.h>
#include <stddef.h>
#include <float.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif
#ifdef __AVR__
#include <avr/pgmspace.h>
#endif

// Override consistently for ALL translation units (compiler -D flags).
#ifndef LIGHTYAK_MAX_TAGS
#define LIGHTYAK_MAX_TAGS 24
#endif
#ifndef LIGHTYAK_RX_BUDGET
#define LIGHTYAK_RX_BUDGET 4
#endif
#ifndef LIGHTYAK_TX_BUDGET
#define LIGHTYAK_TX_BUDGET 4
#endif
#ifndef LIGHTYAK_PREP_BUDGET
#define LIGHTYAK_PREP_BUDGET 4
#endif

namespace lightyak {
enum Access : uint8_t { ReadOnly = 0, ReadWrite = 1 };
enum Type : uint8_t { Bool = 1, Char = 2, Int8 = 3, UInt8 = 4,
  Int16 = 5, UInt16 = 6, Int32 = 7, UInt32 = 8, Float32 = 9 };
enum Status : uint8_t { Ok = 0, BadRequest = 1, UnknownTarget = 2,
  NotWritable = 3, TypeMismatch = 4, BadValue = 5, Busy = 6,
  Unsupported = 7, NotReady = 8, BadCursor = 9 };
enum InitError : uint8_t { InitOk = 0, NoTransport = 1, InvalidName = 2,
  RegistryFull = 3, RegistryFrozen = 4 };
enum Operation : uint8_t { Identify = 1, Browse = 2, Read = 3, Write = 4,
  ResponseBit = 0x80, ErrorResponse = 0xff };
enum Property : uint8_t { Value = 0, Heartbeat = 1, ProtocolVersion = 2,
  CatalogRevision = 3, PayloadLimit = 4, BatchLimit = 5, InitializationStatus = 6 };

static const uint8_t Version = 1;
static const uint8_t MaxPayload = 64;
static const uint8_t MaxBatch = 6;
static const uint8_t MaxName = 24;
static const uint8_t SystemProperties = 6;
static const uint16_t FrameTimeoutMs = 250;
static const uint16_t EndCursor = 0xffff;

struct Text {
  const char* data;
  bool flash;
  explicit Text(const char* p = 0, bool f = false) : data(p), flash(f) {}
};

// Adapters must be nonblocking, bounded, and exclusively owned by LightYak.
// read(): -1 when empty. tryWrite(): false when full, with no byte consumed.
struct Transport {
  void* context;
  int (*read)(void*);
  bool (*tryWrite)(void*, uint8_t);
  uint32_t (*millis)(void*);
};

struct WorkReport {
  uint8_t received;
  uint8_t transmitted;
  uint8_t prepared; // metadata copy OR outgoing CRC bytes, not both
  uint8_t actions;  // at most one state-machine action
  uint8_t tagCopies; // at most one scalar copy, maximum four bytes
};

template<typename T> struct Scalar { static const bool supported = false; static const uint8_t type = 0; };
#define LIGHTYAK_SCALAR(cpp, wire, valid) template<> struct Scalar<cpp> { static const bool supported = valid; static const uint8_t type = wire; }
LIGHTYAK_SCALAR(bool, Bool, sizeof(bool) == 1);
LIGHTYAK_SCALAR(char, Char, sizeof(char) == 1);
LIGHTYAK_SCALAR(signed char, Int8, sizeof(signed char) == 1);
LIGHTYAK_SCALAR(unsigned char, UInt8, sizeof(unsigned char) == 1);
LIGHTYAK_SCALAR(short, sizeof(short) == 2 ? Int16 : Int32, sizeof(short) == 2 || sizeof(short) == 4);
LIGHTYAK_SCALAR(unsigned short, sizeof(unsigned short) == 2 ? UInt16 : UInt32, sizeof(unsigned short) == 2 || sizeof(unsigned short) == 4);
LIGHTYAK_SCALAR(int, sizeof(int) == 2 ? Int16 : Int32, sizeof(int) == 2 || sizeof(int) == 4);
LIGHTYAK_SCALAR(unsigned int, sizeof(unsigned int) == 2 ? UInt16 : UInt32, sizeof(unsigned int) == 2 || sizeof(unsigned int) == 4);
LIGHTYAK_SCALAR(long, Int32, sizeof(long) == 4);
LIGHTYAK_SCALAR(unsigned long, UInt32, sizeof(unsigned long) == 4);
LIGHTYAK_SCALAR(float, Float32, sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128);
LIGHTYAK_SCALAR(double, Float32, sizeof(double) == 4 && DBL_MANT_DIG == 24 && DBL_MAX_EXP == 128);
#undef LIGHTYAK_SCALAR
template<typename T> struct Unvolatile { typedef T Type; };
template<typename T> struct Unvolatile<volatile T> { typedef T Type; };

class Device {
public:
  Device();
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  bool Listen(const Transport& transport, const char* deviceName);
  bool Listen(const Transport& transport, Text deviceName);
#ifdef ARDUINO
  template<typename Port> bool Listen(Port& port, unsigned long baud, uint8_t config, const char* name) {
    port.begin(baud, config);
    return listenPort(port, Text(name));
  }
  template<typename Port> bool Listen(Port& port, unsigned long baud, uint8_t config, const __FlashStringHelper* name) {
    port.begin(baud, config);
    return listenPort(port, Text(reinterpret_cast<const char*>(name), true));
  }
#endif
  template<typename T> uint16_t AddTag(T& variable, const char* name, Access access = ReadOnly) {
    return addScalar(variable, Text(name), access);
  }
#ifdef ARDUINO
  template<typename T> uint16_t AddTag(T& variable, const __FlashStringHelper* name, Access access = ReadOnly) {
    return addScalar(variable, Text(reinterpret_cast<const char*>(name), true), access);
  }
#endif
  bool SetCatalogRevision(uint32_t revision);
  void Update();
  bool Ready() const { return init_ == InitOk; }
  InitError LastError() const { return init_; }
  uint16_t TagCount() const { return tagCount_; }
  const WorkReport& LastWork() const { return work_; }

private:
  static const uint8_t HeaderSize = 5;
  static const uint8_t FrameSize = HeaderSize + MaxPayload + 2;
  struct Tag { volatile uint8_t* value; Text name; uint8_t nameLength; uint8_t type; Access access; };
  enum Phase : uint8_t { Dispatch, ValidateWrites, ExecuteRead, ExecuteWrite, CopyName, Checksum, Send };
  template<typename T> uint16_t addScalar(T& variable, Text name, Access access) {
    typedef typename Unvolatile<T>::Type Base;
    static_assert(Scalar<Base>::supported, "LightYak supports mutable bool/char/8-32-bit integers/IEEE binary32 only; no strings or arrays");
    return registerTag(reinterpret_cast<volatile uint8_t*>(&variable), Scalar<Base>::type, name, access);
  }
#ifdef ARDUINO
  template<typename Port> struct SerialAdapter {
    static int read(void* p) { return static_cast<Port*>(p)->read(); }
    static bool write(void* p, uint8_t b) {
      Port* port = static_cast<Port*>(p);
      if (port->availableForWrite() <= 0) return false;
      return port->write(b) == 1;
    }
    static uint32_t clock(void*) { return ::millis(); }
  };
  template<typename Port> bool listenPort(Port& port, Text name) {
    Transport t = { &port, SerialAdapter<Port>::read, SerialAdapter<Port>::write, SerialAdapter<Port>::clock };
    return Listen(t, name);
  }
#endif
  uint16_t registerTag(volatile uint8_t*, uint8_t, Text, Access);
  static uint8_t textByte(Text, uint8_t);
  static bool nameLength(Text, uint8_t&);
  static uint8_t width(uint8_t);
  static uint16_t crcByte(uint16_t, uint8_t);
  static uint16_t get16(const uint8_t*);
  static void put16(uint8_t*, uint16_t);
  static void put32(uint8_t*, uint32_t);
  void input(uint8_t, uint32_t);
  void finishInput();
  void resetInput(bool open);
  void advance(uint32_t);
  void startResponse(uint8_t operation);
  void sealResponse();
  void fail(Status);
  void scheduleBusy(uint8_t);
  void describe(uint16_t);
  Status readValue(uint16_t, uint8_t, uint8_t&, uint8_t*, uint8_t&, uint32_t);
  Status writeValue(uint16_t, uint8_t, uint8_t, const uint8_t*, uint8_t);
  void transmit();

  Transport transport_;
  Tag tags_[LIGHTYAK_MAX_TAGS];
  Text deviceName_;
  uint32_t revision_;
  uint32_t lastRx_;
  uint16_t tagCount_;
  InitError init_;
  bool frozen_;
  bool active_;
  bool pendingBusy_;
  uint8_t busyId_;
  uint8_t rx_[2][FrameSize];
  uint8_t rxSlot_;
  uint8_t commandSlot_;
  uint8_t rxLength_;
  uint16_t rxCrc_;
  bool rxOpen_;
  bool rxEscaped_;
  bool rxDiscard_;
  uint8_t tx_[FrameSize];
  uint8_t txLength_;
  uint8_t txPosition_;
  uint8_t txStage_;
  uint16_t txCrc_;
  uint8_t crcPosition_;
  uint8_t requestId_;
  Phase phase_;
  uint8_t itemCount_;
  uint8_t itemIndex_;
  uint8_t commandPosition_;
  Text copyText_;
  uint8_t copyLength_;
  uint8_t copyPosition_;
  uint8_t deviceNameLength_;
  WorkReport work_;
};
static_assert(LIGHTYAK_MAX_TAGS > 0 && LIGHTYAK_MAX_TAGS <= 255, "tag capacity must be 1..255");
static_assert(LIGHTYAK_RX_BUDGET > 0 && LIGHTYAK_RX_BUDGET <= 32, "RX budget must be 1..32");
static_assert(LIGHTYAK_TX_BUDGET > 0 && LIGHTYAK_TX_BUDGET <= 32, "TX budget must be 1..32");
static_assert(LIGHTYAK_PREP_BUDGET > 0 && LIGHTYAK_PREP_BUDGET <= 16, "prepare budget must be 1..16");
} // namespace lightyak

extern lightyak::Device LightYak;
#endif
