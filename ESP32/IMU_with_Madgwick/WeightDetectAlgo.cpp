#include "WeightDetectAlgo.h"

// --- Global Variables Specific to this Algorithm ---
float prev_z = 0.0f;       
int z_direction = -1;      // -1 = Down, 1 = Up, 0 = Static

float last_valid_beat_z = -0.5f; 
float last_valid_beat_x = -0.5f; 

float beat_threshold = 4.8f; 
const float RESTING_MAGNITUDE = 4.5f; 
const float MAX_HEIGHT_DIFF = 0.03f; 
const float MIN_VELOCITY_FOR_VALLEY = 0.006f;
const float GYRO_CONFIRMATION_THRESHOLD = 0.35f; // Minimum rotation to confirm slow beat

float local_min_z = 100.0f;  
float local_min_x = 0.0f;    
float local_max_z = -100.0f; // Track actual peak during Up phase

// --- NEW TRACKING VARIABLES ---
float apex_x = 0.0f;           // The calculated extrema point (Red point in diagram)
float x_at_peak_z = -100.0f;    // Temporary holder for X at the very top of the arc

bool checkForValley(float z, float x, float velocity_z, float acc_magnitude, float gyro_magnitude) {
    if (z_direction == -1)
    {
      if (z < local_min_z) 
      {
        local_min_z = z;
        local_min_x = x;
      }
      // Logical improvement: Even if velocity is low, if there's a gyro flick, we consider it a valley
      bool trend_reversed = (velocity_z > MIN_VELOCITY_FOR_VALLEY);
      // bool gyro_flick = (gyro_magnitude > GYRO_CONFIRMATION_THRESHOLD);
      // if (trend_reversed || (gyro_flick && acc_magnitude > (beat_threshold * 0.8))) 
      if (trend_reversed)
      {
        z_direction = 1; 
        local_max_z = -100.0f;
        return true; 
      }
    }
    else if (z_direction == 1) 
    {
      if (z > local_max_z) 
      {
        local_max_z = z;
        x_at_peak_z = x;
      }

      bool steady_downward = (velocity_z < -MIN_VELOCITY_FOR_VALLEY);
      bool gyro_is_low = (gyro_magnitude > GYRO_CONFIRMATION_THRESHOLD * 1.5);

      if (steady_downward && gyro_is_low) 
      {
        z_direction = -1;
        apex_x = x_at_peak_z;
        local_min_z = 100.0f; // Reset valley tracker for the down phase
      }
    }
    return false; 
}

// --- WEIGHT 2 ---
bool checkBeat1LogicWithWeight2(float magnitude, float z, float x, float velocity_z, int &next_expected_beat, float gz) {
    // --- EXPECTING BEAT 1 (the DOWN BEAT) ---
  // Calculate horizontal distance from the "Red Point" (apex_x) to current "Green Point" (x)
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_left = (delta_x > 0.0f);
  if (magnitude > beat_threshold && gz < -GYRO_CONFIRMATION_THRESHOLD && came_from_left) 
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    return true;
  }
  else
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.print("DEBUG BEAT 1 -> ");
    if(!came_from_left){
      Serial.print("delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > beat_threshold)){
      Serial.print("magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(beat_threshold, 4);
    }
    if(!(gz < -GYRO_CONFIRMATION_THRESHOLD)){
      Serial.print("GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(-GYRO_CONFIRMATION_THRESHOLD, 4);
    }
  }
    return false;
}

bool checkBeat2LogicWithWeight2(float magnitude, float z, float x, float velocity_z, int &next_expected_beat, float gz) {
    // --- EXPECTING BEAT 2 (the UP BEAT) ---
  // Calculate horizontal distance from the "Red Point" (apex_x) to current "Green Point" (x)
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_right = (delta_x < 0.0f);
  
  if ((magnitude > (beat_threshold * 0.7)) && gz > GYRO_CONFIRMATION_THRESHOLD && came_from_right)
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    return true;
  }
  else
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.print("DEBUG BEAT 2 -> ");
    if(!came_from_right){
      Serial.print("delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > (beat_threshold * 0.7))){
      Serial.print("magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(beat_threshold * 0.7, 4);
    }
    if(!(gz > GYRO_CONFIRMATION_THRESHOLD)){
      Serial.print("GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONFIRMATION_THRESHOLD, 4);
    }
  }


  // // Error Recovery
  // bool is_strong = (magnitude > beat_threshold * 1.25);
  // if (is_strong) {
  //   last_valid_beat_z = z;
  //   last_valid_beat_x = x;
  //   next_expected_beat = 2; // Will be incremented to 1 in main loop logic eventually if needed, or resets cycle
  //   return true;
  // }
  return false;
}

// --- WEIGHT 3 ---
bool checkBeat1LogicWithWeight3(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
    // --- EXPECTING BEAT 1 (the DOWN BEAT) ---
  if (magnitude > beat_threshold) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
      }
    return false;
}

bool checkBeat2LogicWithWeight3(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
    // --- EXPECTING BEAT 2 (the LEFT BEAT) ---
  bool approached_from_left = (x > apex_x);
  bool is_higher = (z + MAX_HEIGHT_DIFF >= last_valid_beat_z);
  if (approached_from_left && is_higher && (magnitude > (beat_threshold * 0.7))) {
      last_valid_beat_z = z; 
      last_valid_beat_x = x; 
      return true;
  }
  // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  bool is_strong = (magnitude > beat_threshold * 1.25);
  if (is_strong) {
        last_valid_beat_z = z; 
        last_valid_beat_x = x; 
        next_expected_beat = 3;   // expecting beat num 1 next time (detectBeat will fix it to 1)
        return true;
  }
  return false;
}

bool checkBeat3LogicWithWeight3(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
    // --- EXPECTING BEAT 3 (the RIGHT BEAT) ---
  bool is_higher = (z + MAX_HEIGHT_DIFF >= last_valid_beat_z);
  bool approached_from_right = (apex_x > x);

  if (is_higher && approached_from_right && (magnitude > (beat_threshold * 0.7))) {
      last_valid_beat_z = z; 
      last_valid_beat_x = x; 
      return true;
  }
  // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  bool is_strong = (magnitude > beat_threshold * 1.25);
  if (is_strong) {
        last_valid_beat_z = z; 
        last_valid_beat_x = x; 
        next_expected_beat = 3;  // expecting beat num 1 next time (detectBeat will fix it to 1)
        return true;
  }
  return false;
}

// --- WEIGHT 4 ---
bool checkBeat1LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
  // --- EXPECTING BEAT 1 (the DOWN BEAT) ---
  if (magnitude > beat_threshold) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
      }
    return false;
}

bool checkBeat2LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
  // --- EXPECTING BEAT 2 (the LEFT BEAT) ---
  // Geometric Rule: Must be to the LEFT of Beat 1
  bool is_higher = (z + MAX_HEIGHT_DIFF >= last_valid_beat_z);
  bool approached_from_right = (apex_x > x);

  if (is_higher && approached_from_right && (magnitude > (beat_threshold * 0.7))) {
      last_valid_beat_z = z; 
      last_valid_beat_x = x; 
      return true;
  }
  // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  bool is_strong = (magnitude > beat_threshold * 1.25);
  if (is_strong) {
        last_valid_beat_z = z; 
        last_valid_beat_x = x; 
        next_expected_beat = 4;   // expecting beat num 1 next time (detectBeat will fix it to 1)
        return true;
  }
  return false;
}

bool checkBeat3LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
  // --- EXPECTING BEAT 3 (the RIGHT BEAT) ---
  // Geometric Rule: Must be to the RIGHT of Beat 2
  bool approached_from_left = (x > apex_x);
  bool is_higher = (z + MAX_HEIGHT_DIFF >= last_valid_beat_z);
  
  if (approached_from_left && is_higher && (magnitude > (beat_threshold * 0.7))) {
      last_valid_beat_z = z; 
      last_valid_beat_x = x; 
      return true;
  }
  // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  bool is_strong = (magnitude > beat_threshold * 1.25);
  if (is_strong) {
        last_valid_beat_z = z; 
        last_valid_beat_x = x; 
        next_expected_beat = 4;  // expecting beat num 1 next time (detectBeat will fix it to 1)
        return true;
  }
  return false;
}

bool checkBeat4LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
  // Expecting BEAT 4 (UP)
  // Weak upward motion
  bool is_higher = (z + MAX_HEIGHT_DIFF >= last_valid_beat_z);
  bool approached_from_right = (apex_x > x);

  if (is_higher && approached_from_right && (magnitude > (beat_threshold * 0.7))) {
      last_valid_beat_z = z; 
      last_valid_beat_x = x; 
      return true;
  }
  
  // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  bool is_strong = (magnitude > beat_threshold * 1.25);
  if (is_strong) {
    // Serial.println(">>> MISSED BEAT 4 -> RESETTING <<<");    
    // update last_valid to the last bit
    last_valid_beat_z = z; 
    last_valid_beat_x = x; 
    next_expected_beat = 4; // expecting beat num 1 next time (detectBeat will fix it to 1)
    return true;
  }
  return false;
}