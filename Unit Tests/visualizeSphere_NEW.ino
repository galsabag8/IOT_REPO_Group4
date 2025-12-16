/*
 * Project: Conductors Wand (Smart Baton) - Embedded Algorithm Version
 * Hardware: ESP32 + BMX055
 * Logic: Reads sensors -> Embedded Madgwick Math -> Rotation Math -> Sends final X,Y,Z
 * * FIX: Removed external Madgwick library dependency to solve compilation errors.
 */

#include <SPI.h>
#include <math.h>

// --- Wiring Configuration ---
#define SPI_CLK   18
#define SPI_MOSI  23
#define MISO_ACCEL 19  
#define MISO_GYRO  4   
#define CS_ACCEL  5
#define CS_GYRO   17
#define CS_MAG    14   

// --- Madgwick Global Variables ---
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f; // The Quaternion
float beta = 0.1f; // Gain: 0.1 is smooth, higher is faster response

// --- Conversion Constants ---
// Accel: +/- 4g -> 0.009807 mg/LSB
const float ACCEL_SCALE = 0.009807f; 
// Gyro: +/- 2000 dps -> 0.0609 dps/LSB
const float GYRO_SCALE = 1.0f / 16.4f;

// --- Timing ---
unsigned long last_loop_time = 0;
const int LOOP_DELAY_US = 10000; // 10ms = 100Hz

// --- Function Prototypes ---
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void writeRegister(int csPin, byte reg, byte val, bool isAccel);
void MadgwickUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt);

void setup() {
  // 1. Serial Setup
  Serial.begin(921600); 
  while(!Serial); 
  
  // 2. Pins Setup
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  digitalWrite(CS_MAG, HIGH); 
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100); 

  // 3. Configure ACCELEROMETER (Range +/- 4g)
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  writeRegister(CS_ACCEL, 0x0F, 0x05, true); // +/- 4g
  writeRegister(CS_ACCEL, 0x10, 0x0C, true); // 125Hz Bandwidth
  SPI.end();

  delay(50);

  // 4. Configure GYROSCOPE (Range +/- 2000 dps)
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  writeRegister(CS_GYRO, 0x0F, 0x00, false); // +/- 2000 dps
  writeRegister(CS_GYRO, 0x10, 0x02, false); // 116Hz Bandwidth
  SPI.end();
  
  delay(100); 
  Serial.println("System Ready. Using Embedded Madgwick.");
}

void loop() {
  unsigned long current_time = micros();
  if (current_time - last_loop_time < LOOP_DELAY_US) {
    return;
  }
  
  // חישוב Delta Time אמיתי לשמירה על דיוק גם אם יש גמגומים
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

  // --- Convert to Physical Units ---
  float ax = (float)raw_ax * ACCEL_SCALE;
  float ay = (float)raw_ay * ACCEL_SCALE;
  float az = (float)raw_az * ACCEL_SCALE;

  // המרה לרדיאנים! האלגוריתם המתמטי הגולמי עובד ברדיאנים
  float gx_deg = (float)raw_gx * GYRO_SCALE;
  float gy_deg = (float)raw_gy * GYRO_SCALE;
  float gz_deg = (float)raw_gz * GYRO_SCALE;
  
  float gx_rad = gx_deg * (M_PI / 180.0f);
  float gy_rad = gy_deg * (M_PI / 180.0f);
  float gz_rad = gz_deg * (M_PI / 180.0f);

  // --- Run Embedded Algorithm ---
  MadgwickUpdate(gx_rad, gy_rad, gz_rad, ax, ay, az, dt);

  // --- The Magic Math: Rotate Vector [1,0,0] ---
  // משתמשים במשתנים הגלובליים q0, q1, q2, q3 שעודכנו בפונקציה
  float x_val = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
  float y_val = 2.0f * (q1 * q2 + q0 * q3);
  float z_val = 2.0f * (q1 * q3 - q0 * q2);

  // --- Screen Mapping ---
  float screen_x = -y_val;
  float screen_y = x_val;
  float screen_z = -z_val;

  // --- Send Data ---
  Serial.print(screen_x, 4); 
  Serial.print(",");
  Serial.print(screen_y, 4); 
  Serial.print(",");
  Serial.println(screen_z, 4);
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
// מקור: Sebastian Madgwick's open source implementation
// חישוב זה מעדכן את המשתנים הגלובליים q0, q1, q2, q3
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