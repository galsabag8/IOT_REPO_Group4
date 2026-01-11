#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =================================================================
//			HARDWARE & WIRING (IMU_with_Madgwick.ino)
// =================================================================
// These define which pins on your microcontroller connect to the IMU sensors.
#define SPI_CLK      18
#define SPI_MOSI     23
#define MISO_ACCEL   19  
#define MISO_GYRO    4   
#define CS_ACCEL     5
#define CS_GYRO      17
#define CS_MAG       14   
#define SOURCE_PIN   2
#define SENSING_PIN  15

// =================================================================
//					SYSTEM CONTROL & MODES
// =================================================================
// for logging in WeightDetectAlgo.cpp
const bool DEBUG_MODE = false; 

// =================================================================
//					BUTTON PARAMETERS
// =================================================================
// for debounce in .ino
#define DEBOUNCE_DELAY 50

// =================================================================
//				SENSOR CALIBRATION & FILTERING
// =================================================================
// ACCEL_SCALE: 1.95mg/LSB * 9.80665 (for +/- 4g range).
const float ACCEL_SCALE = 0.01912f; 
// GYRO_SCALE: 1/16.4 dps/LSB (for +/- 2000 dps range).
const float GYRO_SCALE  = 1.0f / 16.4f; 

// Madgwick filter gain. Higher = faster response but more noise. 
// Lower = smoother orientation but more lag.
const float MADGWICK_BETA = 0.03f; 

// Size of the moving average window for acceleration magnitude.
// Increasing this makes beat detection more stable but less responsive.
const int SMOOTH_WINDOW = 5; 

// =================================================================
//		  BEAT DETECTION THRESHOLDS (WeightDetectAlgo.cpp)
// =================================================================

/**
 * SAMPLES_TO_CONFIRM_REVERSAL: Number of consecutive samples to confirm a trend reversal.
 * Helps filter out noise-induced false reversals.
 */
const int SAMPLES_TO_CONFIRM_REVERSAL = 2;  


const float RESTING_MAGNITUDE = 1.5f; 

/**
 * ACCEL_HISTORY_SIZE: Number of past acceleration values to store for velocity calculation.
 */
const int ACCEL_HISTORY_SIZE = 5;  // Configurable window size

/**
 * VELOCITY_ALPHA: EMA alpha for velocity smoothing.
 * Higher = responsive to quick changes; Lower = filters out jitter
 */
const float VELOCITY_ALPHA = 0.3f;

/**
 * MIN_VELOCITY_FOR_VALLEY: Speed required to detect a change in direction.
 * If the wand is moving slower than this, the algorithm won't look for a "valley."
 */
const float MIN_VELOCITY_FOR_VALLEY = 0.0075f;

/**
 * ACCEL_CHANGE_SMOOTHING: EMA alpha for smoothing magnitude changes.
 * Higher = responsive to quick changes; Lower = filters out jitter
 */
const float ACCEL_CHANGE_ALPHA = 0.2f;

/**
 * RESTING_ACCEL_CHANGE_THRESHOLD: Maximum magnitude change to be considered "at rest"
 * Used to detect when baton is not moving (for filtering out static tilts)
 */
const float RESTING_ACCEL_CHANGE_THRESHOLD = 0.80f;

// Threshold for considering motion "vertical" (minimal rotation)
const float VERTICAL_MOTION_THRESHOLD = 0.15f;  // Tune this!

// Threshold for considering motion "vertical" (minimal rotation)
const float VERTICAL_NOISE_THRESHOLD = 0.01f;  // Tune this!


// =================================================================
//						BPM & TIMING LOGIC
// =================================================================
// Loop speed in microseconds (10000us = 100Hz).
const int LOOP_DELAY_US = 10000; 

// Shortest possible time between two beats to prevent double-triggering.
const int MIN_BEAT_INTERVAL = 250; 

// Longest time between beats before the sequence resets.
const int MAX_BEAT_INTERVAL = 2000; 

// Time in ms of inactivity before BPM is forced to 0.
const unsigned long BPM_TIMEOUT = 3000; 

// Alpha for the EMA filter (0.0 to 1.0). 
// Higher = BPM updates faster; Lower = BPM is more stable/averaged.
const float BPM_SMOOTHING_ALPHA = 0.2f; 

// Number of beats to include in the rolling average BPM.
const int NUM_BEATS_AVG = 4; 

// How often to print data to the Serial port (in ms).
const int PRINT_INTERVAL = 100;

#endif