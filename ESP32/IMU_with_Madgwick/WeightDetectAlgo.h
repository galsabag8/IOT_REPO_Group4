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
    float peak_jerk_in_phase;
    
    AccelChangeTracker() : index(0), avg_baseline(0.0f), smoothed_change(0.0f), peak_jerk_in_phase(0.0f) {
        for(int i = 0; i < ACCEL_HISTORY_SIZE; i++) {
            history[i] = 0.0f;
        }
    }
    void resetPeakJerk() { peak_jerk_in_phase = 0.0f; }
    void updatePeakJerk() { 
        if (smoothed_change > peak_jerk_in_phase) {
            peak_jerk_in_phase = smoothed_change;
        }
    }
    float getPeakJerk() const { return peak_jerk_in_phase; }
    
    void update(float current_magnitude);
    float getJerkMagnitude() const { return smoothed_change; }
    float getRawBaseline() const { return avg_baseline; }
};

// --- Velocity Tracker Struct ---
struct VelocityTracker {
    float prev_z;              // Just store the previous position
    float smoothed_velocity;   // EMA handles the "memory"
    
    VelocityTracker() : prev_z(0.0f), smoothed_velocity(0.0f) {}
    
    void update(float current_z);
    float getVelocityZ() const { return smoothed_velocity; }
};

enum BeatDirection {
    DIR_UNKNOWN = 0,
    DIR_DOWN,       // Minimal lateral movement, downward motion
    DIR_LEFT,       // Positive gz (counter-clockwise from above)
    DIR_RIGHT,      // Negative gz (clockwise from above)
    DIR_UP          // Weak upward recovery motion
};

// --- Position-Based Direction Tracker ---
// Tracks X position throughout descent to determine lateral movement
struct PositionTracker {
    float x_at_descent_start;   // X when we started going down
    float x_at_valley;          // X at the lowest point
    float min_x_during_descent; // Leftmost X during descent
    float max_x_during_descent; // Rightmost X during descent
    float total_x_travel;       // How much X changed during descent
    bool tracking_descent;
    
    PositionTracker() : x_at_descent_start(0.0f), x_at_valley(0.0f), 
                        min_x_during_descent(100.0f), max_x_during_descent(-100.0f),
                        total_x_travel(0.0f), tracking_descent(false) {}
    
    void startDescent(float x) {
        x_at_descent_start = x;
        min_x_during_descent = x;
        max_x_during_descent = x;
        tracking_descent = true;
    }

    void updateDuringDescent(float x) {
        if (!tracking_descent) return;
        if (x < min_x_during_descent) min_x_during_descent = x;
        if (x > max_x_during_descent) max_x_during_descent = x;
    }
    
    void endDescent(float x) {
        x_at_valley = x;
        total_x_travel = x_at_valley - x_at_descent_start;
        tracking_descent = false;
    }

    // Returns: positive = moved right during descent, negative = moved left
    float getNetXMovement() const { return total_x_travel; }
    
    // Returns the dominant direction of travel
    BeatDirection getDominantDirection(float threshold) const {
        if (fabs(total_x_travel) < threshold) {
            return DIR_DOWN;  // Mostly vertical
        }
        return (total_x_travel > 0) ? DIR_RIGHT : DIR_LEFT;
    }
    // Check if motion was mostly vertical
    bool wasVertical(float threshold) const {
        Serial.print("total_x_travel: "); Serial.println(total_x_travel);
        return fabs(total_x_travel) < threshold;
    }
    
    void reset() {
        x_at_descent_start = 0.0f;
        x_at_valley = 0.0f;
        min_x_during_descent = 100.0f;
        max_x_during_descent = -100.0f;
        total_x_travel = 0.0f;
        tracking_descent = false;
    }
};

struct ValleyInfo {
    bool detected;
    float peak_jerk_during_descent;
    float valley_z;
    float valley_x;
    float velocity_at_valley;
    float net_x_movement;       // How much X changed: positive=right, negative=left
    bool was_vertical;          // True if motion was mostly straight down
    BeatDirection direction;    // Pre-classified direction based on position
};


// --- Tuning Constants ---
extern float apex_x;
extern AccelChangeTracker accel_tracker;
extern VelocityTracker velocity_tracker;
extern PositionTracker position_tracker;

// --- Core Helper Functions ---

ValleyInfo checkForValley(float z, float x, float gyro_magnitude, float current_jerk);

// --- Beat Logic Functions ---
bool checkBeat1LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);
bool checkBeat2LogicWithWeight2(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);

bool checkBeat1LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);
bool checkBeat2LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);
bool checkBeat3LogicWithWeight3(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);

bool checkBeat1LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);
bool checkBeat2LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);
bool checkBeat3LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);
bool checkBeat4LogicWithWeight4(float magnitude, float z, float x, int &next_expected_beat, const ValleyInfo& valley);

#endif