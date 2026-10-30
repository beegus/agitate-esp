#pragma once

#include <Arduino.h>

#include "Config.h"

class StepperAgitator {
 public:
  StepperAgitator(uint8_t in1,
                  uint8_t in2,
                  uint8_t in3,
                  uint8_t in4,
                  uint16_t stepsPerRevolution = config::kStepsPerRevolution);

  void begin();
  void update(uint32_t nowMs);

  void setSpeedRpm(float rpm);
  float speedRpm() const { return speedRpm_; }

  void rotateDegrees(float degrees);
  void startTimedSweep(float degrees,
                       uint32_t pauseMsBetweenDirections,
                       float rpmOverride = -1.0f,
                       uint8_t cycles = 1);

  void stop();
  bool isBusy() const;
  // Changes every time a new motion starts, so callers can tell whose motion is running.
  uint32_t motionId() const { return motionId_; }

 private:
  enum class MotionMode : uint8_t {
    Idle,
    Rotate,
    Sweep,
    SweepPause
  };

  void applySpeedRpm(float rpm);
  uint16_t stepsForDegrees(float degrees) const;
  void startStepMotion(uint16_t steps, int8_t direction);
  void stepOnce();
  void releaseCoils();

  uint8_t pins_[4];
  uint16_t stepsPerRevolution_;
  float speedRpm_;
  uint32_t stepIntervalUs_;
  uint32_t lastStepUs_;

  MotionMode motionMode_;
  uint16_t stepsRemaining_;
  int8_t direction_;
  uint8_t sequenceIndex_;
  uint32_t motionId_;
  float baseRpm_;  // speed restored after a sweep that used an rpm override

  uint16_t sweepSteps_;
  uint16_t sweepLegsRemaining_;
  uint32_t sweepPauseMs_;
  uint32_t sweepPauseStartMs_;
};
