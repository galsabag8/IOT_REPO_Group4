/*
 * Project: Conductors Wand - Hybrid V1 (Vis + BPM)
 * Features: 
 * - High-speed Motion Data (for Visualization)
 * - Real-time BPM Calculation (for Music Control)
 * - Dual Filtering (Responsive for Vis, Smooth for Beats)
 */

#include <SPI.h>
#include "MadgwickAlgo.h"
#include "WeightDetectAlgo.h"

// --- Wiring ---
#define SPI_CLK   18
#define SPI_MOSI  23
#define MISO_ACCEL 19  
#define MISO_GYRO  4   
#define CS_ACCEL  5
#define CS_GYRO   17
#define CS_MAG    14   

// --- Conversion Constants ---
// Corrected for +/- 4g range: 1.95mg/LSB * 9.80665 = 0.01912
const float ACCEL_SCALE = 0.01912f; 
// Gyro: +/- 2000 dps -> 1/16.4 dps/LSB
const float GYRO_SCALE = 1.0f / 16.4f;

// --- Calibration Variables ---
bool gravity_calibrated = false;
int calibration_count = 0;
float gravity_accumulator = 0;
float gravity_mag = 9.80665f; // Default, will be updated

// --- Smoothing Constants ---
const int SMOOTH_WINDOW = 5;
float accel_mag_history[SMOOTH_WINDOW] = {0};
int smooth_idx = 0;

// --- Global Variable for Time Signature ---
int TIME_SIGNATURE = 4; // Default to 4/4. Can be changed via Serial command later.
int next_expected_beat = 1;   

// --- Beat Detection Variables ---
unsigned long last_beat_time = 0;
const int MIN_BEAT_INTERVAL = 250; 
const int MAX_BEAT_INTERVAL = 2000;
const unsigned long BPM_TIMEOUT = 3000;


float smoothed_bpm = 60;
float bpm_alpha = 0.2; 

const int NUM_BEATS_AVG = 4;
unsigned long beat_intervals[NUM_BEATS_AVG]; 
int beat_idx = 0;                      

unsigned long last_print_time = 0;
const int PRINT_INTERVAL = 100; // Only affects BPM printing

// Timing
unsigned long last_loop_time = 0;
const int LOOP_DELAY_US = 10000; // 100Hz Loop

// --- Prototypes ---
void writeRegister(int csPin, byte reg, byte val, bool isAccel);
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void updateBPM();
void detectBeat(float x, float y, float z, float ax, float ay, float az, float gx, float gy, float gz, float gyro_mag, float smoothed_mag);
bool handleMetric2(float x, float y, float z, float magnitude, float gyro_mag, float gz);
bool handleMetric3(float x, float y, float z, float magnitude, float gyro_mag, float gz);
bool handleMetric4(float x, float y, float z, float ax, float magnitude, float gyro_mag, float gz);

void setup() {
  // 1. High Speed Serial (Matches Friend's code)
  Serial.begin(921600); 
  while(!Serial); 
  
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  
  digitalWrite(CS_MAG, HIGH); 
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100); 

  // --- 2. Configure ACCEL (+/- 4g) ---
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  writeRegister(CS_ACCEL, 0x0F, 0x05, true); // Range +/- 4g
  writeRegister(CS_ACCEL, 0x10, 0x0C, true); // BW 125Hz
  SPI.end();

  delay(50);

  // --- 3. Configure GYRO (+/- 2000 dps) ---
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  writeRegister(CS_GYRO, 0x0F, 0x00, false); // Range +/- 2000dps
  writeRegister(CS_GYRO, 0x10, 0x02, false); // BW 116Hz
  SPI.end();
  
  delay(100); 
}

void loop() {
  // --- 1. Check for incoming Command (Non-blocking) ---
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim(); 

    // Protocol: "SET_SIG:3"
    if (input.startsWith("SET_SIG:")) {
      int new_sig = input.substring(8).toInt();
      if (new_sig >= 2 && new_sig <= 4) {
        TIME_SIGNATURE = new_sig;
        next_expected_beat = 1; // Reset beat counter
        Serial.print("Time: ");  Serial.println(TIME_SIGNATURE);

      }
    }
  }
  // 100Hz Loop
  unsigned long current_time = micros();
  if (current_time - last_loop_time < LOOP_DELAY_US) return;
  last_loop_time = micros();

  float dt = 0.01f;
  last_loop_time = current_time;

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  // --- Read Raw Data ---
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  readSensor(CS_ACCEL, 0x02, &raw_ax, &raw_ay, &raw_az, true);
  SPI.end(); 
  
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  readSensor(CS_GYRO, 0x02, &raw_gx, &raw_gy, &raw_gz, false);
  SPI.end();

  // --- Convert to Physical ---
  float ax_phys = raw_ax * ACCEL_SCALE;
  float ay_phys = raw_ay * ACCEL_SCALE;
  float az_phys = raw_az * ACCEL_SCALE;
  float gx_phys = raw_gx * GYRO_SCALE;
  float gy_phys = raw_gy * GYRO_SCALE;
  float gz_phys = raw_gz * GYRO_SCALE;
  float gx_rad = gx_phys * (M_PI / 180.0f);
  float gy_rad = gy_phys * (M_PI / 180.0f);
  float gz_rad = gz_phys * (M_PI / 180.0f);

  if (!gravity_calibrated) {
      float current_mag = sqrt(ax_phys*ax_phys + ay_phys*ay_phys + az_phys*az_phys);
      gravity_accumulator += current_mag;
      calibration_count++;
      
      if (calibration_count >= 100) {
          gravity_mag = gravity_accumulator / 100.0f;
          gravity_calibrated = true;
          // Serial.print("Gravity Calibrated: "); Serial.println(gravity_mag, 4);
      }
      return; // Wait until calibration is finished
  }

  // --- Run Embedded Algorithm (from MadgwickAlgo.cpp) ---
  MadgwickUpdate(gx_rad, gy_rad, gz_rad, ax_phys, ay_phys, az_phys, dt);

  // 3. Gravity Vector from Quaternions [cite: 33-35]
  float gravity_x = 2.0f * (q1 * q3 - q0 * q2);
  float gravity_y = 2.0f * (q0 * q1 + q2 * q3);
  float gravity_z = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

  // 4. Linear Acceleration (Subtract measured gravity)
  ax_phys = ax_phys - (gravity_x * gravity_mag);
  ay_phys = ay_phys - (gravity_y * gravity_mag);
  az_phys = az_phys - (gravity_z * gravity_mag);

  // 5. Visualization Data [cite: 36]
  float x_vis = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
  float y_vis = 2.0f * (q1 * q2 + q0 * q3);
  float z_vis = 2.0f * (q1 * q3 - q0 * q2);

  float screen_x = -y_vis;
  float screen_y = x_vis;
  float screen_z = -z_vis;

  // Calculate Gyro Magnitude for flicker detection
  float gyro_mag = sqrt(gx_rad*gx_rad + gy_rad*gy_rad + gz_rad*gz_rad);

  // Simple Smoothing for Accel Magnitude
  float current_magnitude = sqrt(ax_phys*ax_phys + ay_phys*ay_phys + az_phys*az_phys);
  float current_gyro_mag = sqrt(gx_rad*gx_rad + gy_rad*gy_rad + gz_rad*gz_rad);
  float current_velocity_z = screen_z - prev_z; // Simple velocity Z [cite: 78]


  // // --- CALIBRATION PRINTS FOR SERIAL PLOTTER ---
  // if (millis() - last_print_time > PRINT_INTERVAL) {
  //     // 1. To find RESTING_MAGNITUDE: Look at Magnitude when hand is still
  //     Serial.print("Magnitude:"); Serial.print(current_magnitude, 4); Serial.print(",");
      
  //     // 2. To find GYRO_CONFIRMATION_THRESHOLD: Look at peak Gyro during the 'flick'
  //     Serial.print("Gyro_Flick:"); Serial.print(current_gyro_mag, 4); Serial.print(",");
      
  //     // 3. To find MIN_VELOCITY_FOR_VALLEY: Look at Velocity_Z during slow direction change
  //     Serial.print("Velocity_Z:"); Serial.print(current_velocity_z*10, 4); Serial.print(",");
      
  //     // Threshold references (Visual lines in Plotter)
  //     Serial.print("Ref_Rest:"); Serial.print(4.5); Serial.print(",");
  //     Serial.print("Ref_Beat:"); Serial.print(6.0); Serial.print(",");
  //     Serial.print("Ref_Gyro:"); Serial.print(0.9); Serial.print(",");
  //     Serial.print("Ref_Vel:"); Serial.print(0.010f*10); Serial.print(",");
  //     Serial.println();
      
  //     last_print_time = millis();
  // }
  // if (checkForValley(screen_z, screen_x, current_velocity_z, current_magnitude, current_gyro_mag)) {
  //     Serial.println(">>>_VALLEY_DETECTED_<<<");
  //     last_beat_time = millis();
  // }
  // prev_z = screen_z;
  // Small delay to make the plot readable

  //--- OUTPUT 1: Visualization Data (CSV) ---
  //Format: DATA,x,y,z
  Serial.print("DATA,");
  Serial.print(screen_x, 4); 
  Serial.print(",");
  Serial.print(screen_y, 4); 
  Serial.print(",");
  Serial.println(screen_z, 4);

  // --- OUTPUT 2: Beat Detection Logic ---
  // Now passing both Position (screen_x/y/z) and Acceleration (b_ax/ay/az)
  detectBeat(screen_x, screen_y, screen_z, ax_phys, ay_phys, az_phys, gx_rad, gy_rad, gz_rad, current_gyro_mag, current_magnitude);

  // --- Timeout Check (Force 0 BPM if idle) ---
  if (millis() - last_beat_time > BPM_TIMEOUT) {
      smoothed_bpm = 0;
  }

  // --- Send BPM Update ---
  // We check this every loop, but print intermittently or on change
  if (millis() - last_print_time > PRINT_INTERVAL) {
      //Your Python app listens for "BPM: "
      Serial.print("BPM: ");
      Serial.println((int)smoothed_bpm); 
      last_print_time = millis();
  }
}

// --- HELPER: BPM CALCULATION ---
void updateBPM() {
    unsigned long now = millis();
    unsigned long interval = now - last_beat_time;
    last_beat_time = now;

    if (interval < MAX_BEAT_INTERVAL) {
      // Add to circular buffer
      beat_intervals[beat_idx] = interval;  
      beat_idx = (beat_idx + 1) % NUM_BEATS_AVG;

      // Average calculation
      unsigned long sum = 0;
      int valid_count = 0;
      for (int i = 0; i < NUM_BEATS_AVG; i++) {
        if (beat_intervals[i] > 0) { sum += beat_intervals[i]; valid_count++; }
      }
      
      float avg_interval = 0;
      if (valid_count > 0) avg_interval = (float)sum / valid_count;

      float raw_bpm_float = 0;
      if (avg_interval > 0) raw_bpm_float = 60000.0 / avg_interval;

      // Smoothing
      if (smoothed_bpm == 0) {
          smoothed_bpm = raw_bpm_float; 
      } else {
          float smooth_float = (bpm_alpha * raw_bpm_float) + ((1.0 - bpm_alpha) * smoothed_bpm);
          smoothed_bpm = (int)round(smooth_float);
      }
    }
}
// --- LOGIC: DETECT BEAT & BPM ---
void detectBeat(float x, float y, float z, float ax, float ay, float az, float gx, float gy, float gz, float gyro_mag, float magnitude) {
  bool beatConfirmed = false;
  switch (TIME_SIGNATURE) {
    case 2:
      beatConfirmed = handleMetric2(x, y, z, magnitude, gyro_mag, gz);
      break;
    case 3:
      beatConfirmed = handleMetric3(x, y, z, magnitude, gyro_mag, gz);
      break;
    case 4:
      beatConfirmed = handleMetric4(x, y, z, ax, magnitude, gyro_mag, gz);
      break;
  }

  if (beatConfirmed) {
      unsigned long now = millis();
      
      if (now - last_beat_time > MIN_BEAT_INTERVAL) {
          updateBPM();    

          next_expected_beat++;
          if (next_expected_beat > TIME_SIGNATURE) {
              next_expected_beat = 1;
          }

          // // --- DIAGNOSTIC BEAT LOG ---
          // Serial.println("---------- BEAT DETECTED ----------");
          // Serial.print("Beat Number: "); Serial.println(next_expected_beat - 1 == 0 ? TIME_SIGNATURE : next_expected_beat - 1);
          // Serial.print("Accel Magnitude: "); Serial.println(magnitude, 4); 
          // Serial.print("Accel X: "); Serial.print(ax, 4);
          // Serial.print("Accel Y: "); Serial.print(ay, 4);
          // Serial.print("Accel Z: "); Serial.println(az, 4);
          // Serial.print("Gyro Flick Mag: "); Serial.println(gyro_mag, 4);
          // Serial.print("Gyro X: "); Serial.print(gx, 4);
          // Serial.print("Gyro Y: "); Serial.print(gy, 4);
          // Serial.print("Gyro Z: "); Serial.println(gz, 4);
          // Serial.print("Position (X, Z): "); Serial.print(x, 4); Serial.print(", "); Serial.println(z, 4);
          // Serial.print("Position (apex_x): "); Serial.println(apex_x, 4);
          // Serial.println("-----------------------------------");

          // --- NEW: Send Trigger to Python ---
          Serial.println("BEAT_TRIG");
          Serial.print("BEAT: "); Serial.println(next_expected_beat - 1 == 0 ? TIME_SIGNATURE : next_expected_beat - 1);
      }
  }
}
// --- Logic for 2/4 Time Signature ---
bool handleMetric2(float x, float y, float z, float magnitude, float gyro_mag, float gz) {
  // 1. Calculate Velocity & Magnitude
  float velocity_z = z - prev_z;

  // 2. Update Previous Z for next loop
  prev_z = z;

  // 3. Check Physics (Using the helper function)
  bool valley_found = checkForValley(z, x, velocity_z, magnitude, gyro_mag);

  // If no valley found this frame, we stop here.
  if (!valley_found) return false;

  // --- IF WE ARE HERE, A VALLEY WAS JUST DETECTED ---
  // Now we apply the Rules of Conducting (Geometry & Force)

  // Rule A: The wand must not be resting
  if (magnitude < RESTING_MAGNITUDE) return false;

  // Rule B: Check Beat Expectations
  switch (next_expected_beat) {
    case 1:
        if (checkBeat1LogicWithWeight2(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    case 2:
        if (checkBeat2LogicWithWeight2(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    default:
      break;
  }

  return false;
}

// --- Logic for 3/4 Time Signature ---
// Pattern: 1 (Down), 2 (Out/Right), 3 (Up)
bool handleMetric3(float x, float y, float z, float magnitude, float gyro_mag, float gz) {
  // 1. Calculate Velocity & Magnitude
  float velocity_z = z - prev_z;

  // 2. Update Previous Z for next loop
  prev_z = z; 
  // 3. Check Physics (Using the helper function)
  bool valley_found = checkForValley(z, x, velocity_z, magnitude, gyro_mag);
  // If no valley found this frame, we stop here.
  if (!valley_found) return false;
  // Rule A: The wand must not be resting
  if (magnitude < RESTING_MAGNITUDE) return false;

  // Rule B: Check Beat Expectations
  switch (next_expected_beat) {
    case 1:
        if (checkBeat1LogicWithWeight3(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    case 2:
        if (checkBeat2LogicWithWeight3(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    case 3:
        if (checkBeat3LogicWithWeight3(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    default:
      break;
  }

  return false;
}

// --- Logic for 4/4 Time Signature ---
// Pattern: 1 (Down), 2 (In/Left), 3 (Out/Right), 4 (Up)
bool handleMetric4(float x, float y, float z, float ax, float magnitude, float gyro_mag, float gz) {
  float velocity_z = z - prev_z;
  prev_z = z; 
  
  bool valley_found = checkForValley(z, x, velocity_z, magnitude, gyro_mag);
  if (!valley_found) return false;
  if (magnitude < RESTING_MAGNITUDE) return false;
  
  switch (next_expected_beat) {
    case 1:
        if (checkBeat1LogicWithWeight4(magnitude, ax, z, x, next_expected_beat, gz)) return true;
        break;
    case 2:
        if (checkBeat2LogicWithWeight4(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    case 3:
        if (checkBeat3LogicWithWeight4(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    case 4:
        if (checkBeat4LogicWithWeight4(magnitude, z, x, next_expected_beat, gz)) return true;
        break;
    default:
      break;
  }
  return false;
}

// --- SPI Helpers ---
void writeRegister(int csPin, byte reg, byte val, bool isAccel) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  digitalWrite(csPin, LOW); 
  SPI.transfer(reg & 0x7F); 
  SPI.transfer(val);
  digitalWrite(csPin, HIGH); 
  SPI.endTransaction();
}

void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel) {
  byte data[6]; 
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0)); 
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
