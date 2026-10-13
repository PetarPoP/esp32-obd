#pragma once

#include <Arduino.h>

#include "pid_decoder.h"

namespace obd {
namespace core {

// Telemetry field selection: a set of indices into kPidCatalogue.

// The fields polled when start_stream names no metrics (the original four,
// so the update rate of existing apps does not drop).
void DefaultTelemetryFields(BitSet256& out_fields);

// Parses optional "metrics":["rpm",...] from command JSON. "all" selects every
// catalogue field. Falls back to the default fields if absent or nothing matches.
void ParseTelemetryFields(const String& raw_json, BitSet256& out_fields);

}  // namespace core
}  // namespace obd
