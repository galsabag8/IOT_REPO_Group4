/*
 * Project: Conductors Wand - Hybrid V1 (Vis + BPM)
 * Features: 
 * - High-speed Motion Data (for Visualization)
 * - Real-time BPM Calculation (for Music Control)
 * - Dual Filtering (Responsive for Vis, Smooth for Beats)
 */

#include <SPI.h>

// --- Wiring ---
#define SPI_CLK   18
#define SPI_MOSI  23
#define MISO_ACCEL 19  
#define MISO_GYRO  4   
#define CS_ACCEL  5
#define CS_GYRO   17
#define CS_MAG    14   

// --- Madgwick Global Variables ---
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f; // The Quaternion
float beta = 0.03f; // Gain: 0.1 is smooth, higher is faster response

// --- Conversion Constants ---
// Accel: +/- 4g -> 0.009807 mg/LSB
const float ACCEL_SCALE = 0.009807f; 
// Gyro: +/- 2000 dps -> 0.0609 dps/LSB
const float GYRO_SCALE = 1.0f / 16.4f;

// --- Dual Filters ---
// Filter 1: VISUALIZATION (Responsive, alpha 0.8)
//const float alpha_vis = 0.8;
//float v_ax = 0, v_ay = 0, v_az = 0;
//float v_gx = 0, v_gy = 0, v_gz = 0;

// Filter 2: BEAT DETECTION (Smooth, alpha 0.15)
const float alpha_beat = 0.15;
float b_ax = 0, b_ay = 0, b_az = 0;

// --- Global Variable for Time Signature ---
int TIME_SIGNATURE = 2; // Default to 4/4. Can be changed via Serial command later.
int next_expected_beat = 1;   // המערכת תמיד מתחילה בציפייה לפעמה הראשונה (ה-Downbeat)

// --- Beat Detection Variables (From Your Code) ---
float beat_threshold = 8.0; 
const float MIN_VELOCITY_FOR_VALLEY = 0.02f;
//float min_threshold = 12.5;  
//float decay_rate = 0.99;
unsigned long last_beat_time = 0;
const int MIN_BEAT_INTERVAL = 250; 
const int MAX_BEAT_INTERVAL = 2000;
const unsigned long BPM_TIMEOUT = 3000;
// --- Noise Filtering Constants ---


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
void MadgwickUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt);
void updateBPM();
void detectBeat(float x, float y, float z, float ax, float ay, float az);
bool handleMetric2(float x, float y, float z, float ax, float ay, float az);
bool handleMetric3(float x, float y, float z, float ax, float ay, float az);
bool handleMetric4(float x, float y, float z, float ax, float ay, float az);
bool checkForValley(float z, float x, float velocity_z);

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

  // --- Run Embedded Algorithm ---
  MadgwickUpdate(gx_rad, gy_rad, gz_rad, ax_phys, ay_phys, az_phys, dt);

  // --- The Magic Math: Rotate Vector [1,0,0] ---
  // משתמשים במשתנים הגלובליים q0, q1, q2, q3 שעודכנו בפונקציה
  float x_val = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
  float y_val = 2.0f * (q1 * q2 + q0 * q3);
  float z_val = 2.0f * (q1 * q3 - q0 * q2);

  // --- Screen Mapping ---
  float screen_x = -y_val;
  float screen_y = x_val;
  float screen_z = -z_val;

  //--- OUTPUT 1: Visualization Data (CSV) ---
  //Format: DATA,x,y,z
  Serial.print("DATA,");
  Serial.print(screen_x, 4); 
  Serial.print(",");
  Serial.print(screen_y, 4); 
  Serial.print(",");
  Serial.println(screen_z, 4);

    // --- 1. BEAT DETECTION FILTER (Smooth) ---
  b_ax = (alpha_beat * ax_phys) + ((1.0 - alpha_beat) * b_ax);
  b_ay = (alpha_beat * ay_phys) + ((1.0 - alpha_beat) * b_ay);
  b_az = (alpha_beat * az_phys) + ((1.0 - alpha_beat) * b_az);

  // --- OUTPUT 2: Beat Detection Logic ---
  // Now passing both Position (screen_x/y/z) and Acceleration (b_ax/ay/az)
  detectBeat(screen_x, screen_y, screen_z, b_ax, b_ay, b_az);

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
void detectBeat(float x, float y, float z, float ax, float ay, float az) {
  bool beatConfirmed = false;

  // 1. בדיקה פיזיקלית: האם התנועה מתאימה לפעמה?
  switch (TIME_SIGNATURE) {
    case 2:
      beatConfirmed = handleMetric2(x, y, z, ax, ay, az);
      break;
    case 3:
      beatConfirmed = handleMetric3(x, y, z, ax, ay, az);
      break;
    case 4:
    default:
      beatConfirmed = handleMetric4(x, y, z, ax, ay, az);
      break;
  }

  if (beatConfirmed) {
      unsigned long now = millis();
      
      if (now - last_beat_time > MIN_BEAT_INTERVAL) {
          
          // רק אם עברנו את בדיקת הזמן, מעדכנים את ה-BPM
          updateBPM();     
          
          // ומקדמים את הפעמה הבאה
          next_expected_beat++;
          if (next_expected_beat > TIME_SIGNATURE) {
              next_expected_beat = 1;
          }
          
          //Serial.print("BEAT: "); Serial.println(next_expected_beat - 1 == 0 ? TIME_SIGNATURE : next_expected_beat - 1);
      }
  }
}

// ==========================================
//      GLOBAL VARIABLES FOR BEAT LOGIC
// ==========================================

// --- 1. Motion Tracking (Velocity & Direction) ---
// Used to calculate Z-velocity and detect direction changes
float prev_z = 0.0f;       
int z_direction = -1;       // -1 = Down, 1 = Up, 0 = Static

// --- 2. Relative Logic (Adaptive Reference Points) ---
// These store the position of the LAST detected beat to compare against
float last_valid_beat_z = -0.5f; 
float last_valid_beat_x = -0.5f; 

// --- 3. Tuning Constants (Thresholds) ---
// --- Constants ---
const float RESTING_MAGNITUDE = 8.0f;       // Minimum force to consider a beat
const float MAX_HEIGHT_DIFF = 0.05f;         // Beat 2 must be 5cm higher
const float MIN_WIDTH_DIFF = 0.0005f;          // Beat 2 must be 3cm to the right

float local_min_z = 100.0f;  // Tracks the lowest point during a descent
float local_min_x = 0.0f;    // Tracks the X position at that lowest point

/**
 * Physics State Machine
 * Returns TRUE only at the exact moment the wand switches from Down to Up.
 * It automatically tracks the lowest point (local_min_z) during the descent.
 */
 float apex_x = 0.0f;

bool checkForValley(float z, float x, float velocity_z) {
    
    // CASE 1: We are currently moving DOWN
    if (z_direction == -1) {
        
        // A. Track the absolute bottom point
        if (z < local_min_z) {
            local_min_z = z;
            local_min_x = x;
        }

        // B. Check for state switch (Down -> Up)
        // We require significant upward velocity to confirm the switch
        if (velocity_z > MIN_VELOCITY_FOR_VALLEY) {
            // Change state to UP
            z_direction = 1; 
            //Serial.println("valley found");
            return true; // Valley Detected!
        }
    }
    // CASE 2: We are currently moving UP
    else if (z_direction == 1) {
        
        // Check for state switch (Up -> Down)
        // We require significant downward velocity to confirm the switch
        if (velocity_z < -MIN_VELOCITY_FOR_VALLEY) {
            // Change state to DOWN
            z_direction = -1;
            apex_x = x;
            // Reset local min tracking for the new descent
            local_min_z = 100.0f; 
        }
    }

    return false; // No state change / No valley detected this frame
}

bool handleMetric2(float x, float y, float z, float ax, float ay, float az) {
  // 1. Calculate Velocity & Magnitude
  float velocity_z = z - prev_z;
  float magnitude = sqrt(ax*ax + ay*ay + az*az);
  
  // 2. Update Previous Z for next loop
  prev_z = z; 

  // 3. Check Physics (Using the helper function)
  bool valley_found = checkForValley(z, x, velocity_z);

  // If no valley found this frame, we stop here.
  if (!valley_found) return false;

  // --- IF WE ARE HERE, A VALLEY WAS JUST DETECTED ---
  // Now we apply the Rules of Conducting (Geometry & Force)

  // Rule A: The wand must not be resting
  if (magnitude < RESTING_MAGNITUDE) return false;

  // Rule B: Check Beat Expectations
  if (next_expected_beat == 1) {
      // --- EXPECTING BEAT 1 (The Deep Beat) ---
      if (magnitude > beat_threshold) {
          // SUCCESS: Beat 1 Confirmed
          last_valid_beat_z = z; 
          last_valid_beat_x = x; 
          return true; 
      }
  } 
  else if (next_expected_beat == 2) {
      // --- EXPECTING BEAT 2 (The Higher, Wider Beat) ---
      
      // Geometric Rule: Must be HIGHER or close and to the RIGHT of Beat 1
      bool is_higher = (z + MAX_HEIGHT_DIFF >= last_valid_beat_z);
      bool approached_from_right = (apex_x > x);
      //Serial.print("z: "); Serial.print((float)z); Serial.print("  last_valid_beat_z: "); Serial.println((float)last_valid_beat_z);
      //Serial.print("apex_x: "); Serial.print((float)apex_x); Serial.print("  x: "); Serial.println((float)x);

      if (is_higher && approached_from_right && (magnitude > (beat_threshold * 0.7))) {
          // SUCCESS: Beat 2 Confirmed
          last_valid_beat_z = z; 
          last_valid_beat_x = x; 
          return true;
      }
      // --- ERROR RECOVERY (maybe its a 1 BEAT again)
      bool is_strong = (magnitude > beat_threshold * 1.25);
      if (is_strong) {
          // Serial.println(">>> MISSED BEAT 2 -> RESETTING <<<");    
           // update last_valid to the last bit
           last_valid_beat_z = z; 
           last_valid_beat_x = x; 
           next_expected_beat = 2; // expecting beat num 1 next time (detectBeat will fix it to 1)
           return true;
      }
  }

  return false;
}

// --- Logic for 3/4 Time Signature ---
// Pattern: 1 (Down), 2 (Out/Right), 3 (Up)
bool handleMetric3(float x, float y, float z, float ax, float ay, float az) {
  float magnitude = sqrt(ax*ax + ay*ay + az*az);
  if (magnitude < beat_threshold) return false;

  if (next_expected_beat == 1) {
      // Expecting DOWNBEAT
      return true;
  } 
  else if (next_expected_beat == 2) {
      // Expecting BEAT 2 (OUT/RIGHT)
      // TODO: Check if X is positive (moving right for right-handed conductor)
      return true;
  }
  else if (next_expected_beat == 3) {
      // Expecting BEAT 3 (UP)
      // TODO: Check if Z is positive (moving up)
      return true;
  }
  return false;
}

// --- Logic for 4/4 Time Signature ---
// Pattern: 1 (Down), 2 (In/Left), 3 (Out/Right), 4 (Up)
bool handleMetric4(float x, float y, float z, float ax, float ay, float az) {
  float magnitude = sqrt(ax*ax + ay*ay + az*az);
  if (magnitude < beat_threshold) return false;

  if (next_expected_beat == 1) {
      // Expecting DOWNBEAT
      // Strong downward motion
      return true;
  } 
  else if (next_expected_beat == 2) {
      // Expecting BEAT 2 (IN/LEFT)
      // Note: This is different from 3/4! Here beat 2 is Inwards.
      // TODO: Check if X is negative (moving left)
      return true;
  }
  else if (next_expected_beat == 3) {
      // Expecting BEAT 3 (OUT/RIGHT)
      // TODO: Check if X is positive (moving right)
      return true;
  }
  else if (next_expected_beat == 4) {
      // Expecting BEAT 4 (UP)
      // Weak upward motion
      return true;
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

// --- Embedded Madgwick Algorithm ---
// Sebastian Madgwick's open source implementation
void MadgwickUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt) {
  float recipNorm;
  float s0, s1, s2, s3;
  float qDot1, qDot2, qDot3, qDot4;
  float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

  // Rate of change of quaternion from gyroscope
  qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
  qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
  qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
  qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

  // Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
  if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

    // Normalise accelerometer measurement
    recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    // Auxiliary variables to avoid repeated arithmetic
    _2q0 = 2.0f * q0;
    _2q1 = 2.0f * q1;
    _2q2 = 2.0f * q2;
    _2q3 = 2.0f * q3;
    _4q0 = 4.0f * q0;
    _4q1 = 4.0f * q1;
    _4q2 = 4.0f * q2;
    _8q1 = 8.0f * q1;
    _8q2 = 8.0f * q2;
    q0q0 = q0 * q0;
    q1q1 = q1 * q1;
    q2q2 = q2 * q2;
    q3q3 = q3 * q3;

    // Gradient decent algorithm corrective step
    s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
    s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
    s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
    s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
    recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); // normalise step magnitude
    s0 *= recipNorm;
    s1 *= recipNorm;
    s2 *= recipNorm;
    s3 *= recipNorm;

    // Apply feedback step
    qDot1 -= beta * s0;
    qDot2 -= beta * s1;
    qDot3 -= beta * s2;
    qDot4 -= beta * s3;
  }

  // Integrate rate of change of quaternion to yield quaternion
  q0 += qDot1 * dt;
  q1 += qDot2 * dt;
  q2 += qDot3 * dt;
  q3 += qDot4 * dt;

  // Normalise quaternion
  recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm;
  q1 *= recipNorm;
  q2 *= recipNorm;
  q3 *= recipNorm;
}