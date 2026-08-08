#include "../../include/services/diagnostics_service.h"

namespace obd {
namespace services {

DiagnosticsService::DiagnosticsService(core::DtcDecoder& dtc_decoder,
                                       app_link::TransportBle& app_link,
                                       safety::SafetyGuard& safety_guard)
    : dtc_decoder_(dtc_decoder),
      app_link_(app_link),
      safety_guard_(safety_guard) {}

void DiagnosticsService::ReadDtc() {
  const String code = dtc_decoder_.DecodeOrUnknown("P0130");
  app_link_.NotifyResponse("{\"type\":\"dtc\",\"codes\":[\"" + code +
                           "\"],\"ts\":" + String(millis()) + "}");
}

void DiagnosticsService::ClearDtc() {
  if (!safety_guard_.IsClearDtcAllowed()) {
    app_link_.NotifyResponse(
        "{\"type\":\"ack\",\"command\":\"clear_dtc\",\"status\":\"locked\","
        "\"reason\":\"safety_lock\",\"ts\":" +
        String(millis()) + "}");
    return;
  }
  app_link_.NotifyResponse(
      "{\"type\":\"ack\",\"command\":\"clear_dtc\",\"status\":\"ok\",\"ts\":" +
      String(millis()) + "}");
}

}  // namespace services
}  // namespace obd
