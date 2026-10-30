#include "StepperAgitator.h"

#include <math.h>

namespace {
// Bit i of each entry drives coil pin i (IN1..IN4).
constexpr uint8_t kHalfStepSequence[8] = {
    0b0001,
    0b0011,
    0b0010,
    0b0110,
    0b0100,
    0b1100,
    0b1000,
    0b1001,
};

// A 28BYJ-48 stalls above roughly 15 RPM at 5 V.
constexpr float kMinRpm = 1.0f;
constexpr float kMaxRpm = 15.0f;

// Steps emitted per update() call, so the async web server keeps getting time.
constexpr uint8_t kMaxStepsPerUpdate = 4;
}  // namespace

StepperAgitator::StepperAgitator(uint8_t in1, uint8_t in2, uint8_t in3, uint8_t in4, uint16_t stepsPerRevolution)
    : pins_{in1, in2, in3, in4},
      stepsPerRevolution_(stepsPerRevolution == 0 ? config::kStepsPerRevolution : stepsPerRevolution),
      speedRpm_(config::kAgitationRpm),
      stepIntervalUs_(1000),
      lastStepUs_(0),
      motionMode_(MotionMode::Idle),
      stepsRemaining_(0),
      direction_(1),
      sequenceIndex_(0),
      motionId_(0),
      baseRpm_(config::kAgitationRpm),
      sweepSteps_(0),
      sweepLegsRemaining_(0),
      sweepPauseMs_(0),
      sweepPauseStartMs_(0) {
  setSpeedRpm(speedRpm_);
}

void StepperAgitator::begin() {
  for (uint8_t pin : pins_) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
}

void StepperAgitator::setSpeedRpm(float rpm) {
  applySpeedRpm(rpm);
  baseRpm_ = speedRpm_;
}

void StepperAgitator::applySpeedRpm(float rpm) {
  speedRpm_ = constrain(rpm, kMinRpm, kMaxRpm);
  const float usPerStep = 60000000.0f / (speedRpm_ * static_cast<float>(stepsPerRevolution_));
  stepIntervalUs_ = static_cast<uint32_t>(usPerStep < 800.0f ? 800.0f : usPerStep);
}

uint16_t StepperAgitator::stepsForDegrees(float degrees) const {
  const float steps = fabsf(degrees) * static_cast<float>(stepsPerRevolution_) / 360.0f;
  return static_cast<uint16_t>(steps + 0.5f);
}

void StepperAgitator::rotateDegrees(float degrees) {
  if (isBusy() || degrees == 0.0f) {
    return;
  }

  const uint16_t steps = stepsForDegrees(degrees);
  if (steps == 0) {
    return;
  }

  motionMode_ = MotionMode::Rotate;
  startStepMotion(steps, degrees > 0.0f ? 1 : -1);
}

void StepperAgitator::startTimedSweep(float degrees,
                                      uint32_t pauseMsBetweenDirections,
                                      float rpmOverride,
                                      uint8_t cycles) {
  if (isBusy() || degrees == 0.0f) {
    return;
  }

  sweepSteps_ = stepsForDegrees(degrees);
  if (sweepSteps_ == 0) {
    return;
  }

  if (rpmOverride > 0.0f) {
    applySpeedRpm(rpmOverride);
  }

  sweepPauseMs_ = pauseMsBetweenDirections;
  sweepLegsRemaining_ = static_cast<uint16_t>(cycles == 0 ? 1 : cycles) * 2;
  motionMode_ = MotionMode::Sweep;
  startStepMotion(sweepSteps_, degrees > 0.0f ? 1 : -1);
}

void StepperAgitator::startStepMotion(uint16_t steps, int8_t direction) {
  ++motionId_;
  stepsRemaining_ = steps;
  direction_ = direction >= 0 ? 1 : -1;
  lastStepUs_ = micros() - stepIntervalUs_;
}

void StepperAgitator::update(uint32_t nowMs) {
  if (motionMode_ == MotionMode::Idle) {
    return;
  }

  if (motionMode_ == MotionMode::SweepPause) {
    if (nowMs - sweepPauseStartMs_ >= sweepPauseMs_) {
      // Each leg runs opposite to the previous one.
      motionMode_ = MotionMode::Sweep;
      stepsRemaining_ = sweepSteps_;
      direction_ = static_cast<int8_t>(-direction_);
      lastStepUs_ = micros() - stepIntervalUs_;
    }
    return;
  }

  const uint32_t nowUs = micros();
  for (uint8_t emitted = 0; stepsRemaining_ > 0 && emitted < kMaxStepsPerUpdate; ++emitted) {
    if (nowUs - lastStepUs_ < stepIntervalUs_) {
      break;
    }

    lastStepUs_ += stepIntervalUs_;
    if (nowUs - lastStepUs_ > stepIntervalUs_ * 4UL) {
      lastStepUs_ = nowUs;  // resync instead of sprinting after a long stall
    }

    stepOnce();
    --stepsRemaining_;
  }

  if (stepsRemaining_ > 0) {
    return;
  }

  if (motionMode_ == MotionMode::Sweep) {
    --sweepLegsRemaining_;
    if (sweepLegsRemaining_ == 0) {
      stop();
      return;
    }

    motionMode_ = MotionMode::SweepPause;
    sweepPauseStartMs_ = nowMs;
    releaseCoils();
    return;
  }

  stop();
}

void StepperAgitator::stepOnce() {
  sequenceIndex_ = static_cast<uint8_t>((sequenceIndex_ + direction_ + 8) % 8);
  const uint8_t pattern = kHalfStepSequence[sequenceIndex_];
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(pins_[i], ((pattern >> i) & 0x01) ? HIGH : LOW);
  }
}

void StepperAgitator::releaseCoils() {
  for (uint8_t pin : pins_) {
    digitalWrite(pin, LOW);
  }
}

void StepperAgitator::stop() {
  motionMode_ = MotionMode::Idle;
  stepsRemaining_ = 0;
  sweepLegsRemaining_ = 0;
  applySpeedRpm(baseRpm_);
  releaseCoils();
}

bool StepperAgitator::isBusy() const {
  return motionMode_ != MotionMode::Idle;
}
