#include "SensorParentInterface.hpp"

using namespace fancon;

SensorParentInterface::SensorParentInterface(SensorParentInterface &&other) : temp(other.temp) {
  for (auto &fp : fans)
    fans.emplace_back(move(fp));
  fans.clear();
}

bool SensorParentInterface::update() {
  int newTemp = read();

  if (newTemp != temp) {
    temp = newTemp;
    return true;
  }
  return false;
}

bool SensorParent::operator==(const UID &uid) const {
  auto endIt = (find(ts_path.rbegin(), ts_path.rend(), '_') + 1).base();
  return uid.getBasePath() == string(ts_path.begin(), endIt);
}