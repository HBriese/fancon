#ifdef FANCON_NVIDIA_SUPPORT
#include "NvidiaUtil.hpp"

using namespace fancon;

// Define global variables
namespace fancon {
namespace NV {
NV::LibX11 xlib;
NV::LibXNvCtrl xnvlib;
NV::DisplayWrapper dw(nullptr);
}
}

bool NV::DisplayWrapper::open(string da, string xa) {
  const char *denv = "DISPLAY", *xaenv = "XAUTHORITY";
  bool forcedDa = false;

  // Set da and xa to environmental variables if they are set
  char *res;
  if (da.empty() && (res = getenv(denv)))
    da = string(res);
  if (xa.empty() && (res = getenv(xaenv)))
    xa = string(res);

  if ((da.empty()) || (xa.empty())) {
    // Attempt to find the da and xa used by startx | xinit
    redi::ipstream ips("echo \"$(ps wwaux 2>/dev/null | grep -wv PID | grep -v grep)\" "
                           "| grep '/X.* :[0-9][0-9]* .*-auth' | egrep -v 'startx|xinit' "
                           "| sed -e 's,^.*/X.* \\(:[0-9][0-9]*\\) .* -auth \\([^ ][^ ]*\\).*$,\\1\\,\\2,' | sort -u");
    string pair;
    ips >> pair;

    // set missing da/xa to that found
    auto sepIt = find(pair.begin(), pair.end(), ',');
    if (sepIt != pair.end()) {

      if (da.empty()) {
        da = string(pair.begin(), sepIt);
      }
      if (xa.empty()) {
        xa = string(sepIt + 1, pair.end());
        setenv(xaenv, xa.c_str(), 1);   // $XAUTHORITY is queried directly by XOpenDisplay
      }
    } else if ((forcedDa = da.empty()))    // If no da is found, then guess
      da = ":0";
  }

  // Open display and log errors
  if (!(dp = xlib.OpenDisplay(da.c_str()))) {
    stringstream err;
    if (forcedDa)   // da.empty() == true   // TODO: review check - guessed da may be correct
      err << "Set \"display=\" in " << Util::conf_path << " to your display (echo $" << denv << ')';
    if (!getenv(xaenv))
      err << "Set \"xauthority=\" in " << Util::conf_path << " to your .Xauthority file (echo $" << xaenv << ')';

    return false;
  }

  return true;
}

Display *NV::DisplayWrapper::operator*() {
  assert(dp != nullptr);
  return dp;
}

bool NV::supported() {
  if (!Util::locked())
    xlib.InitThreads();   // Run before first XOpenDisplay() from any process

  if (!dw.open({}, {})) {
    LOG(llvl::debug) << "X11 display cannot be opened";
    return false;
  }

  int eventBase, errorBase;
  auto ret = xnvlib.QueryExtension(*dw, &eventBase, &errorBase);
  if (!ret) {
    LOG(llvl::debug) << "NVIDIA fan control not supported";   // NV-CONTROL X does not exist!
    return false;
  } else if (errorBase)
    LOG(llvl::warning) << "NV-CONTROL X return error base: " << errorBase;

  int major = 0, minor = 0;
  ret = xnvlib.QueryVersion(*dw, &major, &minor);
  if (!ret) {
    LOG(llvl::error) << "Failed to query NV-CONTROL X version";
    return false;
  } else if ((major < 1) || (major == 1 && minor < 9))  // XNVCTRL must be at least v1.9
    LOG(llvl::warning) << "NV-CONTROL X version is not officially supported (too old!); please use v1.9 or higher";

//  LOG(llvl::debug) << "NVIDIA fan control supported";
  return true;
}

void NV::enableFanControlCoolbit() {
  // TODO: find and set Coolbits value without 'nvidia-xconfig' - for when not available (e.g. in snap confinement)
  string command("sudo nvidia-xconfig -t | grep Coolbits");
  redi::pstream ips(command);
  string l;
  std::getline(ips, l);

  // Exit early with message if nvidia-xconfig isn't found
  if (l.empty()) {
    LOG(llvl::info) << "nvidia-xconfig could not be found, either install it, or set coolbits value manually";
    return;
  }

  int iv = Util::getLastNum(l);  // Initial value
  int cv(iv);   // Current val

  const int nBits = 5;
  const int fcBit = 2;    // 4
  int cbV[nBits] = {1, 2, 4, 8, 16};  // pow(2, bit)
  int cbS[nBits]{0};

  for (auto i = nBits - 1; i >= 0; --i)
    if ((cbS[i] = (cv / cbV[i]) >= 1))
      cv -= cbV[i];

  int nv = iv;
  if (cv > 0) {
    LOG(llvl::error) << "Invalid coolbits value, fixing with fan control bit set";
    nv -= cv;
  }

  if (!cbS[fcBit])
    nv += cbV[fcBit];

  if (nv != iv) {
    command = string("sudo nvidia-xconfig --cool-bits=") + to_string(nv) + " > /dev/null";
    if (system(command.c_str()) != 0)
      LOG(llvl::error) << "Failed to write coolbits value, nvidia fan test may fail!";
    LOG(llvl::info) << "Reboot, or restart your display server to enable NVIDIA fan control";
  }
}

int NV::getNumGPUs(bool nvidiaControl) {
  int nGPUs = 0;
  if (!nvidiaControl)
    return nGPUs;

  // Number of GPUs
  if (!xnvlib.QueryTargetCount(*dw, NV_CTRL_TARGET_TYPE_GPU, &nGPUs))
    LOG(llvl::error) << "Failed to query number of NVIDIA GPUs";

  return nGPUs;
}

vector<int> NV::nvProcessBinaryData(const unsigned char *data, const int len) {
  vector<int> v;
  for (int i = 0; i < len; ++i) {
    int val = static_cast<int>(*(data + i));
    if (val == 0)       // Memory set to 0 when allocated
      break;
    v.push_back(--val); // Return an index (start at 0), binary data start at 1
  }

  return v;
}

vector<UID> NV::getFans(bool nvidiaControl) {
  vector<UID> uids;
  auto nGPUs = getNumGPUs(nvidiaControl);

  for (int i = 0; i < nGPUs; ++i) {
    vector<int> fanIDs, tSensorIDs;

    // GPU fans
    unsigned char *coolersBuf = nullptr;
    int len = 0;    // Buffer size allocated by NVCTRL
    int ret = xnvlib.QueryTargetBinaryData(*dw, NV_CTRL_TARGET_TYPE_GPU, i, 0,
                                           NV_CTRL_BINARY_DATA_COOLERS_USED_BY_GPU, &coolersBuf, &len);
    if (!ret || coolersBuf == nullptr) {
      LOG(llvl::error) << "Failed to query number of fans for GPU: " << i;
      continue;
    }
    fanIDs = nvProcessBinaryData(coolersBuf, len);

    // GPU product name
    char *productNameBuf = nullptr;
    ret = xnvlib.QueryTargetStringAttribute(*dw, NV_CTRL_TARGET_TYPE_GPU, i, 0,
                                            NV_CTRL_STRING_PRODUCT_NAME, &productNameBuf);
    if (!ret || productNameBuf == nullptr) {
      LOG(llvl::error) << "Failed to query gpu: " << i << " product name";
      continue;
    }
    string productName(productNameBuf);

    // Move iterator past title(s), and space trailing it
    vector<string> titles = {"GeForce", "GTX", "GRID", "Quadro"};
    auto begIt = productName.begin();
    for (const auto &t : titles) {
      auto newBegIt = search(begIt, productName.end(), t.begin(), t.end()) + t.size();
      if (*newBegIt == ' ' && (newBegIt + 1) != productName.end())  // skip spaces
        begIt = ++newBegIt;
    }

    std::replace(begIt, productName.end(), ' ', '_');

    for (const auto &fi : fanIDs)
      uids.emplace_back(UID(Util::nvidia_label, fi, string(begIt, productName.end())));
  }

  return uids;
}

vector<UID> NV::getSensors(bool nvidiaControl) {
  vector<UID> uids;
  auto nGPUs = getNumGPUs(nvidiaControl);

  for (auto i = 0; i < nGPUs; ++i) {
    vector<int> tSensorIDs;

    int len = 0;
    // GPU temperature sensors
    unsigned char *tSensors = nullptr;
    int ret = xnvlib.QueryTargetBinaryData(*dw, NV_CTRL_TARGET_TYPE_GPU, i, 0,
                                           NV_CTRL_BINARY_DATA_THERMAL_SENSORS_USED_BY_GPU, &tSensors, &len);
    if (!ret || tSensors == nullptr) {
      LOG(llvl::error) << "Failed to query number of temperature sensors for GPU: " << i;
      continue;
    }
    tSensorIDs = nvProcessBinaryData(tSensors, len);

    for (const auto &tsi : tSensorIDs)
      uids.emplace_back(UID(Util::nvidia_label, tsi, string(Util::temp_sensor_label)));
  }

  return uids;
}

#ifdef FANCON_NVML_SUPPORT_EXPERIMENTAL
vector<nvmlDevice_t> NV::getDevices() {
  uint nDevices = 0;
  if (nvmlDeviceGetCount(&nDevices) != NVML_SUCCESS)
    LOG(llvl::error) << "Failed to query number of NVIDIA GPUs";

  vector<nvmlDevice_t> devices(nDevices);

  for (uint i = 0; i < nDevices; ++i) {
    nvmlDevice_t dev;
    if (nvmlDeviceGetHandleByIndex(i, &dev) != NVML_SUCCESS)
      LOG(llvl::error) << "Failed to get NVIDIA device " << i << "; GPU count = " << nDevices;
    devices.emplace_back(std::move(dev));
  }

  return devices;
}

// TODO: add support for cards with multiple fans
vector<UID> NV::getFans() {
  vector<UID> uids;
  auto devices = getDevices();

  for (uint i = 0, e = devices.size(); i < e; ++i) {
    // Verify device fan is valid
    uint speedPercent = 0;
    if (nvmlDeviceGetFanSpeed(devices[i], &speedPercent) != NVML_SUCCESS) {
      LOG(llvl::error) << "Failed to get GPU " << i << " fan";
      continue;
    }

    // GPU product name
    char productNameBuf[NVML_DEVICE_NAME_BUFFER_SIZE];
    if (nvmlDeviceGetName(devices[i], &productNameBuf[0], NVML_DEVICE_NAME_BUFFER_SIZE) != NVML_SUCCESS) {
      LOG(llvl::error) << "Failed to get GPU " << i << " product name";
      continue;
    }
    string productName(productNameBuf);

    // Move iterator past title(s), and space trailing it
    vector<string> titles = {"GeForce", "GTX", "GRID", "Quadro"};
    auto begIt = productName.begin();
    for (const auto &t : titles) {
      auto newBegIt = search(begIt, productName.end(), t.begin(), t.end()) + t.size();
      if (*newBegIt == ' ' && (newBegIt + 1) != productName.end())  // skip spaces
        begIt = ++newBegIt;
    }

    std::replace(begIt, productName.end(), ' ', '_');

    uids.emplace_back(UID(Util::nvidia_label, i, string(begIt, productName.end())));
  }

  return uids;
}

vector<UID> NV::getSensors() {
  vector<UID> uids;
  auto devices = getDevices();

  for (uint i = 0, e = devices.size(); i < e; ++i) {
    // GPU temperature sensors
    uint temp = 0;
    if (nvmlDeviceGetTemperature(devices[i], NVML_TEMPERATURE_GPU, &temp) != NVML_SUCCESS) {
      LOG(llvl::error) << "Failed to read GPU " << i << " temperature";
      continue;
    }

    uids.emplace_back(UID(Util::nvidia_label, i, string(Util::temp_sensor_label)));
  }

  return uids;
}
#endif //FANCON_NVML_SUPPORT_EXPERIMENTAL

int NV::Data_R::read(const int hwID) const {
  int val{};
  if (!xnvlib.QueryTargetAttribute(*dw, target, hwID, 0, attribute, &val)) {
    LOG(llvl::error) << "NVIDIA fan " << hwID << ": failed reading " << title;
    val = 0;
  }

  return val;
}

void NV::Data_RW::write(const int hwID, int val) const {
  if (!xnvlib.SetTargetAttributeAndGetStatus(*dw, target, hwID, 0, attribute, val))
    LOG(llvl::error) << "NVIDIA fan " << hwID << ": failed writing " << title << " = " << val;
}
#endif //FANCON_NVIDIA_SUPPORT