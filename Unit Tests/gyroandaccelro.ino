/*
 * Project: Conductors Wand (Smart Baton)
 * Component: 6-Axis IMU Data Acquisition
 * Platform: ESP32 with BMX055 via SPI
 * * Description:
 * This sketch reads raw data from the Accelerometer and Gyroscope.
 * It converts the raw integer values (LSB) into standard physical units:
 * 1. Accelerometer: Meters per Second Squared (m/s^2)
 * 2. Gyroscope: Degrees per Second (deg/s)
 * * Note: No offset calibration is applied. The data reflects the absolute raw
 * reading from the sensor, including natural Zero-G offset and Gyro drift.
 * * Hardware Constraint:
 * The Magnetometer is explicitly disabled (CS_MAG = HIGH) to prevent
 * data bus contention on Pin 4 (MISO).
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
#define CS_GYRO   13
#define CS_MAG    14   // Used only to keep the Magnetometer disabled

// Sensor Register Addresses
#define ACCEL_DATA_START_REG 0x02 
#define GYRO_DATA_START_REG  0x02

// --- Conversion Constants ---

// Accelerometer Scaling:
// Range: +/- 2g
// Sensitivity: ~1024 LSB = 1g
// Gravity (1g) = 9.81 m/s^2
// Factor = 9.81 / 1024.0
const float ACCEL_SCALE = 9.81 / 1024.0; 

// Gyroscope Scaling:
// Range: +/- 2000 degrees/second
// Sensitivity: ~16.4 LSB = 1 dps
// Factor = 1.0 / 16.4
const float GYRO_SCALE = 1.0 / 16.4;

void setup() {
  Serial.begin(115200);
  while(!Serial); // Wait for Serial Monitor to initialize
  
  // Initialize Chip Select pins
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  
  // --- Hardware Safety ---
  // Force Magnetometer CS High to disable it and free up MISO Pin 4
  digitalWrite(CS_MAG, HIGH); 
  
  // Set initial state for active sensors (High = Inactive)
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100); // Allow sensors to power up
}

void loop() {
  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  // --- 1. Read Raw Accelerometer Data ---
  // Switch SPI context to Pin 19
  SPI.end(); 
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  // 'true' flag -> Apply bit shifting for 12-bit resolution
  readSensor(CS_ACCEL, ACCEL_DATA_START_REG, &raw_ax, &raw_ay, &raw_az, true);
  
  // --- 2. Read Raw Gyroscope Data ---
  // Switch SPI context to Pin 4
  SPI.end();
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  // 'false' flag -> No bit shifting needed for 16-bit resolution
  readSensor(CS_GYRO, GYRO_DATA_START_REG, &raw_gx, &raw_gy, &raw_gz, false);

  // --- 3. Convert to Physical Units ---
  
  // Convert LSB to m/s^2
  float ax_phys = raw_ax * ACCEL_SCALE;
  float ay_phys = raw_ay * ACCEL_SCALE;
  float az_phys = raw_az * ACCEL_SCALE;

  // Convert LSB to deg/s
  // Note: Since we are not calculating offset, this value includes drift.
  float gx_phys = raw_gx * GYRO_SCALE;
  float gy_phys = raw_gy * GYRO_SCALE;
  float gz_phys = raw_gz * GYRO_SCALE;

  // --- 4. Output Data (Serial Plotter Format) ---
  // Format: "Label:Value,Label:Value..."
  
  Serial.print("AccX:"); Serial.print(ax_phys);
  Serial.print(",AccY:"); Serial.print(ay_phys);
  Serial.print(",AccZ:"); Serial.print(az_phys);
  
  Serial.print(",GyrX:"); Serial.print(gx_phys);
  Serial.print(",GyrY:"); Serial.print(gy_phys);
  Serial.print(",GyrZ:"); Serial.println(gz_phys);

  // Sampling Rate Control
  // 20ms delay results in approximately 50Hz sampling rate
  delay(20); 
}

/**
 * readSensor
 * ----------------
 * Generic function to perform SPI burst reads.
 * Handles the specific bit-shifting requirements for the BMX055 Accelerometer.
 */
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel) {
  byte data[6]; // Buffer for X_LSB, X_MSB, Y_LSB, Y_MSB, Z_LSB, Z_MSB
  
  // Configure SPI: 1MHz clock, MSB First, Mode 0
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  
  digitalWrite(csPin, LOW); // Select Sensor
  SPI.transfer(startReg | 0x80); // Send register address + Read Bit
  
  // Burst read 6 bytes
  for (int i = 0; i < 6; i++) {
    data[i] = SPI.transfer(0x00);
  }
  
  digitalWrite(csPin, HIGH); // Deselect Sensor
  SPI.endTransaction();

  // --- Data Conversion ---
  if (isAccel) {
      // Accelerometer: 12-bit data inside 16-bit container.
      // Requires shifting right by 4 bits to remove padding.
      *x = (int16_t)((data[1] << 8) | data[0]) >> 4; 
      *y = (int16_t)((data[3] << 8) | data[2]) >> 4;
      *z = (int16_t)((data[5] << 8) | data[4]) >> 4;
  } else {
      // Gyroscope: Standard 16-bit data. No shift required.
      *x = (int16_t)((data[1] << 8) | data[0]);
      *y = (int16_t)((data[3] << 8) | data[2]);
      *z = (int16_t)((data[5] << 8) | data[4]);
  }
}