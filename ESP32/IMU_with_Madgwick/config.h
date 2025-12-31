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

// =================================================================
//					SYSTEM CONTROL & MODES
// =================================================================
// for logging in WeightDetectAlgo.cpp
const bool DEBUG_MODE = false; 

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
 * BEAT_THRESHOLD: The "force" required to register a beat. 
 * Increase if getting ghost beats; decrease if the wand feels "deaf."
 */
const float DEFAULT_BEAT_THRESHOLD = 4.8f; 

/**
 * RESTING_MAGNITUDE: Below this acceleration, the wand is considered "still."
 * Prevents noise from triggering beats when the hand isn't moving.
 */
const float RESTING_MAGNITUDE = 4.5f; 

/**
 * GYRO_CONFIRMATION_THRESHOLD: Minimum rotation (flick) to confirm a beat.
 * Higher values require a more aggressive "snap" of the wrist.
 */
const float GYRO_CONF_THRESHOLD = 0.25f; 

/**
 * MIN_VELOCITY_FOR_VALLEY: Speed required to detect a change in direction.
 * If the wand is moving slower than this, the algorithm won't look for a "valley."
 */
const float MIN_VELOCITY_FOR_VALLEY = 0.006f;

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