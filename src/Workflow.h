#pragma once

#include <Arduino.h>

#include "StepperAgitator.h"

struct ProcessStep {
  const char *name;
  uint32_t durationSec;
  uint16_t agitationInitialSec;
  uint16_t agitationIntervalSec;
  uint16_t agitationDurationSec;
};

class Workflow {
 public:
  Workflow();

  void configureDeveloper(const String &filmId,
                          const String &developerId,
                          const String &fixerName,
                          const String &dilution,
                          uint32_t developerDurationSec);

  void start(uint32_t nowMs);
  void stop();
  void togglePause(uint32_t nowMs);
  void restartCurrentStep(uint32_t nowMs);
  void nextStep(uint32_t nowMs);
  void update(uint32_t nowMs, StepperAgitator &agitator);

  bool isRunning() const;
  bool isPaused() const;
  bool isWaitingForNext() const;
  bool isWaitingForStart() const;
  bool isAgitating() const;

  uint8_t currentStepIndex() const;
  uint8_t totalSteps() const;
  const ProcessStep &currentStep() const;
  const ProcessStep &stepAt(uint8_t index) const;

  uint32_t elapsedStepSec(uint32_t nowMs) const;
  uint32_t remainingStepSec(uint32_t nowMs) const;

  const String &filmId() const;
  const String &developerId() const;
  const String &fixerName() const;
  const String &dilution() const;

 private:
  static constexpr uint8_t kStepCount = 4;

  bool agitationWanted(uint32_t elapsedSec) const;
  void advanceStep(uint32_t nowMs);

  ProcessStep steps_[kStepCount];
  uint8_t stepIndex_;
  bool running_;
  bool paused_;
  bool waitingForNext_;
  bool waitingForStart_;
  bool agitating_;
  uint32_t scheduledMotionId_;  // StepperAgitator::motionId() of the last sweep this workflow started

  uint32_t stepStartMs_;
  uint32_t pauseStartedMs_;
  uint32_t pausedAccumulatedMs_;

  String filmId_;
  String developerId_;
  String fixerName_;
  String dilution_;
};
