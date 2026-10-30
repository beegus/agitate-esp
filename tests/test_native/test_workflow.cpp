#include <unity.h>

#include "Arduino.h"
#include "StepperAgitator.h"
#include "Workflow.h"

namespace native_test {
uint32_t nowMicros = 0;
uint32_t digitalWriteCount = 0;
}  // namespace native_test

void setUp() {
  native_test::nowMicros = 0;
  native_test::digitalWriteCount = 0;
}

void tearDown() {}

void testWorkflowStartsIdleAndStartsAtPresoak() {
  Workflow workflow;

  TEST_ASSERT_FALSE(workflow.isRunning());
  TEST_ASSERT_EQUAL_UINT8(4, workflow.totalSteps());
  workflow.start(1000);
  TEST_ASSERT_TRUE(workflow.isRunning());
  TEST_ASSERT_EQUAL_UINT8(0, workflow.currentStepIndex());
  TEST_ASSERT_EQUAL_STRING("Pre-soak", workflow.currentStep().name);
  TEST_ASSERT_EQUAL_STRING("Stop bath", workflow.stepAt(2).name);
  TEST_ASSERT_EQUAL_STRING("Fixer", workflow.stepAt(3).name);
}

void testStopBathAgitatesForTheFullStep() {
  Workflow workflow;
  StepperAgitator agitator(4, 5, 6, 7);
  workflow.start(1000);
  workflow.nextStep(1000);
  workflow.start(1000);
  workflow.nextStep(1000);
  workflow.start(1000);

  workflow.update(1000, agitator);
  TEST_ASSERT_TRUE(workflow.isAgitating());
  workflow.update(44999, agitator);
  TEST_ASSERT_TRUE(workflow.isAgitating());
  workflow.update(46000, agitator);
  TEST_ASSERT_TRUE(workflow.isWaitingForNext());
  TEST_ASSERT_FALSE(workflow.isAgitating());
}

void testDeveloperDurationHasMinimum() {
  Workflow workflow;
  workflow.configureDeveloper("film", "developer", "fixer", "dilution", 1);

  TEST_ASSERT_EQUAL_UINT32(30, workflow.stepAt(1).durationSec);
}

void testPauseAndResumePreserveRemainingTime() {
  Workflow workflow;
  workflow.configureDeveloper("film", "developer", "fixer", "dilution", 600);
  workflow.start(1000);
  workflow.nextStep(1000);

  TEST_ASSERT_TRUE(workflow.isWaitingForStart());
  workflow.start(2000);
  TEST_ASSERT_EQUAL_UINT32(600, workflow.remainingStepSec(2000));

  workflow.togglePause(12000);
  TEST_ASSERT_TRUE(workflow.isPaused());
  TEST_ASSERT_EQUAL_UINT32(590, workflow.remainingStepSec(42000));

  workflow.togglePause(42000);
  TEST_ASSERT_FALSE(workflow.isPaused());
  TEST_ASSERT_EQUAL_UINT32(590, workflow.remainingStepSec(42000));
  TEST_ASSERT_EQUAL_UINT32(580, workflow.remainingStepSec(52000));
}

void testInitialAgitationStartsTheStepper() {
  Workflow workflow;
  StepperAgitator agitator(4, 5, 6, 7);
  workflow.start(1000);

  workflow.update(1000, agitator);

  TEST_ASSERT_TRUE(workflow.isAgitating());
  TEST_ASSERT_TRUE(agitator.isBusy());
}

void testStepperClampsSpeedAndCanBeStopped() {
  StepperAgitator agitator(4, 5, 6, 7);
  agitator.setSpeedRpm(30.0f);

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, agitator.speedRpm());
  agitator.rotateDegrees(30.0f);
  TEST_ASSERT_TRUE(agitator.isBusy());

  agitator.stop();

  TEST_ASSERT_FALSE(agitator.isBusy());
  TEST_ASSERT_EQUAL_UINT32(4, native_test::digitalWriteCount);
}

void testStepperSweepCompletesInBoundedUpdates() {
  StepperAgitator agitator(4, 5, 6, 7);
  agitator.startTimedSweep(180.0f, 0, 15.0f, 1);
  agitator.update(0);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(16, native_test::digitalWriteCount);

  for (uint32_t update = 1; update <= 1100 && agitator.isBusy(); ++update) {
    native_test::nowMicros += 4000;
    agitator.update(update * 4);
  }

  TEST_ASSERT_FALSE(agitator.isBusy());
}

// Steps the agitator and workflow together the way loop() does.
void runUntil(Workflow &workflow, StepperAgitator &agitator, uint32_t &nowMs, uint32_t endMs) {
  for (; nowMs < endMs; ++nowMs) {
    native_test::nowMicros = nowMs * 1000;
    agitator.update(nowMs);
    workflow.update(nowMs, agitator);
  }
}

void testDeveloperAgitationFollowsScheduleFromStepStart() {
  Workflow workflow;
  StepperAgitator agitator(4, 5, 6, 7);
  workflow.start(0);
  workflow.nextStep(0);
  workflow.start(0);
  uint32_t nowMs = 0;

  // Initial 10 s: a 360-degree sweep takes ~10.3 s, so it must be cut off at 10 s.
  runUntil(workflow, agitator, nowMs, 9900);
  TEST_ASSERT_TRUE(agitator.isBusy());
  runUntil(workflow, agitator, nowMs, 10100);
  TEST_ASSERT_FALSE(agitator.isBusy());
  TEST_ASSERT_FALSE(workflow.isAgitating());

  // Quiet until the 1:00 mark, then 5 s of agitation.
  runUntil(workflow, agitator, nowMs, 59900);
  TEST_ASSERT_FALSE(agitator.isBusy());
  runUntil(workflow, agitator, nowMs, 60100);
  TEST_ASSERT_TRUE(agitator.isBusy());
  runUntil(workflow, agitator, nowMs, 64900);
  TEST_ASSERT_TRUE(agitator.isBusy());
  runUntil(workflow, agitator, nowMs, 65100);
  TEST_ASSERT_FALSE(agitator.isBusy());
}

void testScheduleCutoffLeavesManualMotionRunning() {
  Workflow workflow;
  StepperAgitator agitator(4, 5, 6, 7);
  workflow.start(0);
  uint32_t nowMs = 0;
  runUntil(workflow, agitator, nowMs, 15000);
  TEST_ASSERT_FALSE(agitator.isBusy());

  // Outside an agitation window, as the aggressive-agitation route does.
  agitator.startTimedSweep(30.0f, 100, 15.0f, 20);
  runUntil(workflow, agitator, nowMs, 16000);
  TEST_ASSERT_TRUE(agitator.isBusy());
}

void testRestartCurrentStepResetsTimer() {
  Workflow workflow;
  workflow.start(0);
  TEST_ASSERT_EQUAL_UINT32(40, workflow.remainingStepSec(20000));

  workflow.restartCurrentStep(20000);

  TEST_ASSERT_EQUAL_UINT32(60, workflow.remainingStepSec(20000));
  TEST_ASSERT_EQUAL_UINT8(0, workflow.currentStepIndex());
}

void testNextOnLastStepStopsWorkflow() {
  Workflow workflow;
  workflow.start(0);
  for (uint8_t i = 1; i < workflow.totalSteps(); ++i) {
    workflow.nextStep(0);
    workflow.start(0);
  }
  TEST_ASSERT_EQUAL_STRING("Fixer", workflow.currentStep().name);

  workflow.nextStep(0);

  TEST_ASSERT_FALSE(workflow.isRunning());
  TEST_ASSERT_EQUAL_UINT8(0, workflow.currentStepIndex());
}

void testElapsedTimeSurvivesMillisWraparound() {
  Workflow workflow;
  const uint32_t startMs = 0xFFFFFFFFUL - 5000;
  workflow.start(startMs);

  TEST_ASSERT_EQUAL_UINT32(10, workflow.elapsedStepSec(startMs + 10000));
  TEST_ASSERT_EQUAL_UINT32(50, workflow.remainingStepSec(startMs + 10000));
}

void testStaleTimestampDoesNotEndNewStep() {
  Workflow workflow;
  StepperAgitator agitator(4, 5, 6, 7);
  workflow.start(5000);

  // loop() read the clock just before a web handler started the run.
  workflow.update(4999, agitator);

  TEST_ASSERT_FALSE(workflow.isWaitingForNext());
  TEST_ASSERT_TRUE(workflow.isAgitating());
}

void testSweepRpmOverrideIsRestoredAfterStop() {
  StepperAgitator agitator(4, 5, 6, 7);
  agitator.setSpeedRpm(12.0f);
  agitator.startTimedSweep(30.0f, 0, 15.0f, 1);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, agitator.speedRpm());

  agitator.stop();

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, agitator.speedRpm());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testWorkflowStartsIdleAndStartsAtPresoak);
  RUN_TEST(testStopBathAgitatesForTheFullStep);
  RUN_TEST(testDeveloperDurationHasMinimum);
  RUN_TEST(testPauseAndResumePreserveRemainingTime);
  RUN_TEST(testInitialAgitationStartsTheStepper);
  RUN_TEST(testStepperClampsSpeedAndCanBeStopped);
  RUN_TEST(testStepperSweepCompletesInBoundedUpdates);
  RUN_TEST(testDeveloperAgitationFollowsScheduleFromStepStart);
  RUN_TEST(testScheduleCutoffLeavesManualMotionRunning);
  RUN_TEST(testRestartCurrentStepResetsTimer);
  RUN_TEST(testNextOnLastStepStopsWorkflow);
  RUN_TEST(testElapsedTimeSurvivesMillisWraparound);
  RUN_TEST(testStaleTimestampDoesNotEndNewStep);
  RUN_TEST(testSweepRpmOverrideIsRestoredAfterStop);
  return UNITY_END();
}
