#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "api.hpp"
#include "json.hpp"
#include "models.hpp"
#include "protocol.hpp"

namespace {

constexpr unsigned kStatusPollFrames = 60;
constexpr unsigned kWifiCheckFrames = 30;
constexpr unsigned kStaleAfterFrames = 240;
constexpr unsigned kDiscoveryRetryFrames = 300;
constexpr size_t kMaximumProfileFiles = 64;
constexpr size_t kMaximumProfiles = 1024;
constexpr size_t kMaximumProfileFileBytes = 256 * 1024;

const char *const kFixerChoices[] = {
    agitate3ds::kDefaultFixer,
    "Kodak Rapid Fixer (1+3)",
    "Adox Adofix Plus (1+4)",
    "Custom fixer (edit in source)",
};

enum class Tab {
  Controls,
  Setup,
};

enum class PendingRequest {
  None,
  Discovery,
  Status,
  ProfileList,
  ProfileFile,
  Control,
};

enum class PickerType {
  None,
  Film,
  Developer,
  Dilution,
};

struct ControlCommand {
  unsigned control;
  std::string path;
  std::string body;
};

struct PickerOption {
  std::string id;
  std::string label;
  int profileIndex;
};

constexpr size_t kMaximumQueuedControls = 8;
constexpr size_t kPickerPageSize = 18;

const char *const kControlPaths[] = {
    "/api/start",
    "/api/pause",
    "/api/restart_step",
    "/api/next",
    "/api/stop",
    "/api/motor/test",
    "/api/motor/aggressive",
};

const char *const kControlSuccessMessages[] = {
    "Start request sent",
    "Pause/resume request sent",
    "Restart-step request sent",
    "Next/skip request sent",
    "Stop request sent",
    "Motor test request sent",
    "Aggressive agitation request sent",
};

class Application {
 public:
  Application()
      : tab_(Tab::Controls),
        setupSelection_(0),
        selectedControl_(0),
        selectedProfile_(-1),
        fixerSelection_(0),
        frame_(0),
        nextStatusFrame_(1),
        lastStatusFrame_(0),
        wifiConnected_(false),
        wasWifiConnected_(false),
        haveStatus_(false),
        shouldExit_(false),
        apiReady_(false),
        pendingRequest_(PendingRequest::None),
        pendingControl_(0),
        profileReloadRequested_(false),
        profileLoadActive_(false),
        nextProfileFile_(0),
        skippedProfileFiles_(0),
        statusRefreshRequested_(false),
        serverReachable_(false),
        discoveryRequested_(false),
        nextDiscoveryFrame_(0),
        serverAddressUpdateRequested_(false),
        manualServerAddress_(false),
        editingControllerIp_(false),
        selectedIpOctet_(0),
        pickerType_(PickerType::None),
        pickerSelection_(0) {}

  bool initialize() {
    std::string error;
    if (!api_.initialize(error)) {
      connectionMessage_ = error;
      return false;
    }
    apiReady_ = true;
    return true;
  }

  void shutdown() {
    api_.shutdown();
  }

  bool shouldExit() const {
    return shouldExit_;
  }

  void update(u32 keysDown, u32 keysDownRepeat) {
    ++frame_;
    if (frame_ == 1 || frame_ % kWifiCheckFrames == 0) {
      wifiConnected_ = apiReady_ && api_.wifiConnected();
      if (wifiConnected_ && !wasWifiConnected_) {
        if (manualServerAddress_) {
          connectionMessage_ = "Using entered controller address";
          requestProfileLoad();
          statusRefreshRequested_ = true;
        } else if (api_.discoverAsync()) {
          pendingRequest_ = PendingRequest::Discovery;
          connectionMessage_ = "Searching LAN for agitate-esp";
        } else {
          serverAddress_ = agitate3ds::kFallbackServerUrl;
          if (api_.setServerUrl(serverAddress_)) {
            connectionMessage_ = "Discovery unavailable; trying direct AP";
            requestProfileLoad();
            statusRefreshRequested_ = true;
          } else {
            connectionMessage_ = "Could not start controller discovery";
          }
        }
      }
      if (!wifiConnected_ && wasWifiConnected_) {
        serverReachable_ = false;
        discoveryRequested_ = false;
        statusRefreshRequested_ = false;
      }
      wasWifiConnected_ = wifiConnected_;
    }

    if (wifiConnected_ && serverReachable_ &&
        frame_ >= nextStatusFrame_) {
      statusRefreshRequested_ = true;
      nextStatusFrame_ = frame_ + kStatusPollFrames;
    }

    if (keysDown & KEY_START) {
      shouldExit_ = true;
      return;
    }
    if (keysDown & KEY_TOUCH) {
      touchPosition touch = {};
      hidTouchRead(&touch);
      handleTouch(touch);
    }

    if (pickerType_ != PickerType::None) {
      handlePickerKeys(keysDown, keysDownRepeat);
    } else if (tab_ == Tab::Controls) {
      handleControlKeys(keysDown, keysDownRepeat);
    } else {
      handleSetupKeys(keysDown, keysDownRepeat);
    }

    processApiResponse();
    dispatchNextRequest();
  }

  void render(PrintConsole &topConsole, PrintConsole &bottomConsole) const {
    topConsole.frameBuffer =
        reinterpret_cast<u16 *>(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, nullptr,
                                                  nullptr));
    consoleSelect(&topConsole);
    consoleClear();
    renderTop();

    bottomConsole.frameBuffer =
        reinterpret_cast<u16 *>(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr,
                                                  nullptr));
    consoleSelect(&bottomConsole);
    consoleClear();
    if (tab_ == Tab::Controls) {
      renderControls();
    } else if (pickerType_ != PickerType::None) {
      renderPicker();
    } else {
      renderSetup();
    }
  }

 private:
  void handleControlKeys(u32 keysDown, u32 keysDownRepeat) {
    if (keysDown & (KEY_LEFT | KEY_RIGHT)) {
      tab_ = Tab::Setup;
      return;
    }
    if (keysDownRepeat & KEY_UP) {
      selectedControl_ =
          (selectedControl_ + 6) % 7;
    }
    if (keysDownRepeat & KEY_DOWN) {
      selectedControl_ = (selectedControl_ + 1) % 7;
    }
    if (keysDown & KEY_A) {
      performControl(selectedControl_);
    }
    if (keysDown & KEY_B) {
      performControl(1);
    }
    if (keysDown & KEY_X) {
      performControl(2);
    }
    if (keysDown & KEY_Y) {
      performControl(3);
    }
    if (keysDown & KEY_R) {
      performControl(4);
    }
    if (keysDown & KEY_L) {
      performControl(5);
    }
    if (keysDown & KEY_SELECT) {
      performControl(6);
    }
  }

  void handleSetupKeys(u32 keysDown, u32 keysDownRepeat) {
    if (editingControllerIp_) {
      handleControllerIpKeys(keysDown);
      return;
    }

    if (keysDownRepeat & (KEY_LEFT | KEY_RIGHT)) {
      const int direction = (keysDownRepeat & KEY_RIGHT) ? 1 : -1;
      if (setupSelection_ < 3) {
        cycleSelector(setupSelection_, direction);
      } else if (setupSelection_ == 3) {
        cycleFixer(direction);
      }
    }
    if (keysDownRepeat & (KEY_L | KEY_R)) {
      const int direction = (keysDownRepeat & KEY_R) ? 1 : -1;
      if (setupSelection_ < 3) {
        cycleSelector(setupSelection_, direction,
                      setupSelection_ == 2 ? 8 : 16);
      } else if (setupSelection_ == 3) {
        cycleFixer(direction);
      }
    }
    if (keysDownRepeat & KEY_UP) {
      setupSelection_ = (setupSelection_ + 7) % 8;
    }
    if (keysDownRepeat & KEY_DOWN) {
      setupSelection_ = (setupSelection_ + 1) % 8;
    }
    if ((keysDown & KEY_X) && setupSelection_ < 3) {
      cycleSelector(setupSelection_, -1,
                    setupSelection_ == 2 ? 16 : 64);
    }
    if ((keysDown & KEY_Y) && setupSelection_ < 3) {
      cycleSelector(setupSelection_, 1,
                    setupSelection_ == 2 ? 16 : 64);
    }
    if (keysDown & KEY_A) {
      activateSetupItem(setupSelection_);
    }
    if ((keysDown & KEY_SELECT) && setupSelection_ < 3) {
      editSetupField(setupSelection_);
    }
    if (keysDown & KEY_B) {
      tab_ = Tab::Controls;
    }
  }

  void handleTouch(const touchPosition &touch) {
    if (pickerType_ != PickerType::None) {
      handlePickerTouch(touch);
      return;
    }
    if (editingControllerIp_) {
      return;
    }
    if (touch.py < 24) {
      tab_ = tab_ == Tab::Controls ? Tab::Setup : Tab::Controls;
      return;
    }
    if (touch.py >= 192) {
      return;
    }

    const unsigned row = static_cast<unsigned>((touch.py - 24) / 16);
    if (tab_ == Tab::Controls) {
      if (row < 7) {
        selectedControl_ = row;
        performControl(row);
      }
      return;
    }

    if (row < 8) {
      setupSelection_ = row;
      activateSetupItem(row);
    }
  }

  void performControl(unsigned control) {
    if (control >= sizeof(kControlPaths) / sizeof(kControlPaths[0])) {
      return;
    }

    std::string body;
    if (control == 0) {
      if (!haveFreshStatus()) {
        message_ = "Get a valid status before starting the process";
        return;
      }
      if (status_.running && !status_.paused && !status_.waitingForStart) {
        message_ = "A step is active; use Pause, Next/Skip, or Stop";
        return;
      }
      if (status_.running) {
        body = "{}";
      } else {
        std::string error;
        if (!agitate3ds::makeStartPayload(setup_, effectiveTimeMinutes(),
                                         body, error)) {
          message_ = error;
          return;
        }
      }
    }

    if (!wifiConnected_) {
      message_ = "Connect to the agitate-esp Wi-Fi network first";
      return;
    }
    if (controlQueue_.size() >= kMaximumQueuedControls) {
      message_ = "Command queue full; wait for the server";
      return;
    }

    ControlCommand command;
    command.control = control;
    command.path = kControlPaths[control];
    command.body = body;
    controlQueue_.push_back(command);
    message_ = "Command queued";
  }

  void activateSetupItem(unsigned item) {
    switch (item) {
      case 0:
      case 1:
      case 2:
        if (profiles_.empty()) {
          editSetupField(item);
        } else {
          openPicker(item);
        }
        break;
      case 3:
        cycleFixer(1);
        break;
      case 4:
        editTime();
        break;
      case 5:
        discoveryRequested_ = true;
        nextDiscoveryFrame_ = frame_;
        connectionMessage_ = "Searching LAN for agitate-esp";
        break;
      case 6:
        requestProfileLoad();
        statusRefreshRequested_ = true;
        break;
      case 7:
        beginControllerIpEdit();
        break;
      default:
        break;
    }
  }

  void editSetupField(unsigned field) {
    if (field == 0) {
      editText("Film ID", setup_.film, true);
    } else if (field == 1) {
      editText("Developer ID", setup_.developer, true);
    } else if (field == 2) {
      editText("Dilution", setup_.dilution, true);
    }
  }

  bool editText(const char *label,
                std::string &value,
                bool clearSelectedProfile) {
    char input[256] = {};
    std::strncpy(input, value.c_str(), sizeof(input) - 1);

    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 64);
    swkbdSetHintText(&keyboard, label);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Save", true);
    if (!api_.pauseWorkerIfIdle()) {
      message_ = "Wait for network activity to finish, then retry";
      return false;
    }
    const SwkbdButton result = swkbdInputText(&keyboard, input, sizeof(input));
    api_.resumeWorker();
    if (result != SWKBD_BUTTON_RIGHT) {
      return false;
    }

    const std::string edited(input);
    if (edited.size() > agitate3ds::kMaximumSetupFieldBytes) {
      message_ = "Setup fields must be at most 64 UTF-8 bytes";
      return false;
    }
    value = edited;
    if (clearSelectedProfile) {
      selectedProfile_ = -1;
    }
    message_ = std::string(label) + " updated";
    return true;
  }

  void editTime() {
    char input[32] = {};
    if (setup_.customTimeMinutes > 0.0) {
      std::snprintf(input, sizeof(input), "%.2f",
                    setup_.customTimeMinutes);
    }

    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 12);
    swkbdSetHintText(&keyboard, "Developer time in minutes; blank uses profile/default");
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Save", true);
    if (!api_.pauseWorkerIfIdle()) {
      message_ = "Wait for network activity to finish, then retry";
      return;
    }
    const SwkbdButton result = swkbdInputText(&keyboard, input, sizeof(input));
    api_.resumeWorker();
    if (result != SWKBD_BUTTON_RIGHT) {
      return;
    }

    if (input[0] == '\0') {
      setup_.customTimeMinutes = 0.0;
      message_ = "Using selected profile time or 10-minute default";
      return;
    }

    char *end = nullptr;
    const double minutes = std::strtod(input, &end);
    if (end == input || *end != '\0' || !std::isfinite(minutes) ||
        minutes < 0.0 || minutes > 24.0 * 60.0) {
      message_ = "Enter a number from 0 to 1440 minutes";
      return;
    }
    setup_.customTimeMinutes = minutes;
    message_ = minutes == 0.0
                   ? "Using selected profile time or 10-minute default"
                   : "Custom developer time updated";
  }

  void beginControllerIpEdit() {
    std::string current = serverAddress_;
    const std::string prefix = "http://";
    if (current.compare(0, prefix.size(), prefix) == 0) {
      current.erase(0, prefix.size());
    }

    unsigned octetIndex = 0;
    size_t start = 0;
    while (octetIndex < 4 && start < current.size()) {
      const size_t end = current.find('.', start);
      const size_t length =
          (end == std::string::npos ? current.size() : end) - start;
      if (length == 0 || length > 3) {
        break;
      }
      unsigned value = 0;
      for (size_t i = start; i < start + length; ++i) {
        if (current[i] < '0' || current[i] > '9') {
          break;
        }
        value = value * 10 + static_cast<unsigned>(current[i] - '0');
      }
      if (value > 255) {
        break;
      }
      controllerIpOctets_[octetIndex++] = value;
      if (end == std::string::npos) {
        start = current.size();
      } else {
        start = end + 1;
      }
    }

    if (octetIndex != 4 || start != current.size()) {
      controllerIpOctets_[0] = 192;
      controllerIpOctets_[1] = 168;
      controllerIpOctets_[2] = 1;
      controllerIpOctets_[3] = 224;
    }
    selectedIpOctet_ = 0;
    editingControllerIp_ = true;
    message_ = "Left/right octet; up/down +/-1; L/R +/-16; A save";
  }

  void handleControllerIpKeys(u32 keysDown) {
    if (keysDown & KEY_B) {
      editingControllerIp_ = false;
      message_ = "Controller IP change cancelled";
      return;
    }
    if (keysDown & (KEY_LEFT | KEY_RIGHT)) {
      selectedIpOctet_ =
          (selectedIpOctet_ + ((keysDown & KEY_RIGHT) ? 1 : 3)) % 4;
    }
    if (keysDown & KEY_UP) {
      controllerIpOctets_[selectedIpOctet_] =
          (controllerIpOctets_[selectedIpOctet_] + 1) & 0xff;
    }
    if (keysDown & KEY_DOWN) {
      controllerIpOctets_[selectedIpOctet_] =
          (controllerIpOctets_[selectedIpOctet_] + 255) & 0xff;
    }
    if (keysDown & KEY_R) {
      controllerIpOctets_[selectedIpOctet_] =
          (controllerIpOctets_[selectedIpOctet_] + 16) & 0xff;
    }
    if (keysDown & KEY_L) {
      controllerIpOctets_[selectedIpOctet_] =
          (controllerIpOctets_[selectedIpOctet_] + 240) & 0xff;
    }
    if (!(keysDown & KEY_A)) {
      return;
    }

    char address[16];
    std::snprintf(address, sizeof(address), "%u.%u.%u.%u",
                  controllerIpOctets_[0], controllerIpOctets_[1],
                  controllerIpOctets_[2], controllerIpOctets_[3]);
    serverAddress_ = std::string("http://") + address;
    manualServerAddress_ = true;
    editingControllerIp_ = false;
    serverAddressUpdateRequested_ = true;
    discoveryRequested_ = false;
    statusRefreshRequested_ = true;
    requestProfileLoad();
    connectionMessage_ = "Trying controller IP";
    message_ = "Controller IP saved";
  }

  void cycleFixer(int direction) {
    const int choices =
        static_cast<int>(sizeof(kFixerChoices) / sizeof(kFixerChoices[0]));
    const int nextSelection =
        (fixerSelection_ + direction + choices) % choices;
    fixerSelection_ = nextSelection;
    setup_.fixer = kFixerChoices[fixerSelection_];
    message_ = "Fixer updated";
  }

  std::vector<PickerOption> filmPickerOptions() const {
    std::vector<PickerOption> options;
    options.push_back({"", "Manual setup", -1});
    for (const agitate3ds::Profile &profile : profiles_) {
      const auto found = std::find_if(
          options.begin() + 1, options.end(),
          [&profile](const PickerOption &option) {
            return option.id == profile.filmId;
          });
      if (found == options.end()) {
        options.push_back({profile.filmId, profile.filmName, -1});
      }
    }
    std::sort(options.begin() + 1, options.end(),
              [](const PickerOption &left, const PickerOption &right) {
                return left.label == right.label ? left.id < right.id
                                                 : left.label < right.label;
              });
    return options;
  }

  std::vector<PickerOption> developerPickerOptions() const {
    std::vector<PickerOption> options;
    options.push_back({"", "Manual developer", -1});
    for (const agitate3ds::Profile &profile : profiles_) {
      if (!setup_.film.empty() && profile.filmId != setup_.film) {
        continue;
      }
      const auto found = std::find_if(
          options.begin() + 1, options.end(),
          [&profile](const PickerOption &option) {
            return option.id == profile.developerId;
          });
      if (found == options.end()) {
        options.push_back(
            {profile.developerId, profile.developerName, -1});
      }
    }
    std::sort(options.begin() + 1, options.end(),
              [](const PickerOption &left, const PickerOption &right) {
                return left.label == right.label ? left.id < right.id
                                                 : left.label < right.label;
              });
    return options;
  }

  std::vector<PickerOption> dilutionPickerOptions() const {
    std::vector<PickerOption> options;
    options.push_back({"", "Manual dilution", -1});
    if (setup_.film.empty() || setup_.developer.empty()) {
      return options;
    }

    for (size_t i = 0; i < profiles_.size(); ++i) {
      const agitate3ds::Profile &profile = profiles_[i];
      if (profile.filmId != setup_.film ||
          profile.developerId != setup_.developer) {
        continue;
      }
      std::string label = profile.dilution;
      if (!profile.asaIso.empty()) {
        label += " @ ISO " + profile.asaIso;
      }
      label += " - " + formatDuration(profile.timeMinutes) + "m";
      options.push_back({profile.dilution, label, static_cast<int>(i)});
    }
    return options;
  }

  std::vector<PickerOption> pickerOptions(unsigned field) const {
    if (field == 0) {
      return filmPickerOptions();
    }
    if (field == 1) {
      return developerPickerOptions();
    }
    return dilutionPickerOptions();
  }

  size_t currentPickerSelection(
      unsigned field,
      const std::vector<PickerOption> &options) const {
    if (field < 2) {
      const std::string &value = field == 0 ? setup_.film : setup_.developer;
      for (size_t i = 0; i < options.size(); ++i) {
        if (options[i].id == value) {
          return i;
        }
      }
      return 0;
    }
    for (size_t i = 0; i < options.size(); ++i) {
      if (options[i].profileIndex == selectedProfile_ &&
          selectedProfile_ >= 0) {
        return i;
      }
    }
    return 0;
  }

  PickerType pickerTypeForField(unsigned field) const {
    if (field == 0) {
      return PickerType::Film;
    }
    if (field == 1) {
      return PickerType::Developer;
    }
    return PickerType::Dilution;
  }

  void openPicker(unsigned field) {
    pickerOptions_ = pickerOptions(field);
    if (pickerOptions_.empty()) {
      message_ = "No choices available";
      return;
    }
    pickerType_ = pickerTypeForField(field);
    pickerSelection_ = currentPickerSelection(field, pickerOptions_);
  }

  void closePicker() {
    pickerType_ = PickerType::None;
    pickerOptions_.clear();
    pickerSelection_ = 0;
  }

  void handlePickerKeys(u32 keysDown, u32 keysDownRepeat) {
    if (keysDown & KEY_B) {
      closePicker();
      return;
    }
    if (pickerOptions_.empty()) {
      closePicker();
      return;
    }

    const size_t pageCount =
        (pickerOptions_.size() + kPickerPageSize - 1) / kPickerPageSize;
    const size_t pageStart =
        (pickerSelection_ / kPickerPageSize) * kPickerPageSize;
    if (keysDownRepeat & KEY_UP) {
      if (pickerSelection_ > 0) {
        --pickerSelection_;
      }
    }
    if (keysDownRepeat & KEY_DOWN) {
      if (pickerSelection_ + 1 < pickerOptions_.size()) {
        ++pickerSelection_;
      }
    }
    if (keysDownRepeat & KEY_LEFT) {
      pickerSelection_ = pickerSelection_ > 0 ? pickerSelection_ - 1 : 0;
    }
    if (keysDownRepeat & KEY_RIGHT) {
      pickerSelection_ =
          std::min(pickerSelection_ + 1, pickerOptions_.size() - 1);
    }
    if (keysDownRepeat & KEY_L) {
      pickerSelection_ =
          pickerSelection_ > kPickerPageSize
              ? pickerSelection_ - kPickerPageSize
              : 0;
    }
    if (keysDownRepeat & KEY_R) {
      pickerSelection_ = std::min(pickerSelection_ + kPickerPageSize,
                                  pickerOptions_.size() - 1);
    }
    if (keysDown & KEY_X) {
      pickerSelection_ =
          pickerSelection_ > 5 ? pickerSelection_ - 5 : 0;
    }
    if (keysDown & KEY_Y) {
      pickerSelection_ = std::min(pickerSelection_ + 5,
                                  pickerOptions_.size() - 1);
    }
    if (keysDown & KEY_A) {
      selectPickerOption();
      return;
    }

    const size_t newPageStart =
        (pickerSelection_ / kPickerPageSize) * kPickerPageSize;
    if (newPageStart != pageStart && pageCount > 1) {
      message_ = "Page " +
                 std::to_string(newPageStart / kPickerPageSize + 1) + "/" +
                 std::to_string(pageCount);
    }
  }

  void handlePickerTouch(const touchPosition &touch) {
    constexpr unsigned kPickerListTop = 40;
    if (touch.py < kPickerListTop) {
      return;
    }
    const size_t row = (touch.py - kPickerListTop) / 8;
    const size_t pageStart =
        (pickerSelection_ / kPickerPageSize) * kPickerPageSize;
    const size_t index = pageStart + row;
    if (row < kPickerPageSize && index < pickerOptions_.size()) {
      pickerSelection_ = index;
      selectPickerOption();
    }
  }

  void selectPickerOption() {
    if (pickerSelection_ >= pickerOptions_.size()) {
      closePicker();
      return;
    }
    const PickerOption option = pickerOptions_[pickerSelection_];
    const PickerType type = pickerType_;
    closePicker();

    if (type == PickerType::Film) {
      if (option.id.empty()) {
        selectedProfile_ = -1;
        setup_.film.clear();
        setup_.developer.clear();
        setup_.dilution.clear();
        setup_.customTimeMinutes = 0.0;
        message_ = "Manual setup selected";
        return;
      }
      setup_.film = option.id;
      const std::vector<PickerOption> developers = developerPickerOptions();
      const bool hasDeveloper = std::any_of(
          developers.begin() + 1, developers.end(),
          [this](const PickerOption &candidate) {
            return candidate.id == setup_.developer;
          });
      if (!hasDeveloper) {
        setup_.developer =
            developers.size() > 1 ? developers[1].id : std::string();
      }
      selectFirstProfileForCurrentPair();
      return;
    }

    if (type == PickerType::Developer) {
      if (option.id.empty()) {
        selectedProfile_ = -1;
        setup_.developer.clear();
        setup_.dilution.clear();
        setup_.customTimeMinutes = 0.0;
        message_ = "Manual developer selected";
        return;
      }
      setup_.developer = option.id;
      const bool hasFilm = std::any_of(
          profiles_.begin(), profiles_.end(),
          [this](const agitate3ds::Profile &profile) {
            return profile.filmId == setup_.film &&
                   profile.developerId == setup_.developer;
          });
      if (!hasFilm) {
        for (const agitate3ds::Profile &profile : profiles_) {
          if (profile.developerId == setup_.developer) {
            setup_.film = profile.filmId;
            break;
          }
        }
      }
      selectFirstProfileForCurrentPair();
      return;
    }

    if (option.profileIndex >= 0 &&
        static_cast<size_t>(option.profileIndex) < profiles_.size()) {
      applyProfile(static_cast<size_t>(option.profileIndex));
    } else {
      selectedProfile_ = -1;
      setup_.dilution.clear();
      setup_.customTimeMinutes = 0.0;
      message_ = "Manual dilution selected";
    }
  }

  void cycleSelector(unsigned field, int direction, size_t step = 1) {
    const std::vector<PickerOption> options = pickerOptions(field);
    if (options.empty()) {
      message_ = "No choices available";
      return;
    }
    const size_t current = currentPickerSelection(field, options);
    const size_t count = options.size();
    const size_t offset = step % count;
    const size_t next =
        direction > 0 ? (current + offset) % count
                      : (current + count - offset) % count;
    pickerOptions_ = options;
    pickerSelection_ = next;
    pickerType_ = pickerTypeForField(field);
    selectPickerOption();
  }

  void selectFirstProfileForCurrentPair() {
    for (size_t i = 0; i < profiles_.size(); ++i) {
      if (profiles_[i].filmId == setup_.film &&
          profiles_[i].developerId == setup_.developer) {
        applyProfile(i);
        return;
      }
    }
    selectedProfile_ = -1;
    setup_.dilution.clear();
    message_ = "Choose a dilution profile";
  }

  void applyProfile(size_t index) {
    if (index >= profiles_.size()) {
      return;
    }
    selectedProfile_ = static_cast<int>(index);
    const agitate3ds::Profile &profile = profiles_[index];
    setup_.film = profile.filmId;
    setup_.developer = profile.developerId;
    setup_.dilution = profile.dilution;
    setup_.customTimeMinutes = 0.0;
    message_ = "Selected " + profile.filmName + " / " +
               profile.developerName;
  }

  double effectiveTimeMinutes() const {
    if (setup_.customTimeMinutes > 0.0) {
      return setup_.customTimeMinutes;
    }
    if (selectedProfile_ >= 0 &&
        static_cast<size_t>(selectedProfile_) < profiles_.size()) {
      return profiles_[selectedProfile_].timeMinutes;
    }
    return 10.0;
  }

  void requestProfileLoad() {
    profileReloadRequested_ = true;
    profileMessage_ =
        wifiConnected_ ? "Loading profiles..." : "Connect to Wi-Fi to load profiles.";
  }

  void beginProfileLoad() {
    profileReloadRequested_ = false;
    profileSelectionFilm_ = setup_.film;
    profileSelectionDeveloper_ = setup_.developer;
    profileSelectionDilution_ = setup_.dilution;
    profileFiles_.clear();
    profilesBeingLoaded_.clear();
    nextProfileFile_ = 0;
    skippedProfileFiles_ = 0;
    if (!api_.getAsync("/api/profiles")) {
      profileReloadRequested_ = true;
      profileMessage_ = "Could not queue profile request";
      return;
    }
    pendingRequest_ = PendingRequest::ProfileList;
    profileMessage_ = "Loading profile list...";
  }

  void handleProfileListResponse(const std::string &body) {
    agitate3ds::JsonValue root;
    std::string error;
    if (!agitate3ds::parseJson(body, root, error) ||
        root.type != agitate3ds::JsonValue::Type::Object) {
      profileMessage_ = "Invalid profile-list response";
      return;
    }
    const agitate3ds::JsonValue *files = root.get("profiles");
    if (files == nullptr ||
        files->type != agitate3ds::JsonValue::Type::Array) {
      profileMessage_ = "Profile-list response has no profiles array";
      return;
    }

    profileFiles_.clear();
    profilesBeingLoaded_.clear();
    nextProfileFile_ = 0;
    skippedProfileFiles_ = 0;
    size_t fileCount = 0;
    for (const agitate3ds::JsonValue &file : files->arrayValues) {
      if (file.type != agitate3ds::JsonValue::Type::String ||
          !validProfileFilename(file.stringValue)) {
        ++skippedProfileFiles_;
        continue;
      }
      if (fileCount >= kMaximumProfileFiles) {
        ++skippedProfileFiles_;
        continue;
      }
      ++fileCount;
      profileFiles_.push_back(file.stringValue);
    }

    profileLoadActive_ = true;
    if (profileFiles_.empty()) {
      finishProfileLoad();
    }
  }

  void finishProfileLoad() {
    profiles_.swap(profilesBeingLoaded_);
    selectedProfile_ = -1;
    for (size_t i = 0; i < profiles_.size(); ++i) {
      if (profiles_[i].filmId == profileSelectionFilm_ &&
          profiles_[i].developerId == profileSelectionDeveloper_ &&
          profiles_[i].dilution == profileSelectionDilution_) {
        selectedProfile_ = static_cast<int>(i);
        break;
      }
    }
    if (selectedProfile_ >= 0) {
      const agitate3ds::Profile &profile = profiles_[selectedProfile_];
      setup_.film = profile.filmId;
      setup_.developer = profile.developerId;
      setup_.dilution = profile.dilution;
    } else if (!profiles_.empty() && profileSelectionFilm_.empty() &&
               profileSelectionDeveloper_.empty()) {
      const std::vector<PickerOption> films = filmPickerOptions();
      if (films.size() > 1) {
        setup_.film = films[1].id;
        const std::vector<PickerOption> developers =
            developerPickerOptions();
        if (developers.size() > 1) {
          setup_.developer = developers[1].id;
        }
        selectFirstProfileForCurrentPair();
      }
    }

    char summary[96];
    std::snprintf(summary, sizeof(summary), "%lu profile(s) loaded",
                  static_cast<unsigned long>(profiles_.size()));
    profileMessage_ = summary;
    if (skippedProfileFiles_ > 0) {
      char skipped[96];
      std::snprintf(skipped, sizeof(skipped), "; %lu skipped: %s",
                    static_cast<unsigned long>(skippedProfileFiles_),
                    profileLoadError_.c_str());
      profileMessage_ += skipped;
    }
    profileLoadActive_ = false;
  }

  void processApiResponse() {
    agitate3ds::ApiResponse response;
    if (!api_.takeResponse(response)) {
      return;
    }

    const PendingRequest completedRequest = pendingRequest_;
    pendingRequest_ = PendingRequest::None;
    switch (completedRequest) {
      case PendingRequest::Discovery:
        if (response.ok && !response.body.empty()) {
          const std::string discoveredUrl = "http://" + response.body;
          if (api_.setServerUrl(discoveredUrl)) {
            serverAddress_ = discoveredUrl;
            manualServerAddress_ = false;
            serverReachable_ = false;
            connectionMessage_ = "Found controller at " + response.body;
          } else {
            serverAddress_ = agitate3ds::kFallbackServerUrl;
            if (api_.setServerUrl(serverAddress_)) {
              connectionMessage_ =
                  "Could not use discovered address; trying the ESP32 AP address";
            } else {
              connectionMessage_ = "Could not set the controller address";
            }
          }
        } else {
          serverAddress_ = agitate3ds::kFallbackServerUrl;
          if (api_.setServerUrl(serverAddress_)) {
            connectionMessage_ =
                "LAN scan found no controller; trying direct AP";
          } else {
            connectionMessage_ = response.error.empty()
                                     ? "LAN discovery failed"
                                     : response.error;
          }
          discoveryRequested_ = !manualServerAddress_;
          nextDiscoveryFrame_ = frame_ + kDiscoveryRetryFrames;
        }
        requestProfileLoad();
        statusRefreshRequested_ = true;
        break;
      case PendingRequest::Status:
        if (!response.ok) {
          serverReachable_ = false;
          profileReloadRequested_ = false;
          if (!manualServerAddress_) {
            connectionMessage_ = "Controller unreachable; retrying LAN scan";
            discoveryRequested_ = true;
            nextDiscoveryFrame_ = frame_ + kDiscoveryRetryFrames;
          } else {
            connectionMessage_ = responseError(response);
          }
        } else if (parseStatusResponse(response.body)) {
          lastStatusFrame_ = frame_;
          serverReachable_ = true;
          connectionMessage_.clear();
          if (profiles_.empty() && !profileLoadActive_) {
            requestProfileLoad();
          }
        }
        break;
      case PendingRequest::ProfileList:
        if (!response.ok) {
          profileMessage_ = responseError(response);
        } else {
          handleProfileListResponse(response.body);
        }
        break;
      case PendingRequest::ProfileFile:
        if (!response.ok || response.body.size() > kMaximumProfileFileBytes) {
          ++skippedProfileFiles_;
          profileLoadError_ =
              response.ok ? "profile file exceeds 256 KiB limit"
                          : responseError(response);
        } else {
          std::string error;
          if (!agitate3ds::parseProfiles(response.body, profilesBeingLoaded_,
                                        kMaximumProfiles, error) ||
              !error.empty()) {
            ++skippedProfileFiles_;
            profileLoadError_ = error.empty() ? "profile JSON is invalid" : error;
          }
        }
        ++nextProfileFile_;
        break;
      case PendingRequest::Control:
        if (!response.ok) {
          message_ = responseError(response);
          serverReachable_ = false;
          if (!manualServerAddress_) {
            discoveryRequested_ = true;
            nextDiscoveryFrame_ = frame_ + kDiscoveryRetryFrames;
          }
        } else if (parseStatusResponse(response.body)) {
          lastStatusFrame_ = frame_;
          serverReachable_ = true;
          message_ = kControlSuccessMessages[pendingControl_];
          connectionMessage_.clear();
        }
        break;
      case PendingRequest::None:
        break;
    }
  }

  void dispatchNextRequest() {
    if (!wifiConnected_ || pendingRequest_ != PendingRequest::None) {
      return;
    }

    if (serverAddressUpdateRequested_) {
      if (!api_.setServerUrl(serverAddress_)) {
        return;
      }
      serverAddressUpdateRequested_ = false;
      serverReachable_ = false;
      discoveryRequested_ = false;
    }

    if (discoveryRequested_ && frame_ >= nextDiscoveryFrame_) {
      if (api_.discoverAsync()) {
        pendingRequest_ = PendingRequest::Discovery;
        discoveryRequested_ = false;
        connectionMessage_ = "Searching LAN for agitate-esp";
      }
      return;
    }

    if (!controlQueue_.empty()) {
      const ControlCommand &command = controlQueue_.front();
      if (api_.postAsync(command.path, command.body)) {
        pendingControl_ = command.control;
        pendingRequest_ = PendingRequest::Control;
        controlQueue_.pop_front();
        message_ = "Sending control request...";
      }
      return;
    }

    if (profileLoadActive_) {
      while (nextProfileFile_ < profileFiles_.size() &&
             profilesBeingLoaded_.size() >= kMaximumProfiles) {
        ++skippedProfileFiles_;
        ++nextProfileFile_;
      }
      if (nextProfileFile_ >= profileFiles_.size()) {
        finishProfileLoad();
      } else {
        const std::string path =
            "/profiles/" +
            encodePathComponent(profileFiles_[nextProfileFile_]);
        if (api_.getAsync(path)) {
          pendingRequest_ = PendingRequest::ProfileFile;
          return;
        }
        profileLoadActive_ = false;
        profileMessage_ = "Could not queue profile request";
        return;
      }
    }

    if (profileReloadRequested_) {
      beginProfileLoad();
      return;
    }

    if (statusRefreshRequested_) {
      if (api_.getAsync("/api/status")) {
        pendingRequest_ = PendingRequest::Status;
        statusRefreshRequested_ = false;
      } else {
        statusRefreshRequested_ = false;
        connectionMessage_ = "Could not queue status request";
      }
    }
  }

  bool parseStatusResponse(const std::string &body) {
    agitate3ds::WorkflowStatus nextStatus;
    std::string error;
    if (!agitate3ds::parseWorkflowStatus(body, nextStatus, error)) {
      connectionMessage_ = error;
      return false;
    }
    status_ = nextStatus;
    haveStatus_ = true;
    return true;
  }

  std::string responseError(const agitate3ds::ApiResponse &response) const {
    agitate3ds::JsonValue root;
    std::string parseError;
    if (!response.body.empty() &&
        agitate3ds::parseJson(response.body, root, parseError)) {
      const agitate3ds::JsonValue *error = root.get("error");
      if (error != nullptr &&
          error->type == agitate3ds::JsonValue::Type::String &&
          !error->stringValue.empty()) {
        return error->stringValue;
      }
    }
    return response.error.empty() ? "Request failed" : response.error;
  }

  void renderTop() const {
    printLine("AGITATE-ESP / NINTENDO 3DS");
    printLine("----------------------------------------");
    printLine(wifiConnected_ ? "Wi-Fi: Connected" : "Wi-Fi: Not connected");
    if (!wifiConnected_) {
      printLine("Join home Wi-Fi or the agitate-esp AP");
    } else if (pendingRequest_ == PendingRequest::Discovery) {
      printLine("Server: Searching LAN");
    } else if (haveFreshStatus()) {
      printLine("Server: Connected");
      printLine("Host: " + serverAddress_.substr(7));
    } else if (haveStatus()) {
      printLine("Server: Disconnected; values may be stale");
    } else {
      printLine(discoveryRequested_ ? "Server: Retrying LAN discovery"
                                    : "Server: Checking controller");
    }

    printLine("");
    printLine("ACTIVE STEP");
    if (!haveFreshStatus()) {
      printLine("Status: --");
      printLine("Remaining: --:--");
      printLine("Motor: --");
    } else {
      printLine("Status: " + currentStateText());
      printLine("Step: " + status_.stepName);
      printLine("Remaining: " + formatTime(status_.stepRemainingSec));
      printLine(status_.motorActive ? "Motor: AGITATING" : "Motor: Resting");
    }

    printLine("");
    printLine("PROCESS");
    if (haveStatus_) {
      for (size_t i = 0; i < status_.steps.size(); ++i) {
        const char marker =
            status_.running && i == status_.stepIndex ? '>' : ' ';
        char line[64];
        std::snprintf(line, sizeof(line), "%c %lu. %s %s", marker,
                      static_cast<unsigned long>(i + 1),
                      safeText(status_.steps[i].name, 20).c_str(),
                      formatTime(status_.steps[i].durationSec).c_str());
        printLine(line);
      }
    } else {
      printLine("Workflow list unavailable");
    }

    printLine("");
    printLine("Setup: " + selectedProfileLabel());
    char developerLine[48];
    std::snprintf(developerLine, sizeof(developerLine),
                  "Dev: %.2f min; fixer: %s", effectiveTimeMinutes(),
                  safeText(setup_.fixer, 20).c_str());
    printLine(developerLine);
    if (!connectionMessage_.empty()) {
      printLine(connectionMessage_);
    }
  }

  void renderControls() const {
    printLine("CONTROLS - Left/Right: Setup");
    printLine("Touch row or D-pad + A");
    printLine("");
    printControlRow(0, "A", startLabel());
    printControlRow(1, "B", status_.paused ? "Resume" : "Pause");
    printControlRow(2, "X", "Restart step");
    printControlRow(3, "Y", "Next / Skip");
    printControlRow(4, "R", "Stop process");
    printControlRow(5, "L", "Test motor");
    printControlRow(6, "SEL", "Aggressive agitation");

    char profileLine[40];
    std::snprintf(profileLine, sizeof(profileLine), "Profiles: %lu",
                  static_cast<unsigned long>(profiles_.size()));
    printLine(profileLine);
    if (!message_.empty()) {
      printLine(message_);
    }
    printLine("START exits");
  }

  void renderSetup() const {
    printLine("SETUP - Left/Right: Controls");
    printLine("A/touch list; Select edits manual");
    printLine("D-pad cycles; L/R +16; X/Y +64");
    printLine("");
    printSetupRow(0, "Film", filmSelectionLabel());
    printSetupRow(1, "Developer", developerSelectionLabel());
    printSetupRow(2, "Dilution", dilutionSelectionLabel());
    printSetupRow(3, "Fixer", setup_.fixer);
    if (setup_.customTimeMinutes > 0.0) {
      char time[32];
      std::snprintf(time, sizeof(time), "%.2f min (custom)",
                    setup_.customTimeMinutes);
      printSetupRow(4, "Dev time", time);
    } else {
      printSetupRow(4, "Dev time",
                    formatDuration(effectiveTimeMinutes()) + " min (profile)");
    }
    printSetupRow(5, "Find controller", "A / tap");
    printSetupRow(6, "Reload profiles", "A / tap");
    printSetupRow(7, "Controller IP", controllerIpLabel());
    if (editingControllerIp_) {
      printLine("B cancels address edit");
    }

    printLine("");
    printLine(profileMessage_);
    if (!message_.empty()) {
      printLine(message_);
    }
    printLine("START exits");
  }

  void renderPicker() const {
    const size_t pageStart =
        (pickerSelection_ / kPickerPageSize) * kPickerPageSize;
    const size_t pageEnd =
        std::min(pageStart + kPickerPageSize, pickerOptions_.size());
    const char *title = pickerType_ == PickerType::Film
                            ? "SELECT FILM"
                            : pickerType_ == PickerType::Developer
                                  ? "SELECT DEVELOPER"
                                  : "SELECT DILUTION / TIME";
    printLine(title);
    printLine("Touch a choice; Up/Down moves");
    printLine("L/R page; X/Y skip; A select; B back");
    printLine("");

    char page[40];
    std::snprintf(page, sizeof(page), "Page %lu/%lu",
                  static_cast<unsigned long>(pageStart / kPickerPageSize + 1),
                  static_cast<unsigned long>(
                      (pickerOptions_.size() + kPickerPageSize - 1) /
                      kPickerPageSize));
    printLine(page);
    for (size_t i = pageStart; i < pageEnd; ++i) {
      char line[48];
      std::snprintf(line, sizeof(line), "%c %s",
                    i == pickerSelection_ ? '>' : ' ',
                    safeText(pickerOptions_[i].label, 37).c_str());
      printLine(line);
    }
  }

  void printLine(const std::string &line) const {
    std::printf("%s\n", safeText(line, 40).c_str());
  }

  void printControlRow(unsigned item,
                       const char *button,
                       const std::string &label) const {
    char line[48];
    std::snprintf(line, sizeof(line), "%c [%s] %s",
                  selectedControl_ == item ? '>' : ' ', button,
                  safeText(label, 28).c_str());
    printLine(line);
    printLine("");
  }

  void printSetupRow(unsigned item,
                     const char *name,
                     const std::string &value) const {
    char line[64];
    std::snprintf(line, sizeof(line), "%c %-15s %s",
                  setupSelection_ == item ? '>' : ' ', name,
                  safeText(value, 20).c_str());
    printLine(line);
    printLine("");
  }

  std::string selectedProfileLabel() const {
    if (selectedProfile_ < 0 ||
        static_cast<size_t>(selectedProfile_) >= profiles_.size()) {
      return "Manual setup";
    }
    const agitate3ds::Profile &profile = profiles_[selectedProfile_];
    std::string label = profile.filmName + " / " + profile.developerName +
                        " / " + profile.dilution;
    if (!profile.asaIso.empty()) {
      label += " @ ISO " + profile.asaIso;
    }
    return label;
  }

  std::string filmSelectionLabel() const {
    if (selectedProfile_ >= 0 &&
        static_cast<size_t>(selectedProfile_) < profiles_.size()) {
      return profiles_[selectedProfile_].filmName;
    }
    return setup_.film.empty() ? "Manual setup" : setup_.film;
  }

  std::string developerSelectionLabel() const {
    if (selectedProfile_ >= 0 &&
        static_cast<size_t>(selectedProfile_) < profiles_.size()) {
      return profiles_[selectedProfile_].developerName;
    }
    return setup_.developer.empty() ? "(select developer)" : setup_.developer;
  }

  std::string dilutionSelectionLabel() const {
    if (selectedProfile_ < 0 ||
        static_cast<size_t>(selectedProfile_) >= profiles_.size()) {
      return setup_.dilution.empty() ? "(select film/developer)"
                                     : setup_.dilution;
    }
    const agitate3ds::Profile &profile = profiles_[selectedProfile_];
    std::string label = profile.dilution;
    if (!profile.asaIso.empty()) {
      label += " ISO " + profile.asaIso;
    }
    label += " " + formatDuration(profile.timeMinutes) + "m";
    return label;
  }

  std::string startLabel() const {
    if (!haveFreshStatus()) {
      return "Start (status required)";
    }
    if (!status_.running) {
      return "Start";
    }
    if (status_.waitingForStart) {
      return "Start Step";
    }
    if (status_.paused) {
      return "Resume";
    }
    if (status_.waitingForNext) {
      return "Waiting for Next/Skip";
    }
    return "Step active";
  }

  std::string currentStateText() const {
    if (!status_.running) {
      return "Idle";
    }
    if (status_.waitingForNext) {
      return "Ready - change chemical, then Next";
    }
    if (status_.waitingForStart) {
      return "Ready - press Start Step";
    }
    if (status_.paused) {
      return "Paused";
    }
    return "Running";
  }

  bool haveStatus() const {
    return haveStatus_;
  }

  bool haveFreshStatus() const {
    return haveStatus_ && frame_ - lastStatusFrame_ <= kStaleAfterFrames;
  }

  static std::string formatTime(unsigned seconds) {
    char formatted[16];
    std::snprintf(formatted, sizeof(formatted), "%lu:%02lu",
                  static_cast<unsigned long>(seconds / 60),
                  static_cast<unsigned long>(seconds % 60));
    return formatted;
  }

  static std::string formatDuration(double minutes) {
    char formatted[24];
    std::snprintf(formatted, sizeof(formatted), "%.2f", minutes);
    std::string result(formatted);
    while (!result.empty() && result.back() == '0') {
      result.pop_back();
    }
    if (!result.empty() && result.back() == '.') {
      result.pop_back();
    }
    return result;
  }

  static std::string safeText(const std::string &text, size_t maximumLength) {
    std::string safe;
    safe.reserve(std::min(text.size(), maximumLength));
    for (unsigned char ch : text) {
      if (safe.size() >= maximumLength) {
        break;
      }
      safe.push_back(ch < 0x20 || ch > 0x7e ? '?' : static_cast<char>(ch));
    }
    return safe;
  }

  std::string controllerIpLabel() const {
    char address[40];
    if (editingControllerIp_) {
      std::snprintf(address, sizeof(address), "%s%u%s.%s%u%s.%s%u%s.%s%u%s",
                    selectedIpOctet_ == 0 ? "[" : "",
                    controllerIpOctets_[0],
                    selectedIpOctet_ == 0 ? "]" : "",
                    selectedIpOctet_ == 1 ? "[" : "",
                    controllerIpOctets_[1],
                    selectedIpOctet_ == 1 ? "]" : "",
                    selectedIpOctet_ == 2 ? "[" : "",
                    controllerIpOctets_[2],
                    selectedIpOctet_ == 2 ? "]" : "",
                    selectedIpOctet_ == 3 ? "[" : "",
                    controllerIpOctets_[3],
                    selectedIpOctet_ == 3 ? "]" : "");
    } else {
      const std::string ip = serverAddress_.substr(7);
      return ip;
    }
    return address;
  }

  static bool validProfileFilename(const std::string &filename) {
    if (filename.empty() || filename.size() > 128 ||
        filename == "." || filename == ".." ||
        filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.size() < 5 ||
        filename.compare(filename.size() - 5, 5, ".json") != 0) {
      return false;
    }
    for (unsigned char ch : filename) {
      if (ch < 0x20 || ch == 0x7f) {
        return false;
      }
    }
    return true;
  }

  static std::string encodePathComponent(const std::string &value) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char ch : value) {
      if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
          ch == '.' || ch == '~') {
        encoded.push_back(static_cast<char>(ch));
      } else {
        encoded.push_back('%');
        encoded.push_back(kHex[ch >> 4]);
        encoded.push_back(kHex[ch & 0x0f]);
      }
    }
    return encoded;
  }

  agitate3ds::ApiClient api_;
  agitate3ds::WorkflowStatus status_;
  agitate3ds::ProcessSetup setup_;
  std::vector<agitate3ds::Profile> profiles_;
  std::string message_;
  std::string profileMessage_ = "Connect to Wi-Fi to load profiles.";
  std::string connectionMessage_;
  std::string serverAddress_ = agitate3ds::kFallbackServerUrl;
  Tab tab_;
  unsigned setupSelection_;
  unsigned selectedControl_;
  int selectedProfile_;
  int fixerSelection_;
  unsigned frame_;
  unsigned nextStatusFrame_;
  unsigned lastStatusFrame_;
  bool wifiConnected_;
  bool wasWifiConnected_;
  bool haveStatus_;
  bool shouldExit_;
  bool apiReady_;
  PendingRequest pendingRequest_;
  unsigned pendingControl_;
  bool profileReloadRequested_;
  bool profileLoadActive_;
  size_t nextProfileFile_;
  size_t skippedProfileFiles_;
  bool statusRefreshRequested_;
  bool serverReachable_;
  bool discoveryRequested_;
  unsigned nextDiscoveryFrame_;
  bool serverAddressUpdateRequested_;
  bool manualServerAddress_;
  bool editingControllerIp_;
  unsigned selectedIpOctet_;
  unsigned controllerIpOctets_[4] = {192, 168, 1, 224};
  std::vector<std::string> profileFiles_;
  std::vector<agitate3ds::Profile> profilesBeingLoaded_;
  std::string profileSelectionFilm_;
  std::string profileSelectionDeveloper_;
  std::string profileSelectionDilution_;
  std::string profileLoadError_;
  PickerType pickerType_;
  std::vector<PickerOption> pickerOptions_;
  size_t pickerSelection_;
  std::deque<ControlCommand> controlQueue_;
};

}  // namespace

int main() {
  gfxInitDefault();
  PrintConsole topConsole;
  PrintConsole bottomConsole;
  consoleInit(GFX_TOP, &topConsole);
  consoleInit(GFX_BOTTOM, &bottomConsole);
  gfxSetDoubleBuffering(GFX_TOP, true);
  gfxSetDoubleBuffering(GFX_BOTTOM, true);

  Application app;
  app.render(topConsole, bottomConsole);
  gfxFlushBuffers();
  gfxSwapBuffers();
  gspWaitForVBlank();

  app.initialize();

  while (aptMainLoop() && !app.shouldExit()) {
    hidScanInput();
    app.update(hidKeysDown(), hidKeysDownRepeat());
    app.render(topConsole, bottomConsole);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
  }

  app.shutdown();
  gfxExit();
  return 0;
}
