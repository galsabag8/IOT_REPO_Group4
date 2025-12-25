#include "WeightDetectAlgo.h"

// --- Global Variables Specific to this Algorithm ---
float prev_z = 0.0f;       
int z_direction = -1;      // -1 = Down, 1 = Up, 0 = Static

float last_valid_beat_z = -0.5f; 
float last_valid_beat_x = -0.5f; 

float beat_threshold = 8.0; 
const float RESTING_MAGNITUDE = 6.5f; 
const float MAX_HEIGHT_DIFF = 0.05f; 
const float MIN_VELOCITY_FOR_VALLEY = 0.015f;

float local_min_z = 100.0f;  
float local_min_x = 0.0f;    
float apex_x = 0.0f;

bool checkForValley(float z, float x, float velocity_z, float magnitude) {
    if (z_direction == -1) {
        if (z < local_min_z) {
            local_min_z = z;
            local_min_x = x;
        }
        if (velocity_z > MIN_VELOCITY_FOR_VALLEY) {
            z_direction = 1; 
            return true; 
        }
    }
    else if (z_direction == 1) {
        if (velocity_z < -MIN_VELOCITY_FOR_VALLEY) {
            z_direction = -1;
            apex_x = x;
            local_min_z = 100.0f; 
        }
    }
    return false; 
}

// --- WEIGHT 2 ---
bool checkBeat1LogicWithWeight2(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
    // --- EXPECTING BEAT 1 (the DOWN BEAT) ---
  if (magnitude > beat_threshold) {
        last_valid_beat_z = z;
        last_valid_beat_x = x;
        return true;
      }
    return false;
}

bool checkBeat2LogicWithWeight2(float magnitude, float z, float x, float velocity_z, int &next_expected_beat) {
    // --- EXPECTING BEAT 2 (the UP BEAT) ---
  bool is_higher = (z + MAX_HEIGHT_DIFF >= last_valid_beat_z);
  bool approached_from_right = (apex_x > x);
  
  if (is_higher && approached_from_right && (magnitude > (beat_threshold * 0.7))) {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    return true;
  }

  // Error Recovery
  bool is_strong = (magnitude > beat_threshold * 1.25);
  if (is_strong) {
    last_valid_beat_z = z;
    last_valid_beat_x = x;
    next_expected_beat = 2; // Will be incremented to 1 in main loop logic eventually if needed, or resets cycle
    return true;
  }
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