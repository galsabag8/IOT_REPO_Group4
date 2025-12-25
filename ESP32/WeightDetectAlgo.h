#ifndef WEIGHT_DETECT_ALGO_H
#define WEIGHT_DETECT_ALGO_H

#include <Arduino.h>

// --- Tuning Constants (Exposed if needed, generally kept internal) ---
extern float beat_threshold; 
extern const float RESTING_MAGNITUDE;
extern const float MAX_HEIGHT_DIFF;
extern float prev_z; // Needed in main to calc velocity

// --- Core Helper Functions ---
bool checkForValley(float z, float x, float velocity_z, float magnitude);

// --- Beat Logic Functions ---
// Note: Changed signature to include velocity_z (for logging) and next_expected_beat (for logic updates)
bool checkBeat1LogicWithWeight2(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);
bool checkBeat2LogicWithWeight2(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);

bool checkBeat1LogicWithWeight3(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);
bool checkBeat2LogicWithWeight3(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);
bool checkBeat3LogicWithWeight3(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);

bool checkBeat1LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);
bool checkBeat2LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);
bool checkBeat3LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);
bool checkBeat4LogicWithWeight4(float magnitude, float z, float x, float velocity_z, int &next_expected_beat);

#endif