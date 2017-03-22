#include <boost/filesystem/operations.hpp>
#include "Util.hpp"

using namespace fancon;

int Util::getLastNum(const string &str) {
  auto endDigRevIt = std::find_if(str.rbegin(), str.rend(), [](const char &c) { return std::isdigit(c); });
  auto endIt = (endDigRevIt + 1).base();

  bool numFound = false;
  auto begDigRevIt = std::find_if(endDigRevIt, str.rend(), [&numFound](const char &c) {
    if (std::isdigit(c))
      numFound = true;
    return numFound && !std::isdigit(c);
  });

  string numStr(begDigRevIt.base(), endIt + 1);
  std::stringstream ss(numStr);
  int num{};
  ss >> num;
  return num;
}

bool Util::isNum(const string &str) {
  return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
}

bool Util::locked() {
  if (!exists(pid_file))
    return false;

  auto pid = read < pid_t > (pid_file);
  return exists(string("/proc/") + to_string(pid));
//      && pid != getpid();
}

void Util::lock() {
  if (locked()) {
    LOG(llvl::error) << "A fancon process is already running, please close it to continue";
    exit(EXIT_FAILURE);
  } else
    write(pid_file, getpid());
}

string Util::getDir(const string &hwID, DeviceType devType, const bool useSysFS) {
  string d;
  if (devType == DeviceType::FAN)
    d = string((useSysFS) ? hwmon_path : fancon_hwmon_path);
  else if (devType == FAN_NV)
    d = string(fancon_dir) + nvidia_label;

  return (d += hwID + '/');
}

string Util::getPath(const string &path_pf, const string &hwID, DeviceType devType, const bool useSysFS) {
  return getDir(hwID, devType, useSysFS) + path_pf;
}

string Util::readLine(string path, int nFailed) {
  ifstream ifs(path);
  string line;
  std::getline(ifs, line);
  ifs.close();

  if (ifs.fail()) {
    auto exist = exists(path);
    // Retry read 3 times if file exists, before failing
    if (exist && nFailed <= 3)
      return readLine(path, ++nFailed);

    const char *reason = (exist) ? "filesystem or permission error" : "doesn't exist";
    LOG(llvl::error) << "Failed to read from: " << path << " - " << reason << "; user id " << getuid();
  }

  return line;
}
