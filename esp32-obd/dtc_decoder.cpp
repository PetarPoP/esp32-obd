#include "dtc_decoder.h"

namespace obd {
namespace core {

String DtcDecoder::DecodeOrUnknown(const String& raw_code) const {
  if (raw_code.length() == 0) {
    return "unknown_code";
  }
  return raw_code;
}

String DtcDecoder::Format(uint8_t high, uint8_t low) {
  if (high == 0 && low == 0) {
    return String();
  }
  static const char kSystem[] = {'P', 'C', 'B', 'U'};
  static const char kHex[] = "0123456789ABCDEF";
  char code[6];
  code[0] = kSystem[high >> 6];
  code[1] = kHex[(high >> 4) & 0x03];
  code[2] = kHex[high & 0x0F];
  code[3] = kHex[low >> 4];
  code[4] = kHex[low & 0x0F];
  code[5] = 0;
  return String(code);
}

}  // namespace core
}  // namespace obd
