#include "pump.h"
#include "pindef.h"
#include <PSM.h>
#include "utils.h"

PSM pump(zcPin, dimmerPin, PUMP_RANGE, ZC_MODE, 2, 4);
float flowPerClickAtZeroBar = 0.27f;
short maxPumpClicksPerSecond = 50;

//https://www.desmos.com/calculator/uhgfwn5z9f  - blue curve
const std::array<float, 5> pressureInefficiencyCoefficient {{
  0.055f,
  0.016f,
  0.0033f,
  0.00061f,
  0.000026f
}};
// Initialising some pump specific specs, mainly:
// - max pump clicks(dependant on region power grid spec)
// - pump clicks at 0 pressure in the system
void pumpInit(int powerLineFrequency, float pumpFlowAtZero) {
  maxPumpClicksPerSecond = powerLineFrequency;
  flowPerClickAtZeroBar = pumpFlowAtZero;
}

// Function that returns the percentage of clicks the pump makes in it's current phase
float getPumpPct(float targetPressure, float flowRestriction, SensorState &currentState) {
  if (targetPressure == 0.f) {
      return 0.f;
  }

  float diff = targetPressure - currentState.smoothedPressure;
  float maxPumpPct = flowRestriction <= 0.f ? 1.f : getClicksPerSecondForFlow(flowRestriction, currentState.smoothedPressure) / (float) maxPumpClicksPerSecond;
  float pumpPctToMaintainFlow = getClicksPerSecondForFlow(currentState.smoothedPumpFlow, currentState.smoothedPressure) / (float) maxPumpClicksPerSecond;

  if (diff > 2.f) {
    return fminf(maxPumpPct, 0.25f + 0.2f * diff);
  }

  if (diff > 0.f) {
    return fminf(maxPumpPct, pumpPctToMaintainFlow * 0.95f + 0.1f + 0.2f * diff);
  }

  if (currentState.isPressureFalling) {
    return fminf(maxPumpPct, pumpPctToMaintainFlow * 0.2f);
  }

  return 0;
}

// Sets the pump output based on a couple input params:
// - live system pressure
// - expected target
// - flow
// - pressure direction
void setPumpPressure(float targetPressure, float flowRestriction, SensorState &currentState) {
  float pumpPct = getPumpPct(targetPressure, flowRestriction, currentState);
  setPumpToRawValue(pumpPct * PUMP_RANGE);
}

void setPumpOff(void) {
  pump.set(0);
}

void setPumpFullOn(void) {
  pump.set(PUMP_RANGE);
}

void setPumpToRawValue(uint8_t val) {
  pump.set(val);
}

long getAndResetClickCounter(void) {
  long counter = pump.getCounter();
  pump.resetCounter();
  return counter;
}

int getCPS(void) {
  return pump.cps();
}

// Models the flow per click
// Follows a compromise between the schematic and recorded findings
float getPumpFlowPerClick(float pressure) {
  float fpc = (flowPerClickAtZeroBar - pressureInefficiencyCoefficient[0]) - (pressureInefficiencyCoefficient[1] + (pressureInefficiencyCoefficient[2] - (pressureInefficiencyCoefficient[3] - pressureInefficiencyCoefficient[4] * pressure) * pressure) * pressure) * pressure;

  return 60.f * fmaxf(fpc, 0.f) / (float)maxPumpClicksPerSecond;
}

// Follows the schematic from http://ulka-ceme.co.uk/E_Models.html modified to per-click
float getPumpFlow(float cps, float pressure) {
  return cps * getPumpFlowPerClick(pressure);
}

// Currently there is no compensation for pressure measured at the puck, resulting in incorrect estimates
float getClicksPerSecondForFlow(float flow, float pressure) {
  float flowPerClick = getPumpFlowPerClick(pressure);
  float cps = flow / flowPerClick;
  return fminf(cps, maxPumpClicksPerSecond);
}

// Calculates pump percentage for the requested flow and updates the pump raw value
void setPumpFlow(float targetFlow, float pressureRestriction, SensorState &currentState) {
  // If a pressure restriction exists then the we go into pressure profile with a flowRestriction
  // which is equivalent but will achieve smoother pressure management
  if (pressureRestriction > 0.f && currentState.smoothedPressure > pressureRestriction * 0.5f) {
    setPumpPressure(pressureRestriction, targetFlow, currentState);
  }
  else {
    float pumpPct = getClicksPerSecondForFlow(targetFlow, currentState.smoothedPressure) / (float)maxPumpClicksPerSecond;
    setPumpToRawValue(pumpPct * PUMP_RANGE);
  }
}
