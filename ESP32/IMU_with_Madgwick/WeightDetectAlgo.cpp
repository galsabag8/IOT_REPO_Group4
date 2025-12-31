#include "WeightDetectAlgo.h"

// --- Global Variables Specific to this Algorithm ---
float prev_z = 0.0f;       
int z_direction = -1;      // -1 = Down, 1 = Up, 0 = Static

float last_valid_beat_z = -0.5f; 
float last_valid_beat_x = -0.5f; 

const float MAX_HEIGHT_DIFF = 0.03f; 

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
      // bool gyro_flick = (gyro_magnitude > GYRO_CONF_THRESHOLD);
      // if (trend_reversed || (gyro_flick && acc_magnitude > (DEFAULT_BEAT_THRESHOLD * 0.8))) 
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
      bool gyro_is_low = (gyro_magnitude > GYRO_CONF_THRESHOLD * 1.5);

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
bool checkBeat1LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, float gz) {
    // --- EXPECTING BEAT 1 (the DOWN BEAT) ---
  // Calculate horizontal distance from the "Red Point" (apex_x) to current "Green Point" (x)
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_left = (delta_x > 0.0f);
  if (magnitude > DEFAULT_BEAT_THRESHOLD && gz < -GYRO_CONF_THRESHOLD && came_from_left) 
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: BEAT 1 -> ");
    if(!came_from_left){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > DEFAULT_BEAT_THRESHOLD)){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    }
    if(!(gz < -GYRO_CONF_THRESHOLD)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(-GYRO_CONF_THRESHOLD, 4);
    }
  }
    return false;
}

bool checkBeat2LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, float gz) {
    // --- EXPECTING BEAT 2 (the UP BEAT) ---
  // Calculate horizontal distance from the "Red Point" (apex_x) to current "Green Point" (x)
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_right = (delta_x < 0.0f);
  
  if ((magnitude > (DEFAULT_BEAT_THRESHOLD * 0.7)) && gz > GYRO_CONF_THRESHOLD && came_from_right)
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: DEBUG BEAT 2 -> ");
    if(!came_from_right){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > (DEFAULT_BEAT_THRESHOLD * 0.7))){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 0.7, 4);
    }
    if(!(gz > GYRO_CONF_THRESHOLD)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD, 4);
    }
  }


  // // Error Recovery
  // bool is_strong = (magnitude > DEFAULT_BEAT_THRESHOLD * 1.25);
  // if (is_strong) {
  //   last_valid_beat_z = z;
  //   last_valid_beat_x = x;
  //   next_expected_beat = 2; // Will be incremented to 1 in main loop logic eventually if needed, or resets cycle
  //   return true;
  // }
  return false;
}

// --- WEIGHT 3 ---
bool checkBeat1LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, float gz) {
    // --- EXPECTING BEAT 1 (the DOWN BEAT) ---
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_right = (delta_x < 0.0f);
  
  if (magnitude > DEFAULT_BEAT_THRESHOLD && gz > GYRO_CONF_THRESHOLD && came_from_right)
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 1 -> ");
    // Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD, 4);
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: DEBUG BEAT 1 -> ");
    if(!came_from_right){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > (DEFAULT_BEAT_THRESHOLD * 0.7))){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    }
    if(!(gz > GYRO_CONF_THRESHOLD)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD, 4);
    }
  }
  return false;
}

bool checkBeat2LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, float gz) {
    // --- EXPECTING BEAT 2 (the LEFT BEAT) ---
  // Calculate horizontal distance from the "Red Point" (apex_x) to current "Green Point" (x)
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_left = (delta_x > 0.0f);
  if (magnitude > (DEFAULT_BEAT_THRESHOLD * 0.7) && gz < -GYRO_CONF_THRESHOLD * 1.5 && came_from_left) 
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 2 -> ");
    // Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 0.7, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(-GYRO_CONF_THRESHOLD * 1.5, 4);
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: BEAT 2 -> ");
    if(!came_from_left){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > DEFAULT_BEAT_THRESHOLD)){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    }
    if(!(gz < -GYRO_CONF_THRESHOLD)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(-GYRO_CONF_THRESHOLD, 4);
    }
  }
  return false;
}

bool checkBeat3LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, float gz) {
    // --- EXPECTING BEAT 3 (the RIGHT BEAT) ---
  // Calculate horizontal distance from the "Red Point" (apex_x) to current "Green Point" (x)
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_right = (delta_x < 0.0f);
  
  if ((magnitude > DEFAULT_BEAT_THRESHOLD) && gz > GYRO_CONF_THRESHOLD * 0.75 && came_from_right)
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 3 -> ");
    // Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 0.75, 4);
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: DEBUG BEAT 3 -> ");
    if(!came_from_right){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > (DEFAULT_BEAT_THRESHOLD * 0.7))){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 0.7, 4);
    }
    if(!(gz > GYRO_CONF_THRESHOLD * 0.75)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 0.75, 4);
    }
  }
  return false;
}

// --- WEIGHT 4 ---
bool checkBeat1LogicWithWeight4(float magnitude, float ax, float z, float x, int &next_expected_beat, float gz) {
  // --- EXPECTING BEAT 1 (the DOWN BEAT) ---
  float gz_abs = fabs(gz);
  if (magnitude > DEFAULT_BEAT_THRESHOLD * 1.5 && gz_abs < GYRO_CONF_THRESHOLD * 0.75) 
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 1 -> ");
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz_abs, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 0.75, 4);
    return true;
  }
  else if(magnitude > DEFAULT_BEAT_THRESHOLD * 2.0 && gz_abs < GYRO_CONF_THRESHOLD * 2.5)
  {
    // Special case: Very strong beat with low gyro
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 1 (STRONG) -> ");
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 2.0, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz_abs, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 2.5, 4);
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: DEBUG BEAT 1 -> ");
    if(!(magnitude > (DEFAULT_BEAT_THRESHOLD * 1.5))){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 1.5, 4);
    }
    if(!(gz_abs < GYRO_CONF_THRESHOLD * 0.75)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz_abs, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 0.75, 4);
    }
  }
    return false;
}

bool checkBeat2LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, float gz) {
  // --- EXPECTING BEAT 2 (the LEFT BEAT) ---
  // Geometric Rule: Must be to the LEFT of Beat 1
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_right = (delta_x < 0.0f);
  
  if (magnitude > DEFAULT_BEAT_THRESHOLD * 0.8 && gz > GYRO_CONF_THRESHOLD * 1.25 && came_from_right)
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 2 -> ");
    // Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 0.8, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 1.25, 4);
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: DEBUG BEAT 2 -> ");
    if(!came_from_right){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > (DEFAULT_BEAT_THRESHOLD * 0.8))){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 0.8, 4);
    }
    if(!(gz > GYRO_CONF_THRESHOLD)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD, 4);
    }
  }
  // // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  // bool is_strong = (magnitude > DEFAULT_BEAT_THRESHOLD * 1.25);
  // if (is_strong) {
  //       last_valid_beat_z = z; 
  //       last_valid_beat_x = x; 
  //       next_expected_beat = 4;   // expecting beat num 1 next time (detectBeat will fix it to 1)
  //       return true;
  // }
  return false;
}

bool checkBeat3LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, float gz) {
  // --- EXPECTING BEAT 3 (the RIGHT BEAT) ---
  // Geometric Rule: Must be to the RIGHT of Beat 2
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_left = (delta_x > 0.0f);
  if (magnitude > (DEFAULT_BEAT_THRESHOLD * 0.8) && gz < -GYRO_CONF_THRESHOLD * 1.5 && came_from_left) 
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 3 -> ");
    // Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 0.8, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(-GYRO_CONF_THRESHOLD * 1.5, 4);
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: BEAT 3 -> ");
    if(!came_from_left){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > DEFAULT_BEAT_THRESHOLD)){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    }
    if(!(gz < -GYRO_CONF_THRESHOLD)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(-GYRO_CONF_THRESHOLD, 4);
    }
  }
  // // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  // bool is_strong = (magnitude > DEFAULT_BEAT_THRESHOLD * 1.25);
  // if (is_strong) {
  //       last_valid_beat_z = z; 
  //       last_valid_beat_x = x; 
  //       next_expected_beat = 4;  // expecting beat num 1 next time (detectBeat will fix it to 1)
  //       return true;
  // }
  return false;
}

bool checkBeat4LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, float gz) {
  // Expecting BEAT 4 (UP)
  // Weak upward motion
  float delta_x = x - apex_x; 

  // If delta_x is POSITIVE: Current X > Apex X. We came from the LEFT (e.g. Apex was -0.5, X is 0.0)
  // If delta_x is NEGATIVE: Current X < Apex X. We came from the RIGHT (e.g. Apex was 0.5, X is 0.0)
  
  bool came_from_right = (delta_x < 0.0f);
  
  if ((magnitude > DEFAULT_BEAT_THRESHOLD) && gz > 0 && came_from_right)
  {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    // Serial.println("LOG: DEBUG BEAT 4 -> ");
    // Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    // Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD, 4);
    // Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 0.75, 4);
    return true;
  }
  else if(DEBUG_MODE == true)
  {
    // Debugging prints to understand rejection (Uncomment if needed)
    Serial.println("LOG: DEBUG BEAT 4 -> ");
    if(!came_from_right){
      Serial.print("LOG: delta_x: "); Serial.println(delta_x, 4); 
    }
    if(!(magnitude > (DEFAULT_BEAT_THRESHOLD * 0.7))){
      Serial.print("LOG: magnitude: "); Serial.print(magnitude, 4); Serial.print("thresh: "); Serial.println(DEFAULT_BEAT_THRESHOLD * 0.7, 4);
    }
    if(!(gz > GYRO_CONF_THRESHOLD * 0.75)){
      Serial.print("LOG: GYRO_Z: "); Serial.print(gz, 4); Serial.print("thresh: "); Serial.println(GYRO_CONF_THRESHOLD * 0.75, 4);
    }
  }
  
  // --- ERROR RECOVERY (maybe its a 1 BEAT again)
  if ((magnitude > DEFAULT_BEAT_THRESHOLD) && gz > -GYRO_CONF_THRESHOLD && came_from_right)
  {  
    // update last_valid to the last bit
    last_valid_beat_z = z; 
    last_valid_beat_x = x; 
    next_expected_beat = 4; // expecting beat num 1 next time (detectBeat will fix it to 1)
    return true;
  }  
  next_expected_beat = 4; // expecting beat num 1 next time (detectBeat will fix it to 1)
  return false;
}