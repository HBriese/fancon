#include "Config.hpp"

using namespace fancon;

ostream &fancon::operator<<(ostream &os, const fancon::ControllerConfig &c) {
  os << c.update_prefix << c.update_time.count() << ' ' << c.threads_prefix << c.threads
     << ' ' << c.dynamic_prefix << ((c.dynamic) ? "true" : "false");
  return os;
}

istream &fancon::operator>>(istream &is, fancon::ControllerConfig &c) {
  // Read whole line from istream
  std::istreambuf_iterator<char> eos;
  string in(std::istreambuf_iterator<char>(is), eos);

  struct InputValue {
    InputValue(string &input, const string &sep, std::function<bool(const char &ch)> predicate) {
      beg = search(input.begin(), input.end(), sep.begin(), sep.end());
      if (notEqualTo({beg}, input.end()))  // Forward iterator to the last character of sep
        beg += sep.size();

      end = find_if(beg, input.end(), predicate);
      found = beg != input.end() && beg != end;
    }

    string::iterator beg, end;
    bool found;
  };

  InputValue dynamicVal(in, c.dynamic_prefix, [](const char &ch) { return !std::isalpha(ch); });
  InputValue updateVal(in, c.update_prefix, [](const char &ch) { return !std::isdigit(ch); });
  InputValue threadsVal(in, c.threads_prefix, [](const char &ch) { return !std::isdigit(ch); });

  // Fail if no values are found
  if (!dynamicVal.found && !updateVal.found && !threadsVal.found) {
    c.update_time = seconds(0);
    return is;
  }

  if (dynamicVal.found) {
    string dynamicStr(dynamicVal.beg, dynamicVal.end);
    c.dynamic = (dynamicStr != "false" || dynamicStr != "0");
  }

  if (updateVal.found) {
    string updateStr(updateVal.beg, updateVal.end);
    c.update_time = (isNum(updateStr)) ? seconds(std::stoi(updateStr)) : c.update_time;
  }

  if (threadsVal.found) {
    string threadsStr(threadsVal.beg, threadsVal.end);
    uint res = (isNum(threadsStr)) ? (uint) std::stoul(threadsStr) : c.threads;
    c.threads = (res <= std::thread::hardware_concurrency() && res > 0) ? res : c.threads;
  }

  return is;
}

Point &fancon::Point::operator=(const Point &other) {
  temp = other.temp;
  rpm = other.rpm;
  pwm = other.pwm;

  return *this;
}

ostream &fancon::operator<<(ostream &os, const Point &p) {
  string rpm_out = (p.rpm != -1) ? string() + p.rpm_bsep + to_string(p.rpm) : string();
  string pwm_out = (p.pwm != -1) ? string() + p.pwm_bsep + to_string(p.pwm) : string();
  os << p.temp_bsep << p.temp << rpm_out << pwm_out << p.esep;
  return os;
}

istream &fancon::operator>>(istream &is, Point &p) {
  string in;
  is >> in;
  if (in.empty())
    return is;
  std::remove_if(in.begin(), in.end(), [](auto &c) { return isspace(c); });

  string::iterator rpmSepIt, pwmSepIt;
  bool rpmSepFound = (rpmSepIt = find(in.begin(), in.end(), p.rpm_bsep)) != in.end();
  bool pwmSepFound = (pwmSepIt = find(in.begin(), in.end(), p.pwm_bsep)) != in.end();

  auto tempBegIt = find(in.begin(), in.end(), p.temp_bsep) + 1;
  auto tempAbsEndIt = (rpmSepFound) ? rpmSepIt : pwmSepIt;
  string::iterator tempEndIt = find_if(tempBegIt, tempAbsEndIt, [](const char &c) { return !isdigit(c); });

  auto rpmBegIt = rpmSepIt + 1;
  auto rpmEndIt = find(rpmBegIt, in.end(), (pwmSepFound) ? p.pwm_bsep : p.esep);

  auto pwmBegIt = pwmSepIt + 1;
  auto pwmEndIt = find(pwmBegIt, in.end(), p.esep);

  auto tempFound = notEqualTo({tempBegIt, tempEndIt}, in.end());
  bool rpmFound = notEqualTo({rpmBegIt, rpmEndIt}, in.end());
  bool pwmFound = notEqualTo({pwmBegIt, pwmEndIt}, in.end());

  // Must contain temp, and either a rpm or pwm value
  if (!tempFound || (!rpmFound & !pwmFound)) {
    LOG(llvl::error) << "Invalid fan config: " << in;
    return is;
  }

  if (tempFound) {
    string tempStr(tempBegIt, tempEndIt);
    p.temp = (isNum(tempStr)) ? stoi(tempStr) : 0;
    // Convert to celsius if fahrenheit flag given
    if (std::tolower(*tempEndIt) == p.fahrenheit)
      p.temp = static_cast<int>((p.temp - 32) / 1.8);
  }

  if (rpmFound) {
    string rpmStr(rpmBegIt, rpmEndIt);
    p.rpm = (isNum(rpmStr)) ? stoi(rpmStr) : -1;
  }

  if (pwmFound) {
    string pwmStr(pwmBegIt, pwmEndIt);
    p.pwm = (isNum(pwmStr)) ? stoi(pwmStr) : -1;
  }

  return is;
}

ostream &fancon::operator<<(ostream &os, const FanConfig &c) {
  for (auto p : c.points)
    os << p;

  return os;
}

istream &fancon::operator>>(istream &is, FanConfig &c) {
  while (!is.eof()) {
    Point p;
    is >> p;
    if (p.valid())
      c.points.push_back(p);
  }

  return is;
}