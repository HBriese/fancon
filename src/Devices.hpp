#ifndef FANCON_DEVICES_HPP
#define FANCON_DEVICES_HPP

#include <sensors/sensors.h>

#include "FanDell.hpp"
#include "FanInterface.hpp"
#include "FanSysfs.hpp"
#include "NvidiaDevices.hpp"
#include "SensorInterface.hpp"
#include "SensorSysfs.hpp"
#include "Util.hpp"
#include "proto/DevicesSpec.pb.h"

using std::find_if;
using std::make_move_iterator;
using std::set;

namespace fc {
class SensorChips {
public:
  SensorChips();
  ~SensorChips();

  tuple<std::vector<unique_ptr<fc::FanInterface>>, SensorMap> enumerate();

private:
  vector<const sensors_chip_name *> chips;
};

class Devices {
public:
  Devices() = default;
  explicit Devices(bool dry_run);

  vector<unique_ptr<FanInterface>> fans;
  SensorMap sensor_map;

  void from(const fc_pb::Devices &d);
  void to(fc_pb::Devices &d) const;
};

bool operator==(const fc_pb::Fan &l, const fc_pb::Fan &r);
bool operator==(const fc_pb::Sensor &l, const fc_pb::Sensor &r);
} // namespace fc

#endif // FANCON_DEVICES_HPP
