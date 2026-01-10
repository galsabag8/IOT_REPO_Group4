#include "WeightDetectAlgo.h"

// --- Global Variables Specific to this Algorithm ---
enum Direction {DOWN = -1, UP = 1};
int z_direction = DOWN;      // -1 = Down, 1 = Up, 0 = Static

float last_valid_beat_z = -0.5f; 
float last_valid_beat_x = -0.5f; 

float local_min_z = 100.0f;  
float local_min_x = 0.0f;    
float local_max_z = -100.0f; // Track actual peak during Up phase

float apex_x = 0.0f;           // The calculated extrema point (Red point in diagram)
float x_at_peak_z = -100.0f;    // Temporary holder for X at the very top of the arc

AccelChangeTracker accel_tracker;
VelocityTracker velocity_tracker;
PositionTracker position_tracker;

// --- Acceleration Change Tracker Implementation ---
void AccelChangeTracker::update(float current_magnitude) {
    // 1. Add to circular buffer
    history[index] = current_magnitude;
    index = (index + 1) % ACCEL_HISTORY_SIZE;
    
    // 2. Calculate average baseline
    float sum = 0;
    for(int i = 0; i < ACCEL_HISTORY_SIZE; i++) {
        sum += history[i];
    }
    avg_baseline = sum / ACCEL_HISTORY_SIZE;
    
    // 3. Calculate change from baseline
    float raw_change = fabs(current_magnitude - avg_baseline);
    
    // 4. Apply EMA smoothing
    smoothed_change = (ACCEL_CHANGE_ALPHA * raw_change) + 
                      ((1.0f - ACCEL_CHANGE_ALPHA) * smoothed_change);
}

// --- Velocity Tracker Implementation ---
void VelocityTracker::update(float current_z) {
    // Calculate instantaneous velocity
    float instant_velocity = current_z - prev_z;
    
    // Apply EMA smoothing (this "remembers" past velocities implicitly)
    smoothed_velocity = (VELOCITY_ALPHA * instant_velocity) + 
                        ((1.0f - VELOCITY_ALPHA) * smoothed_velocity);
    
    // Store for next iteration
    prev_z = current_z;
}


// Add new tracking variable
float peak_jerk_during_descent = 0.0f;
int upward_sample_count = 0;

ValleyInfo checkForValley(float z, float x, float gyro_magnitude, float current_jerk) {
  // Update trackers
  ValleyInfo result = {false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, DIR_UNKNOWN};
  velocity_tracker.update(z);
  
  // Get velocity from tracker
  float velocity_z = velocity_tracker.getVelocityZ();
  if (z_direction == DOWN)
    {
      position_tracker.updateDuringDescent(x);

      if (z < local_min_z) 
      {
        local_min_z = z;
        local_min_x = x;
      }

      if (current_jerk > peak_jerk_during_descent) {
          peak_jerk_during_descent = current_jerk;
      }
      
      if (velocity_z > MIN_VELOCITY_FOR_VALLEY) {
        upward_sample_count++;
      } else {
          upward_sample_count = 0;  // Reset if we dip back down
      }
      
      bool trend_reversed = (upward_sample_count >= SAMPLES_TO_CONFIRM_REVERSAL);
      if (trend_reversed)
      {
        upward_sample_count = 0;  // Reset for next cycle
        z_direction = UP; 
        local_max_z = -100.0f;
        // End position tracking and capture results
        position_tracker.endDescent(x);
        
        result.detected = true;
        result.peak_jerk_during_descent = peak_jerk_during_descent;
        result.valley_z = local_min_z;
        result.valley_x = local_min_x;
        result.velocity_at_valley = velocity_z;
        result.net_x_movement = position_tracker.getNetXMovement();
        result.was_vertical = position_tracker.wasVertical(VERTICAL_MOTION_THRESHOLD);
        result.direction = position_tracker.getDominantDirection(VERTICAL_MOTION_THRESHOLD);
        
        // Reset for next descent
        peak_jerk_during_descent = 0.0f;
        return result;
      }
    }
    else if (z_direction == UP) 
    {
      if (z > local_max_z) 
      {
        local_max_z = z;
        x_at_peak_z = x;
      }
      if (velocity_z < -MIN_VELOCITY_FOR_VALLEY) {
        upward_sample_count++;
      } else {
          upward_sample_count = 0;  // Reset if we dip back down
      }
      
      bool trend_reversed = (upward_sample_count >= SAMPLES_TO_CONFIRM_REVERSAL);
      if (trend_reversed)
      {
        upward_sample_count = 0;  // Reset for next cycle
        z_direction = DOWN;
        apex_x = x_at_peak_z;
        local_min_z = 100.0f; // Reset valley tracker for the down phase
        peak_jerk_during_descent = 0.0f; // Reset jerk tracker for new descent
        // Start tracking position for the new descent
        position_tracker.startDescent(x);
      }
    }
    return result; 
}


// --- WEIGHT 2 ---
bool checkBeat1LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 1 in 2/4: Expect rightward motion
    if (valley.direction == DIR_RIGHT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 1 (W2) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}
bool checkBeat2LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 2 in 2/4: Expect leftward motion
    if (valley.direction == DIR_LEFT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 2 (W2) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}

// --- WEIGHT 3 ---
bool checkBeat1LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 1 in 3/4: Down-left motion (accept DOWN or LEFT)
    if (valley.direction == DIR_LEFT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 1 (W3) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}

bool checkBeat2LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 2 in 3/4: Rightward motion
    if (valley.direction == DIR_RIGHT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 2 (W3) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}

bool checkBeat3LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 3 in 3/4: Upward-left motion (accept LEFT or any - it's the recovery)
    // This is more permissive since it's the upbeat
    if (valley.direction == DIR_LEFT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 3 (W3) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}

// --- WEIGHT 4 ---
bool checkBeat1LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 1 in 4/4: Pure downbeat (vertical motion)
    if (valley.direction == DIR_DOWN) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 1 (W4) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}

bool checkBeat2LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 2 in 4/4: Leftward motion
    if (valley.direction == DIR_LEFT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 2 (W4) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}
bool checkBeat3LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 3 in 4/4: Rightward motion
    if (valley.direction == DIR_RIGHT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 3 (W4) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}

bool checkBeat4LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley) {
    // Beat 4 in 4/4: Upbeat/recovery - accept LEFT or DOWN
    if (valley.direction == DIR_LEFT) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
    }
    
    if (DEBUG_MODE) {
        Serial.print("LOG: BEAT 4 (W4) dir:"); Serial.print(valley.direction); 
        Serial.print(" net_x:"); Serial.print(valley.net_x_movement, 4);
        Serial.print(" vertical:"); Serial.println(valley.was_vertical);
    }
    return false;
}