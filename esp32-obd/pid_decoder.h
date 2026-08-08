#pragma once

#include <Arduino.h>

#include "telemetry_field_mask.h"

namespace obd {
namespace core {

struct PidDescriptor {
  uint32_t mask_bit;
  uint8_t pid;
  const char* json_key;
};

// Catalogue of standard OBD-II PIDs the firmware can decode.
extern const PidDescriptor kPidCatalogue[];
extern const size_t kPidCatalogueLen;

class PidDecoder {
 public:
  // Returns a pre-formatted JSON value (number) for a known PID, given the
  // raw payload bytes (A, B, ...) returned by the ECU.
  // Returns empty string if the PID is unknown or the payload is too short.
  String FormatValue(uint8_t pid, const uint8_t* payload, uint8_t payload_len) const;
};

}  // namespace core
}  // namespace obd
