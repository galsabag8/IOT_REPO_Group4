#ifndef WEIGHT_DETECT_ALGO_H
#define WEIGHT_DETECT_ALGO_H

#include <Arduino.h>
#include "config.h"


// --- Acceleration Change Tracker Struct ---
struct AccelChangeTracker {
    float history[ACCEL_HISTORY_SIZE];
    int index;
    float avg_baseline;
    float smoothed_change;
    
    AccelChangeTracker() : index(0), avg_baseline(0.0f), smoothed_change(0.0f) {
        for(int i = 0; i < ACCEL_HISTORY_SIZE; i++) {
            history[i] = 0.0f;
        }
    }
    
    void update(float current_magnitude);
    float getJerkMagnitude() const { return smoothed_change; }
    float getRawBaseline() const { return avg_baseline; }
};

// --- Velocity Tracker Struct ---
struct VelocityTracker {
    float z_history[VELOCITY_HISTORY_SIZE];
    int index;
    float avg_z;
    
    VelocityTracker() : index(0), avg_z(0.0f) {
        for(int i = 0; i < VELOCITY_HISTORY_SIZE; i++) {
            z_history[i] = 0.0f;
        }
    }
    
    void update(float current_z);
    float getVelocityZ() const;
};

// --- Tuning Constants ---
extern float apex_x;
extern AccelChangeTracker accel_tracker;
extern VelocityTracker velocity_tracker;

// --- Core Helper Functions ---
// Added gyro_magnitude to help confirm slow beats
bool checkForValley(float z, float x, float gyro_magnitude);

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