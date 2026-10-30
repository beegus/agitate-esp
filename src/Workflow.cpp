#include "Workflow.h"

#include "Config.h"

namespace {
constexpr uint32_t kMinDeveloperSec = 30;
}  // namespace

Workflow::Workflow()
    : steps_{{"Pre-soak", 60, 10, 30, 4},
             {"Developer", 600, 10, 60, 5},
             {"Stop bath", 45, 45, 0, 0},
             {"Fixer", 300, 10, 60, 5}},
      stepIndex_(0),
      running_(false),
      paused_(false),
      waitingForNext_(false),
      waitingForStart_(false),
      agitating_(false),
      scheduledMotionId_(0),
      stepStartMs_(0),
      pauseStartedMs_(0),
      pausedAccumulatedMs_(0),
      filmId_(""),
      developerId_(""),
      fixerName_(""),
      dilution_("Custom") {}

void Workflow::configureDeveloper(const String &filmId,
                                  const String &developerId,
                                  const String &fixerName,
                                  const String &dilution,
                                  uint32_t developerDurationSec) {
  filmId_ = filmId;
  developerId_ = developerId;
  fixerName_ = fixerName;
  dilution_ = dilution;

  steps_[1].durationSec = developerDurationSec < kMinDeveloperSec ? kMinDeveloperSec : developerDurationSec;
}

void Workflow::start(uint32_t nowMs) {
  if (running_) {
    if (waitingForStart_) {
      waitingForStart_ = false;
      paused_ = false;
      stepStartMs_ = nowMs;
      pausedAccumulatedMs_ = 0;
      pauseStartedMs_ = 0;
      return;
    }

    if (paused_) {
      paused_ = false;
      pausedAccumulatedMs_ += nowMs - pauseStartedMs_;
      pauseStartedMs_ = 0;
    }
    return;
  }

  running_ = true;
  paused_ = false;
  waitingForNext_ = false;
  waitingForStart_ = false;
  agitating_ = false;
  stepIndex_ = 0;
  stepStartMs_ = nowMs;
  pauseStartedMs_ = 0;
  pausedAccumulatedMs_ = 0;
}

void Workflow::stop() {
  running_ = false;
  paused_ = false;
  agitating_ = false;
  waitingForNext_ = false;
  waitingForStart_ = false;
  stepIndex_ = 0;
}

void Workflow::togglePause(uint32_t nowMs) {
  if (!running_ || waitingForNext_ || waitingForStart_) {
    return;
  }

  if (!paused_) {
    paused_ = true;
    pauseStartedMs_ = nowMs;
    agitating_ = false;
    return;
  }

  paused_ = false;
  pausedAccumulatedMs_ += nowMs - pauseStartedMs_;
  pauseStartedMs_ = 0;
}

void Workflow::restartCurrentStep(uint32_t nowMs) {
  if (!running_) {
    return;
  }
  stepStartMs_ = nowMs;
  pausedAccumulatedMs_ = 0;
  pauseStartedMs_ = 0;
  paused_ = false;
  waitingForNext_ = false;
  waitingForStart_ = false;
  agitating_ = false;
}

void Workflow::nextStep(uint32_t nowMs) {
  if (!running_) {
    return;
  }

  advanceStep(nowMs);
}

void Workflow::update(uint32_t nowMs, StepperAgitator &agitator) {
  if (!running_) {
    agitating_ = false;
    return;
  }

  if (paused_) {
    if (agitator.isBusy()) {
      agitator.stop();
    }
    agitating_ = false;
    return;
  }

  if (waitingForNext_ || waitingForStart_) {
    if (agitator.isBusy()) {
      agitator.stop();
    }
    agitating_ = false;
    return;
  }

  const uint32_t elapsedSec = elapsedStepSec(nowMs);
  if (elapsedSec >= currentStep().durationSec) {
    if (agitator.isBusy()) {
      agitator.stop();
    }
    waitingForNext_ = true;
    agitating_ = false;
    return;
  }

  const bool shouldAgitate = agitationWanted(elapsedSec);
  agitating_ = shouldAgitate;

  if (shouldAgitate && !agitator.isBusy()) {
    agitator.startTimedSweep(
        config::kAutomaticAgitationSweepDegrees, config::kAgitationSweepPauseMs, config::kAgitationRpm);
    scheduledMotionId_ = agitator.motionId();
  } else if (!shouldAgitate && agitator.isBusy() && agitator.motionId() == scheduledMotionId_) {
    // A sweep outlasts a short window; cut it off so the motor runs only for the
    // configured agitation time. Manual (aggressive) motion is left alone.
    agitator.stop();
  }
}

bool Workflow::agitationWanted(uint32_t elapsedSec) const {
  // Initial agitation covers [0, initial); afterwards agitate for `duration`
  // seconds at the start of every `interval`, measured from the step start
  // (e.g. 10 s, then 5 s at 1:00, 2:00, ...).
  const ProcessStep &step = currentStep();
  if (elapsedSec < step.agitationInitialSec) {
    return true;
  }

  if (step.agitationIntervalSec == 0 || step.agitationDurationSec == 0) {
    return false;
  }

  return elapsedSec % step.agitationIntervalSec < step.agitationDurationSec;
}

void Workflow::advanceStep(uint32_t nowMs) {
  agitating_ = false;
  waitingForNext_ = false;
  waitingForStart_ = false;
  if (stepIndex_ + 1 >= kStepCount) {
    stop();
    return;
  }

  ++stepIndex_;
  stepStartMs_ = nowMs;
  pausedAccumulatedMs_ = 0;
  paused_ = false;
  pauseStartedMs_ = 0;
  waitingForStart_ = true;
}

bool Workflow::isRunning() const {
  return running_;
}

bool Workflow::isPaused() const {
  return paused_;
}

bool Workflow::isWaitingForNext() const {
  return waitingForNext_;
}

bool Workflow::isWaitingForStart() const {
  return waitingForStart_;
}

bool Workflow::isAgitating() const {
  return agitating_;
}

uint8_t Workflow::currentStepIndex() const {
  return stepIndex_;
}

uint8_t Workflow::totalSteps() const {
  return kStepCount;
}

const ProcessStep &Workflow::currentStep() const {
  return steps_[stepIndex_];
}

const ProcessStep &Workflow::stepAt(uint8_t index) const {
  return steps_[index < kStepCount ? index : (kStepCount - 1)];
}

uint32_t Workflow::elapsedStepSec(uint32_t nowMs) const {
  if (!running_) {
    return 0;
  }

  uint32_t effectivePausedMs = pausedAccumulatedMs_;
  if (paused_) {
    effectivePausedMs += nowMs - pauseStartedMs_;
  }

  // A timestamp taken just before the step (re)started would otherwise wrap
  // around to ~49 days and end the step immediately.
  const int32_t elapsedMs = static_cast<int32_t>(nowMs - stepStartMs_ - effectivePausedMs);
  return elapsedMs > 0 ? static_cast<uint32_t>(elapsedMs) / 1000 : 0;
}

uint32_t Workflow::remainingStepSec(uint32_t nowMs) const {
  if (!running_ || waitingForStart_) {
    return 0;
  }

  const uint32_t elapsed = elapsedStepSec(nowMs);
  const uint32_t duration = currentStep().durationSec;
  return elapsed >= duration ? 0 : (duration - elapsed);
}

const String &Workflow::filmId() const {
  return filmId_;
}

const String &Workflow::developerId() const {
  return developerId_;
}

const String &Workflow::fixerName() const {
  return fixerName_;
}

const String &Workflow::dilution() const {
  return dilution_;
}
