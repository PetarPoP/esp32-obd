#pragma once

#include <Arduino.h>

namespace obd {
namespace core {

enum PidFieldFlags : uint8_t {
  kFieldSigned = 1 << 0,     // raw value is two's complement
  kFieldNaAllOnes = 1 << 1,  // all-ones raw value means "not available"
};

// One decoded value of a Mode 01 PID (a PID may yield several fields, e.g.
// an oxygen sensor's voltage and fuel trim). The value is
//   ((big-endian bytes [offset, offset+size) >> shift) & mask) * scale + bias
// and is formatted with `decimals` digits after the point.
struct PidField {
  uint8_t pid;
  const char* json_key;
  uint8_t offset;
  uint8_t size;
  uint8_t shift;
  uint8_t mask;  // 0 = whole value
  float scale;
  float bias;
  uint8_t decimals;
  uint8_t flags;
};

// Every SAE J1979 Mode 01 value the firmware can decode, sorted by PID.
extern const PidField kPidCatalogue[];
extern const size_t kPidCatalogueLen;

// Number of data bytes a PID returns (without the PID byte), or 0 when it is
// not known with certainty. Only PIDs with a known length can be combined in
// one multi-PID request, because the answer has no separators.
uint8_t PidDataLength(uint8_t pid);

// Fixed-size bit set, used for catalogue indices and for PID numbers.
struct BitSet256 {
  uint32_t words[8] = {0};
  void Clear() { memset(words, 0, sizeof(words)); }
  void Set(uint8_t i) { words[i / 32] |= 1u << (i % 32); }
  bool Has(uint8_t i) const { return (words[i / 32] >> (i % 32)) & 1u; }
  bool Empty() const {
    for (uint32_t w : words) {
      if (w != 0) return false;
    }
    return true;
  }
};

class PidDecoder {
 public:
  // Returns the JSON number for one field, given the PID's data bytes
  // (A, B, ...). Empty string if the payload is too short or not available.
  String FormatField(const PidField& field, const uint8_t* payload, uint8_t payload_len) const;
};

}  // namespace core
}  // namespace obd
