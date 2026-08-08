#include "dtc_decoder.h"

namespace obd {
namespace core {

String DtcDecoder::DecodeOrUnknown(const String& raw_code) const {
  if (raw_code.length() == 0) {
    return "unknown_code";
  }
  return raw_code;
}

}  // namespace core
}  // namespace obd
