#pragma once

#include <string>
#include <vector>

#include "protocol.hpp"

namespace agitate3ds {

struct WorkflowStep {
  std::string name;
  unsigned durationSec = 0;
};

struct WorkflowStatus {
  bool running = false;
  bool paused = false;
  bool waitingForNext = false;
  bool waitingForStart = false;
  bool motorActive = false;
  unsigned stepIndex = 0;
  unsigned stepRemainingSec = 0;
  std::string stepName;
  std::vector<WorkflowStep> steps;
};

struct Profile {
  std::string filmId;
  std::string filmName;
  std::string developerId;
  std::string developerName;
  std::string dilution;
  std::string asaIso;
  std::string notes;
  double timeMinutes = 0.0;
};

struct ProcessSetup {
  std::string film;
  std::string developer;
  std::string fixer = kDefaultFixer;
  std::string dilution = "Custom";
  double customTimeMinutes = 0.0;
};

bool parseWorkflowStatus(const std::string &text,
                         WorkflowStatus &status,
                         std::string &error);
bool parseProfiles(const std::string &text,
                   std::vector<Profile> &profiles,
                   size_t maximumProfiles,
                   std::string &error);
bool makeStartPayload(const ProcessSetup &setup,
                      double durationMinutes,
                      std::string &payload,
                      std::string &error);

}  // namespace agitate3ds
