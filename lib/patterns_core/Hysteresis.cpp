#include "Hysteresis.h"

#include <math.h>

bool Hysteresis::update(float temp_c, float setpoint_c) {
  if (isnan(temp_c) || temp_c < min_valid || temp_c > max_valid) {
    on = false;  // a bad sensor must not heat
  } else if (temp_c < setpoint_c - band) {
    on = true;
  } else if (temp_c >= setpoint_c) {
    on = false;
  }  // inside the band: keep the previous state
  return on;
}
