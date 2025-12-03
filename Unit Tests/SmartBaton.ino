/*
 * Project: Conductors Wand (Smart Baton) - BPM & Pattern Recognition
 * Component: 6-Axis IMU Data Acquisition + Real-Time Beat Detection
 * Platform: ESP32 with BMX055 via SPI
 * * Description:
 * This sketch reads raw data from the Accelerometer and Gyroscope.
 * It detects the "Ictus" (beat) based on acceleration magnitude peaks
 * and calculates BPM in real-time. It attempts to identify the "Downbeat" (Beat 1).
 */

#include <SPI.h>

// --- Wiring Configuration ---
#define SPI_CLK   18
#define SPI_MOSI  23

// MISO (Master In Slave Out) Split Configuration
#define MISO_ACCEL 19  // Dedicated MISO line for Accelerometer
#define MISO_GYRO  4   // Shared MISO line (Gyro + Mag)

// Chip Select (CS) Pin Definitions
#define CS_ACCEL  5
#define CS_GYRO   17
#define CS_MAG    14   // Used only to keep the Magnetometer disabled

// Sensor Register Addresses
#define ACCEL_DATA_START_REG 0x02 
#define GYRO_DATA_START_REG  0x02

// --- Conversion Constants ---
const float ACCEL_SCALE = 9.81 / 1024.0; 
const float GYRO_SCALE = 1.0 / 16.4;

// ==========================================
// FILTER CONFIGURATION
// ==========================================
// Stronger Filter: Lower alpha (0.15) smooths out hand jitters better than 0.2
const float alpha = 0.15;

// Filter Variables
float f_ax = 0, f_ay = 0, f_az = 0;
float f_gx = 0, f_gy = 0, f_gz = 0;

// ==========================================
// BEAT DETECTION VARIABLES
// ==========================================
// Thresholds
float beat_threshold = 15.0; // Initial threshold (m/s^2). Will auto-adjust.
float min_threshold = 12.5;  // Floor for threshold (slightly > 1g or 9.8m/s^2)
float decay_rate = 0.99;     // How fast threshold lowers looking for next beat

// Timing
unsigned long last_beat_time = 0;
unsigned long last_loop_time = 0;
const int MIN_BEAT_INTERVAL = 250; // ms (Max 240 BPM)
const int MAX_BEAT_INTERVAL = 2000; // ms (Min 30 BPM)

// BPM Calculation
float instant_bpm = 0;
float smoothed_bpm = 60;
float bpm_alpha = 0.2; // Smoothing factor for BPM (0.0 - 1.0). Lowered slightly to make the BPM number change slower/smoother

// Pattern Recognition (Downbeat Detection)
int beat_counter = 1;      // Assumed start at beat 1
float last_beat_mag = 0;   // Magnitude of the previous beat
float rolling_avg_mag = 0; // Average strength of beats

// ==========================================
// FUNCTION PROTOTYPES
// ==========================================
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void detectBeat(float ax, float ay, float az);
void analyzePattern(float magnitude);

void setup() {
  Serial.begin(115200);
  while(!Serial); 
  
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  
  digitalWrite(CS_MAG, HIGH); 
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100); 
  Serial.println("Smart Baton Ready. Waiting for motion...");
}

void loop() {
  // Maintain a fixed sample rate (approx 50Hz = 20ms) using micros()
  // This is better than delay() for real-time systems
  if (micros() - last_loop_time < 20000) {
    return;
  }
  last_loop_time = micros();

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  // --- 1. Read Raw Data ---
  SPI.end(); 
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  readSensor(CS_ACCEL, ACCEL_DATA_START_REG, &raw_ax, &raw_ay, &raw_az, true);
  
  SPI.end();
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  readSensor(CS_GYRO, GYRO_DATA_START_REG, &raw_gx, &raw_gy, &raw_gz, false);

  // --- 2. Convert to Physical Units ---
  float ax_phys = raw_ax * ACCEL_SCALE;
  float ay_phys = raw_ay * ACCEL_SCALE;
  float az_phys = raw_az * ACCEL_SCALE;

  // --- 3. Low Pass Filter ---
  f_ax = (alpha * ax_phys) + ((1.0 - alpha) * f_ax);
  f_ay = (alpha * ay_phys) + ((1.0 - alpha) * f_ay);
  f_az = (alpha * az_phys) + ((1.0 - alpha) * f_az);

  // --- 4. BEAT DETECTION ALGORITHM ---
  detectBeat(f_ax, f_ay, f_az);

  // --- 5. Output Data ---
  // Only print when a beat happens or periodically, otherwise Serial plotter gets messy
  // For debugging, we print the Magnitude vs Threshold
  float magnitude = sqrt(f_ax*f_ax + f_ay*f_ay + f_az*f_az);
  
  // Plotter format: "Mag:Value,Thresh:Value,BeatMark:Value"
  // Serial.print("Mag:"); Serial.print(magnitude);
  // Serial.print(",Thresh:"); Serial.print(beat_threshold);
  // Serial.println();
}

// ==========================================
// CORE ALGORITHM: DETECT BEAT & BPM
// ==========================================
void detectBeat(float ax, float ay, float az) {
  // Calculate total magnitude of acceleration vector
  // The 'click' of the baton creates a sharp spike in magnitude
  float magnitude = sqrt(ax*ax + ay*ay + az*az);

  // 1. Threshold Check
  if (magnitude > beat_threshold) {
    unsigned long now = millis();
    unsigned long interval = now - last_beat_time;

    // 2. Debounce (Min Interval)
    if (interval > MIN_BEAT_INTERVAL) {
      
      // --- VALID BEAT DETECTED ---
      last_beat_time = now;

      // 3. Calculate BPM
      if (interval < MAX_BEAT_INTERVAL) {
        // instant_bpm = 60000.0 / interval;                                              //OLD - Remove if not necessary
                                                                                          //OLD - Remove if not necessary
        // // Smooth the BPM to avoid jitter                                              //OLD - Remove if not necessary
        // smoothed_bpm = (bpm_alpha * instant_bpm) + ((1.0 - bpm_alpha) * smoothed_bpm); //OLD - Remove if not necessary
        // Calculate raw BPM                                                              //OLD - Remove if not necessary
        float raw_bpm_float = 60000.0 / interval;
        
        // Smooth it using float math first for accuracy
        float smooth_float = (bpm_alpha * raw_bpm_float) + ((1.0 - bpm_alpha) * smoothed_bpm);
        
        // Round to nearest whole number for display
        smoothed_bpm = (int)round(smooth_float);
        instant_bpm = (int)round(raw_bpm_float);
        
        Serial.print(">>> BEAT! BPM: "); 
        Serial.print(smoothed_bpm);
        analyzePattern(magnitude);  // Try to guess 1/2/3/4
        Serial.println();

        // Print a clean, comma-separated line specifically for Python
        // Format: DATA, BPM, BeatCounter
        Serial.print("DATA,");
        Serial.print(smoothed_bpm);
        Serial.print(",");
        Serial.println(beat_counter);
      }

      // 4. Auto-Adjust Threshold (Rise)
      // Set threshold to 70% of this beat's peak to avoid double-triggering
      beat_threshold = magnitude * 0.7; 
    }
  }

  // 5. Threshold Decay
  // Slowly lower threshold to catch softer beats
  beat_threshold *= decay_rate;
  if (beat_threshold < min_threshold) {
    beat_threshold = min_threshold;
  }
}

// ==========================================
// ALGORITHM: PATTERN GUESSING
// ==========================================
void analyzePattern(float magnitude) {
  // Logic: The "Downbeat" (Beat 1) is usually the strongest (physically)
  // If this beat is significantly stronger (>20%) than the average, assume it's Beat 1.
  
  if (rolling_avg_mag == 0) rolling_avg_mag = magnitude; // Init

  if (magnitude > (rolling_avg_mag * 1.2)) {
    beat_counter = 1; // Reset to 1
    Serial.print(" [DOWNBEAT detected]");
  } else {
    beat_counter++;
    if (beat_counter > 4) beat_counter = 1; // Default wrap-around (safe fallback)
  }

  Serial.print(" Count: ");
  Serial.print(beat_counter);
  
  // Update rolling average
  rolling_avg_mag = (0.1 * magnitude) + (0.9 * rolling_avg_mag);
}

// ==========================================
// ALGORITHM: PATTERN GUESSING (Z-AXIS DIP) - OLD, REMOVE
// ==========================================
// void analyzePattern(float magnitude, float z_accel) {
//   // z_accel is the filtered Z-axis acceleration (f_az)
  
//   // LOGIC: 
//   // In most conducting patterns, Beat 1 is a vertical downward strike.
//   // This means the Z-axis acceleration (relative to the baton) usually sees 
//   // the largest negative spike (or positive, depending on sensor mount) 
//   // because you are stopping the baton against gravity.
  
//   // We compare the Z-component of this beat to the "Average Z-component" of recent beats.
//   // If this beat has a MUCH deeper Z-strike than average, it's likely Beat 1.

//   static float rolling_avg_z = 0;
  
//   if (rolling_avg_z == 0) rolling_avg_z = abs(z_accel);

//   // Check if this beat's Z-impact is 15% stronger than the average
//   // We use abs() because depending on wiring, 'Down' might be -Z or +Z
//   float z_strength = abs(z_accel);
  
//   // Heuristic: Beat 1 is usually the "Vertical" beat.
//   // Beat 2 and 3 are usually "Horizontal" sweeps (so Z-accel is lower).
  
//   bool isDownbeat = false;
  
//   // 1. Is this beat stronger overall? (Magnitude check)
//   // 2. Is this beat mostly Vertical? (Z-axis dominance check)
//   // Check if Z-axis contributes to more than 70% of the total Magnitude
//   float z_contribution = z_strength / magnitude;
  
//   if (z_contribution > 0.70 && magnitude > 14.0) {
//      isDownbeat = true;
//   }

//   if (isDownbeat) {
//     beat_counter = 1; 
//     Serial.print(" [DOWNBEAT]");
//   } else {
//     beat_counter++;
//     if (beat_counter > 4) beat_counter = 1; 
//   }

//   Serial.print(" Count: ");
//   Serial.print(beat_counter);
  
//   // Update rolling average
//   rolling_avg_z = (0.2 * z_strength) + (0.8 * rolling_avg_z);
// }

// ==========================================
// SPI HELPER
// ==========================================
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel) {
  byte data[6]; 
  
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  digitalWrite(csPin, LOW); 
  SPI.transfer(startReg | 0x80); 
  for (int i = 0; i < 6; i++) {
    data[i] = SPI.transfer(0x00);
  }
  digitalWrite(csPin, HIGH); 
  SPI.endTransaction();

  if (isAccel) {
      *x = (int16_t)((data[1] << 8) | data[0]) >> 4; 
      *y = (int16_t)((data[3] << 8) | data[2]) >> 4;
      *z = (int16_t)((data[5] << 8) | data[4]) >> 4;
  } else {
      *x = (int16_t)((data[1] << 8) | data[0]);
      *y = (int16_t)((data[3] << 8) | data[2]);
      *z = (int16_t)((data[5] << 8) | data[4]);
  }
}