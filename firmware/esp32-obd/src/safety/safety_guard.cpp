#include "../../include/safety/safety_guard.h"

namespace obd {
namespace safety {

bool SafetyGuard::IsReadOnlyEnabled() const { return kReadOnlyDefault; }

bool SafetyGuard::IsClearDtcAllowed() const { return kAllowClearDtcDefault; }

}  // namespace safety
}  // namespace obd
