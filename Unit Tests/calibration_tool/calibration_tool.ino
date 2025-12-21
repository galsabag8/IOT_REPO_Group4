/*
 * Calibration Tool for Conductor Wand (Updated with Velocity Z)
 * Use this to find the noise floor, resting magnitude, and Z-velocity noise.
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

// --- Madgwick Variables ---
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
float beta = 0.03f; 

// --- Constants ---
const float ACCEL_SCALE = 0.009807f; 
const float GYRO_SCALE = 1.0f / 16.4f;

// --- Calibration Variables ---
float min_x = 1000, max_x = -1000;
float min_y = 1000, max_y = -1000;
float min_z = 1000, max_z = -1000;

// New: Velocity Z Variables
float min_vel_z = 1000, max_vel_z = -1000;
float prev_screen_z = 0;
bool first_sample = true; // To skip the first calculation

float max_mag = 0;
float avg_mag_sum = 0;
int sample_count = 0;

unsigned long calibration_start_time = 0;
const int CALIBRATION_WINDOW_MS = 3000; // Report every 3 seconds

// --- Timing ---
unsigned long last_loop_time = 0;
const int LOOP_DELAY_US = 10000; // 100Hz

// --- Prototypes ---
void writeRegister(int csPin, byte reg, byte val, bool isAccel);
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void MadgwickUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt);

void setup() {
  Serial.begin(921600);
  while(!Serial);

  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  
  digitalWrite(CS_MAG, HIGH); 
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100);

  // --- Configure ACCEL ---
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  writeRegister(CS_ACCEL, 0x0F, 0x05, true); 
  writeRegister(CS_ACCEL, 0x10, 0x0C, true); 
  SPI.end();

  delay(50);

  // --- Configure GYRO ---
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  writeRegister(CS_GYRO, 0x0F, 0x00, false); 
  writeRegister(CS_GYRO, 0x10, 0x02, false); 
  SPI.end();
  
  delay(100);
  
  Serial.println("=== CALIBRATION STARTED ===");
  Serial.println("Keep the wand steady...");
  calibration_start_time = millis();
}

void loop() {
  unsigned long current_time = micros();
  if (current_time - last_loop_time < LOOP_DELAY_US) return;
  
  // Calculate dt
  float dt = (float)(current_time - last_loop_time) / 1000000.0f;
  last_loop_time = current_time;

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  // --- Read Sensors ---
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  readSensor(CS_ACCEL, 0x02, &raw_ax, &raw_ay, &raw_az, true);
  SPI.end(); 
  
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  readSensor(CS_GYRO, 0x02, &raw_gx, &raw_gy, &raw_gz, false);
  SPI.end();

  // --- Convert ---
  float ax_phys = raw_ax * ACCEL_SCALE;
  float ay_phys = raw_ay * ACCEL_SCALE;
  float az_phys = raw_az * ACCEL_SCALE;
  float gx_rad = raw_gx * GYRO_SCALE * (M_PI / 180.0f);
  float gy_rad = raw_gy * GYRO_SCALE * (M_PI / 180.0f);
  float gz_rad = raw_gz * GYRO_SCALE * (M_PI / 180.0f);

  // --- Filter (Madgwick) ---
  MadgwickUpdate(gx_rad, gy_rad, gz_rad, ax_phys, ay_phys, az_phys, dt);

  // --- Get Screen Coordinates ---
  float x_val = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
  float y_val = 2.0f * (q1 * q2 + q0 * q3);
  float z_val = 2.0f * (q1 * q3 - q0 * q2);

  float screen_x = -y_val;
  float screen_y = x_val;
  float screen_z = -z_val;
  
  // --- Calculate Velocity Z ---
  // (Current Z - Previous Z)
  float velocity_z = 0;
  
  if (first_sample) {
      prev_screen_z = screen_z;
      first_sample = false;
  } else {
      velocity_z = screen_z - prev_screen_z;
      prev_screen_z = screen_z; // Update for next loop
  }

  // --- Calculate Magnitude ---
  float magnitude = sqrt(ax_phys*ax_phys + ay_phys*ay_phys + az_phys*az_phys);

  // --- Accumulate Stats ---
  if (screen_x < min_x) min_x = screen_x;
  if (screen_x > max_x) max_x = screen_x;
  
  if (screen_y < min_y) min_y = screen_y;
  if (screen_y > max_y) max_y = screen_y;
  
  if (screen_z < min_z) min_z = screen_z;
  if (screen_z > max_z) max_z = screen_z;

  // Track Velocity Z stats
  if (velocity_z < min_vel_z) min_vel_z = velocity_z;
  if (velocity_z > max_vel_z) max_vel_z = velocity_z;
  
  if (magnitude > max_mag) max_mag = magnitude;
  avg_mag_sum += magnitude;
  sample_count++;

  // --- Report every 3 seconds ---
  if (millis() - calibration_start_time > CALIBRATION_WINDOW_MS) {
      float noise_x = max_x - min_x;
      float noise_y = max_y - min_y;
      float noise_z = max_z - min_z;
      float noise_vel_z = max_vel_z - min_vel_z;
      float avg_mag = avg_mag_sum / sample_count;

      Serial.println("\n--- 3 Second Report ---");
      Serial.print("Drift X (Noise): "); Serial.println(noise_x, 4);
      Serial.print("Drift Y (Noise): "); Serial.println(noise_y, 4);
      Serial.print("Drift Z (Noise): "); Serial.println(noise_z, 4);
      Serial.println("- - - - - - - - - - - -");
      Serial.print("Drift Vel Z (Noise): "); Serial.println(noise_vel_z, 6); // High precision
      Serial.println("- - - - - - - - - - - -");
      Serial.print("Max Magnitude:   "); Serial.println(max_mag, 2);
      Serial.print("Avg Magnitude:   "); Serial.println(avg_mag, 2);
      
      Serial.println("-----------------------");
      Serial.print("RECOMMENDATION for beat_threshold:  > "); Serial.println(max_mag * 1.2, 2);
      Serial.print("RECOMMENDATION for valley threshold:> "); Serial.println(noise_vel_z * 1.5, 6); 
      Serial.println("-----------------------");

      // Reset for next window
      min_x = 1000; max_x = -1000;
      min_y = 1000; max_y = -1000;
      min_z = 1000; max_z = -1000;
      
      min_vel_z = 1000; max_vel_z = -1000;
      
      max_mag = 0;
      avg_mag_sum = 0;
      sample_count = 0;
      calibration_start_time = millis();
  }
}

// --- Helper Functions ---
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

void MadgwickUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt) {
  float recipNorm;
  float s0, s1, s2, s3;
  float qDot1, qDot2, qDot3, qDot4;
  float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

  qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
  qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
  qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
  qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

  if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
    recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

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

    s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
    s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
    s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
    s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
    recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); 
    s0 *= recipNorm;
    s1 *= recipNorm;
    s2 *= recipNorm;
    s3 *= recipNorm;

    qDot1 -= beta * s0;
    qDot2 -= beta * s1;
    qDot3 -= beta * s2;
    qDot4 -= beta * s3;
  }

  q0 += qDot1 * dt;
  q1 += qDot2 * dt;
  q2 += qDot3 * dt;
  q3 += qDot4 * dt;

  recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm;
  q1 *= recipNorm;
  q2 *= recipNorm;
  q3 *= recipNorm;
}