#include "../../include/drivers/can_phy_driver.h"

namespace obd {
namespace drivers {

bool CanPhyDriver::Begin() {
  // MrDIY shield PHY setup hook; kept simple for MVP skeleton.
  return true;
}

}  // namespace drivers
}  // namespace obd
