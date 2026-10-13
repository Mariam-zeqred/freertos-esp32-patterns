// On/off control with a dead band, the way the queen-bee incubator drives its heater:
// switch on below (setpoint - band), off at or above setpoint, and never on for an
// implausible reading. Pure function of its inputs plus one bit of state.
#pragma once

struct Hysteresis {
  Hysteresis(float band_c, float min_valid_c, float max_valid_c)
      : band(band_c), min_valid(min_valid_c), max_valid(max_valid_c) {}

  // Returns the new output. NaN or out-of-range temperature always yields off.
  bool update(float temp_c, float setpoint_c);

  float band;
  float min_valid;
  float max_valid;
  bool on = false;
};
