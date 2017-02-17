#ifndef FANCON_MAIN_HPP
#define FANCON_MAIN_HPP

#include <algorithm>    // transform, sort
#include <csignal>
#include <cmath>        // floor
#include <iostream>
#include <string>
#include <sstream>
#include <functional>   // ref, reference_wrapped
#include <future>       // future, promise
#include <thread>
#include <sensors/sensors.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libexplain/setsid.h>
#include "Util.hpp"
#include "SensorController.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::to_string;
using std::stringstream;
using std::next;
using std::thread;
using std::make_pair;
using std::move;
using std::ref;
using std::reference_wrapper;
using fancon::SensorController;
using fancon::TemperatureSensor;
using fancon::Util::DaemonState;
using fancon::Util::log;

int main(int argc, char *argv[]);

namespace fancon {
const string conf_path("/etc/fancon.conf");

const string pid_file = string(fancon::Util::fancon_dir) + "pid";

DaemonState daemon_state;

string help();

string listFans(SensorController &sensorController);
string listSensors(SensorController &sensorController);

bool pidExists(pid_t pid);
void writeLock();

void test(SensorController &sensorController, const bool debug, const bool profileFans = false, int testRetries = 4);
void testUID(UID &uid, const bool profileFans = false, int retries = 4);

void handleSignal(int sig);
void start(SensorController &sc, const bool debug = false, const bool fork_ = false);
void send(DaemonState state);

struct Command {
  Command(const string &name, bool shrtName = false)
      : name(name), called(false) {
    if (shrtName) {
      shrt_name += name.front();

      // if name contains '-' set first 2 chars of each word as shrt_name
      auto it = name.begin();
      while ((it = find(next(it), name.end(), '-')) != name.end() && next(it) != name.end())
        shrt_name += *(next(it));
    }
  }

  virtual bool operator==(const string &other) { return other == shrt_name || other == name; }

  const string name;
  string shrt_name;
  bool called;
};

struct Option : Command {
public:
  Option(const string &name, bool shrtName = false, bool hasValue = false)
      : Command(name, shrtName), has_value(hasValue) {}

  bool has_value;
  int val;
};
}

#endif //FANCON_MAIN_HPP
