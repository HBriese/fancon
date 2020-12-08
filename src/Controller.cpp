#include "Controller.hpp"

namespace fc {
milliseconds update_interval(500);
bool dynamic = true;
uint smoothing_intervals = 4;
uint top_stickiness_intervals = 4;
uint temp_averaging_intervals = 8;
} // namespace fc

fc::Controller::Controller(path conf_path_) : config_path(move(conf_path_)) {
  reload(true);
  watcher = spawn_watcher();
}

fc::Controller::~Controller() { disable_all(); }

FanStatus fc::Controller::status(const string &flabel) const {
  const auto it = tasks.find(flabel);
  if (it == tasks.end())
    return FanStatus::FanStatus_Status_DISABLED;

  return (it->second.is_testing()) ? FanStatus::FanStatus_Status_TESTING
                                   : FanStatus::FanStatus_Status_ENABLED;
}

void fc::Controller::enable(fc::FanInterface &f, bool enable_all_dell) {
  if (tasks.count(f.label) > 0)
    return;

  // Dell fans can only be enabled/disabled togther
  if (f.type() == DevType::DELL && enable_all_dell)
    return enable_dell_fans();

  if (!f.pre_start_check())
    return;

  tasks.emplace(f.label, [this, &f](bool &run) {
    while (run) {
      notify_status_observers(f.label);
      f.update();
    }
    f.disable_control();
  });

  LOG(llvl::trace) << f.label << ": enabled";
}

void fc::Controller::enable_all() {
  for (auto &[key, f] : devices.fans) {
    if (!tasks.contains(f->label))
      enable(*f);
  }
}

void fc::Controller::disable(const string &flabel, bool disable_all_dell) {
  const auto it = tasks.find(flabel);
  if (it == tasks.end())
    return;

  // Dell fans can only be enabled/disabled togther
  if (devices.fans.at(flabel)->type() == DevType::DELL && disable_all_dell)
    return disable_dell_fans();

  tasks.erase(flabel);
  if (const auto fit = devices.fans.find(flabel); fit != devices.fans.end())
    fit->second->disable_control();

  LOG(llvl::trace) << flabel << ": disabled";
  notify_status_observers(flabel);
}

void fc::Controller::disable_all() {
  vector<string> disabled_fans;
  for (const auto &[flabel, task] : tasks)
    disabled_fans.push_back(flabel);

  tasks.clear();
  for (const auto &flabel : disabled_fans)
    notify_status_observers(flabel);

  LOG(llvl::trace) << "Disabling all";
}

void fc::Controller::reload(bool just_started) {
  if (!just_started)
    LOG(llvl::info) << "Reloading";

  Devices enumerated(true);
  merge(enumerated, false);

  if (const auto c = read_config(); c) {
    from(c->config());

    Devices conf_devs(c->devices());
    merge(conf_devs, true, true);

    remove_devices_not_in({{enumerated}, {conf_devs}});
  } else {
    remove_devices_not_in({{enumerated}});
  }

  notify_devices_observers();
}

void fc::Controller::recover() {
  // Re-enable control for all running tasks
  for (const auto &[flabel, t] : tasks) {
    const auto it = devices.fans.find(flabel);
    if (it != devices.fans.end())
      it->second->enable_control();
  }
}

void fc::Controller::nv_init() {
#ifdef FANCON_NVIDIA_SUPPORT
  if (NV::init(true))
    reload();
#endif // FANCON_NVIDIA_SUPPORT
}

void fc::Controller::test(fc::FanInterface &fan, bool forced, bool blocking,
                          shared_ptr<Util::ObservableNumber<int>> test_status) {
  if (fan.ignore || (fan.tested() && !forced))
    return;

  // If a test is already running for the device then just join onto it
  if (auto it = tasks.find(fan.label); it != tasks.end() && it->second.is_testing()) {
    // Add test_status observers to existing test_status
    for (const auto &cb : test_status->observers)
      it->second.test_status->register_observer(cb, true);
    if (blocking)
      it->second.join();
    return;
  }

  // Remove any running thread before testing
  tasks.erase(fan.label);

  auto test_func = [&] {
    // Test fan, then remove thread from map
    LOG(llvl::info) << fan << ": testing";
    fan.test(*test_status);
    LOG(llvl::info) << fan << ": test complete";
    const thread t([&] { tasks.erase(fan.label); });

    // Only write to file when no other fan are still testing
    if (tests_running() == 0)
      to_file(false);

    enable(fan);
  };

  const auto [it, success] =
      tasks.try_emplace(fan.label, move(test_func), test_status);

  if (!success)
    test(fan, forced, blocking, test_status);

  notify_status_observers(fan.label);
  if (blocking)
    it->second.join();
}

size_t fc::Controller::tests_running() const {
  const auto f = [](const size_t sum, const auto &p) {
    return sum + int(p.second.is_testing());
  };
  return std::accumulate(tasks.begin(), tasks.end(), 0, f);
}

void fc::Controller::set_devices(const fc_pb::Devices &devices_) {
  disable_all();
  devices = fc::Devices(false);
  devices.from(devices_);
  to_file(false);
  enable_all();
}

void fc::Controller::from(const fc_pb::ControllerConfig &c) {
  update_interval = milliseconds(c.update_interval());
  dynamic = c.dynamic();
  smoothing_intervals = c.smoothing_intervals();
  top_stickiness_intervals = c.top_stickiness_intervals();
  temp_averaging_intervals = c.temp_averaging_intervals();
}

void fc::Controller::to(fc_pb::Controller &c) const {
  to(*c.mutable_config());
  devices.to(*c.mutable_devices());
}

void fc::Controller::to(fc_pb::ControllerConfig &c) const {
  c.set_update_interval(update_interval.count());
  c.set_dynamic(dynamic);
  c.set_smoothing_intervals(smoothing_intervals);
  c.set_top_stickiness_intervals(top_stickiness_intervals);
  c.set_temp_averaging_intervals(temp_averaging_intervals);
}

void fc::Controller::enable_dell_fans() {
  for (const auto &[fl, f] : devices.fans) {
    if (f->type() == DevType::DELL)
      enable(*f, false);
  }
}

void fc::Controller::disable_dell_fans() {
  for (const auto &[fl, f] : devices.fans) {
    if (f->type() == DevType::DELL)
      disable(fl, false);
  }
}

bool fc::Controller::is_testing(const string &flabel) const {
  const auto it = tasks.find(flabel);
  return it != tasks.end() && it->second.is_testing();
}

optional<fc_pb::Controller> fc::Controller::read_config() {
  if (!exists(config_path))
    return nullopt;

  fc_pb::Controller c;
  std::ifstream ifs(config_path);
  std::stringstream ss;
  ss << ifs.rdbuf();
  google::protobuf::TextFormat::ParseFromString(ss.str(), &c);
  //  c.ParseFromIstream(&ifs);
  update_config_write_time();

  if (!ifs) {
    LOG(llvl::debug) << "Failed to read config";
    return nullopt;
  }

  return c;
}

void fc::Controller::merge(Devices &d, bool replace_on_match, bool deep_cmp) {
  const auto m = [&](auto &src, auto &dst, const auto &on_match) {
    for (auto &[key, dev] : src) {
      // Insert or assign or overwrite any existing device sharing that hw_id
      const string hw_id = dev->hw_id();
      auto it = find_if(dst.begin(), dst.end(), [&](const auto &p) {
        return p.second->hw_id() == hw_id;
      });

      const bool match = it != dst.end();
      if (match) {
        if (replace_on_match && (!deep_cmp || !dev->deep_equal(*it->second))) {
          on_match(it, it->first, key, dev);
        }
      } else {
        dst.emplace(key, move(dev));
      }
    }
  };

  m(d.fans, devices.fans,
    [&](auto &old_it, const string &old_key, const string &new_key, auto &dev) {
      // On match; re-insert device as the key may have changed
      const auto re_insert = [&] {
        devices.fans.erase(old_it);
        return devices.fans.emplace(new_key, move(dev));
      };
      const FanStatus fstatus = status(old_key);
      if (fstatus == FanStatus::FanStatus_Status_DISABLED) {
        re_insert();
      } else if (fstatus == FanStatus::FanStatus_Status_ENABLED) {
        disable(old_it->first, false);
        auto [it, success] = re_insert();
        enable(*it->second, true);
      } else if (fstatus == FanStatus::FanStatus_Status_TESTING) {
        const auto test_status = tasks.find(old_key)->second.test_status;
        disable(old_key, false);
        auto [it, success] = re_insert();
        test(*it->second, true, false, test_status);
      }
    });

  m(d.sensors, devices.sensors,
    [&](auto &old_it, [[maybe_unused]] const string &old_key,
        const string &new_key, auto &dev) {
      // On match; re-insert device as the key may have changed
      devices.sensors.erase(old_it);
      devices.sensors.emplace(new_key, move(dev));
    });
}

void fc::Controller::remove_devices_not_in(
    std::initializer_list<std::reference_wrapper<Devices>> l) {
  // Remove items not in conf_devs or enumerated but in devices
  for (const auto &p : devices.fans) {
    const auto &flabel = std::get<0>(p);
    if (!std::any_of(l.begin(), l.end(), [&](const Devices &l) {
          return l.fans.contains(flabel);
        })) {
      disable(flabel);
      devices.fans.erase(flabel);
    }
  }

  for (const auto &p : devices.sensors) {
    const auto &slabel = std::get<0>(p);
    if (!std::any_of(l.begin(), l.end(), [&](const Devices &l) {
          return l.sensors.contains(slabel);
        })) {
      devices.sensors.erase(slabel);
    }
  }
}

void fc::Controller::to_file(bool backup) {
  // Backup existing file
  if (backup && exists(config_path)) {
    const path backup_path = config_path.string() + "-" + date_time_now();
    fs::rename(config_path, backup_path);
    LOG(llvl::info) << "Moved previous config to: " << backup_path;
  }

  std::ofstream ofs(config_path);
  fc_pb::Controller c;
  to(c);

  string out_s;
  google::protobuf::TextFormat::Printer printer;
  printer.SetUseShortRepeatedPrimitives(true);

  printer.PrintToString(c, &out_s);
  ofs << out_s;

  if (ofs) {
    update_config_write_time();
    notify_devices_observers();
  } else {
    LOG(llvl::error) << "Failed to write config to: " << config_path;
  }
}

void fc::Controller::update_config_write_time() {
  if (exists(config_path))
    config_write_time = fs::last_write_time(config_path);
}

bool fc::Controller::config_file_modified() const {
  if (!exists(config_path))
    return false;

  return config_write_time != fs::last_write_time(config_path);
}

thread fc::Controller::spawn_watcher() {
  return thread([this] {
    update_config_write_time();
    while (true) {
      if (config_file_modified()) {
        update_config_write_time();
        reload();
      }
      sleep_for(update_interval);
    }
  });
}

void fc::Controller::notify_devices_observers() {
  if (device_observers.empty())
    return;

  const auto scoped_lock = device_observers_mutex.acquire_lock();
  for (const auto &f : device_observers)
    f(devices);
}

void fc::Controller::notify_status_observers(const string &flabel) {
  if (status_observers.empty())
    return;

  const auto fit = devices.fans.find(flabel);
  if (fit == devices.fans.end())
    return;

  const FanStatus fstatus = status(flabel);
  const auto scoped_lock = status_observers_mutex.acquire_lock();
  for (const auto &f : status_observers)
    f(*fit->second, fstatus);
}

string fc::Controller::date_time_now() {
  std::time_t tt = chrono::system_clock::to_time_t(chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&tt, &tm);

  std::stringstream ss;
  ss << tm.tm_year << "-" << tm.tm_mon << "-" << tm.tm_mday << "-" << tm.tm_hour
     << "-" << tm.tm_min;
  return ss.str();
}
