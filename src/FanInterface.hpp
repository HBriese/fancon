#ifndef FANCON_FANINTERFACE_HPP
#define FANCON_FANINTERFACE_HPP

#include <chrono>
#include <sstream>    // stringstream
#include <functional>   // function
#include "Util.hpp"
#include "UID.hpp"
#include "Config.hpp"

using std::stringstream;
using std::function;
using fancon::UID;
using fancon::FanConfig;
using fancon::Util::read;
using fancon::Util::write;
using fancon::Util::getPath;
using fancon::Util::speed_change_t;
using fancon::Util::pwm_max_absolute;

namespace chrono = std::chrono;

namespace fancon {
struct FanTestResult {
  FanTestResult() { canTest = false; }
  FanTestResult(int rpm_min, int rpm_max, int pwm_min, int pwm_max, int pwm_start, long stop_time, double slope)
      : rpm_min(rpm_min), rpm_max(rpm_max), pwm_min(pwm_min), pwm_max(pwm_max), pwm_start(pwm_start),
        stop_time(stop_time), slope(slope) {}

  int rpm_min,
      rpm_max,
      pwm_min,
      pwm_max,
      pwm_start;
  long stop_time;
  double slope;

  bool canTest = true;

  bool testable() { return canTest; }

  bool valid() {
    return (rpm_min > 0) && (rpm_min < rpm_max) && (pwm_min > 0) && (pwm_max <= 255)
        && (pwm_min < pwm_max) && (pwm_start > 0) && (slope > 0);
  }
};

class FanInterface {
public:
  FanInterface(const UID &uid, const FanConfig &conf, bool dynamic, int driverEnableMode, int manualEnableMode = 1);

  bool tested = false;   // characteristic variables written
  int rpm_min, rpm_max,
      pwm_min, pwm_start;
  vector<fancon::Point> points;

  virtual int readRPM() = 0;
  virtual int readPWM() = 0;
  virtual void writePWM(int pwm) = 0;
  virtual void writeEnableMode(int mode) = 0;

  void verifyPoints(const UID &fanUID);

  inline int calcPWM(int rpm) { return (int) (((rpm - rpm_min) / slope) + pwm_min); }
  virtual void update(int temp);

  FanTestResult test();
  static void writeTestResult(const UID &uid, const FanTestResult &result, DeviceType devType);

protected:
  int driver_enable_mode;
  const int manual_enable_mode;
  const int hwID;
  const string hwIDStr;

  long stop_t;
  double slope;   // i.e. rpm-per-pwm
  bool dynamic;

  int getMaxRPM();
  int getMaxPWM(const int rpm_max);
  long getStopTime();
  int getPWMStart();
  std::pair<int, int> getPWMRPMMin(const int startPWM);
};

struct FanPaths {
  FanPaths(const UID &uid, DeviceType devType = FAN);

  DeviceType dev_type;

  string pwm_p, rpm_p, enable_pf;
  string pwm_min_pf, pwm_max_pf,
      rpm_min_pf, rpm_max_pf,
      pwm_start_pf, slope_pf,
      stop_t_pf;
  string hwID;

  bool tested() const;
};
}

#endif //FANCON_FANINTERFACE_HPP
