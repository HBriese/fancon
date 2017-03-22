#include "Fan.hpp"

using namespace fancon;

Fan::Fan(const UID &fanUID, const FanConfig &conf, bool dynamic)
    : FanInterface(fanUID, conf, dynamic, 2) {
  FanPaths p(fanUID);

  // keep paths (with changing values) for future rw
  pwm_p = p.pwm_p;
  rpm_p = p.rpm_p;
  enable_pf = p.enable_pf;

  // Correct driver enable mode if it is not default
  int em;
  if ((em = readEnableMode()) > driver_enable_mode)
    driver_enable_mode = em;

  if (!points.empty()) {
    // set manual mode
    writeEnableMode(manual_enable_mode);
    if (readRPM() <= 0)  // start fan if stopped
      writePWM(pwm_start);
  }
}

bool Fan::recoverControl(int pwm) {
  // Attempt to regain manual fan control, sleeping between attempts
  for (int i = 0; i < 4; ++i, sleep(1)) {
    writeEnableMode(manual_enable_mode);
    if (write(pwm_p, pwm))
      return true;
  }

  return false;
}

int Fan::testPWM(int rpm) {
  // assume the calculation is the real pwm for a starting point
  auto nextPWM(calcPWM(rpm));

  writePWM(nextPWM);
  sleep(speed_change_t * 3);

  int curPWM = 0, lastPWM = -1, curRPM;
  // break when RPM found when matching, or if between the 5 PWM margin
  while ((curRPM = readRPM()) != rpm && nextPWM != lastPWM) {
    lastPWM = curPWM;
    curPWM = nextPWM;
    nextPWM = (curRPM < rpm) ? nextPWM + 2 : nextPWM - 2;
    if (nextPWM < 0)
      nextPWM = calcPWM(rpm);
    if (curRPM <= 0)
      nextPWM = pwm_start;

    writePWM(nextPWM);
    sleep(speed_change_t);
  }

  return nextPWM;
}