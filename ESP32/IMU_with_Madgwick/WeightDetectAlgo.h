#ifndef WEIGHT_DETECT_ALGO_H
#define WEIGHT_DETECT_ALGO_H

#include <Arduino.h>
#include "config.h"

// --- Tuning Constants ---
extern float prev_z; 
extern float apex_x;

// --- Core Helper Functions ---
// Added gyro_magnitude to help confirm slow beats
bool checkForValley(float z, float x, float velocity_z, float magnitude, float gyro_magnitude);

// --- Beat Logic Functions ---
bool checkBeat1LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, float gz);
bool checkBeat2LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, float gz);

bool checkBeat1LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, float gz);
bool checkBeat2LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, float gz);
bool checkBeat3LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, float gz);

bool checkBeat1LogicWithWeight4(float magnitude, float ax, float z, float x, int &next_expected_beat, float gz);
bool checkBeat2LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, float gz);
bool checkBeat3LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, float gz);
bool checkBeat4LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, float gz);

#endif