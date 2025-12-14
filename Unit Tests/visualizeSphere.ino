/*
 * Project: Conductors Wand (Smart Baton) - Optimized v4
 * Hardware: ESP32 + BMX055
 * Improvements: 
 * - Higher Baud Rate (921600) for lower latency
 * - Correct Sensor Ranges (Accel +-4g, Gyro 2000dps) for fast motion
 * - 100Hz Sample Rate
 */

#include <SPI.h>

// --- Wiring Configuration ---
#define SPI_CLK   18
#define SPI_MOSI  23
#define MISO_ACCEL 19  
#define MISO_GYRO  4   
#define CS_ACCEL  5
#define CS_GYRO   17
#define CS_MAG    14   

// --- Conversion Constants (UPDATED FOR NEW RANGES) ---
// Accel: Set to +/- 4g. 
// Resolution is 12-bit (signed). 2048 LSB = 1g (approx) -> Scale = 9.81 / 512.0
// Range is +/-4g over 12 bits implies LSB resolution changes.
// Datasheet: +/-4g -> 0.98mg/LSB. 
const float ACCEL_SCALE = 0.009807; 

// Gyro: Set to +/- 2000 dps.
// 16-bit signed. 
// Datasheet: 2000dps -> 16.4 LSB/dps (Scale = 1/16.4 = 0.0609)
const float GYRO_SCALE = 1.0 / 16.4;

// --- Filter Configuration ---
// Alpha 0.8 means we trust new data 80%. Light smoothing.
// At 100Hz, this is responsive.
const float alpha = 0.8; 
float f_ax = 0, f_ay = 0, f_az = 0;
float f_gx = 0, f_gy = 0, f_gz = 0;

// Timing
unsigned long last_loop_time = 0;
const int LOOP_DELAY_US = 10000; // 10ms = 100Hz

// Function Prototypes
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void writeRegister(int csPin, byte reg, byte val, bool isAccel);

void setup() {
  // 1. High Speed Serial
  Serial.begin(921600); 
  while(!Serial); 
  
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  
  digitalWrite(CS_MAG, HIGH); 
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100); 

  // --- 2. Configure ACCELEROMETER (Range +/- 4g) ---
  // Need to init SPI for Accel first
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  // Reg 0x0F (PMU_RANGE) -> 0x05 = +/- 4g (Default is usually +/- 2g)
  writeRegister(CS_ACCEL, 0x0F, 0x05, true); 
  // Reg 0x10 (PMU_BW) -> 0x0C = 125Hz Bandwidth (Good for 100Hz loop)
  writeRegister(CS_ACCEL, 0x10, 0x0C, true);
  SPI.end();

  delay(50);

  // --- 3. Configure GYROSCOPE (Range +/- 2000 dps) ---
  // Need to switch SPI to Gyro pins
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  // Reg 0x0F (Range) -> 0x00 = +/- 2000 dps (Default is +/- 250 dps - TOO SLOW!)
  writeRegister(CS_GYRO, 0x0F, 0x00, false);
  // Reg 0x10 (BW) -> 0x02 = 116Hz Bandwidth
  writeRegister(CS_GYRO, 0x10, 0x02, false);
  SPI.end();
  
  delay(100); 
  Serial.println("System Ready. High Speed Mode.");
}

void loop() {
  // 100Hz Loop
  if (micros() - last_loop_time < LOOP_DELAY_US) {
    return;
  }
  last_loop_time = micros();

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  // --- Read Accel ---
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  readSensor(CS_ACCEL, 0x02, &raw_ax, &raw_ay, &raw_az, true);
  SPI.end(); 
  
  // --- Read Gyro ---
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  readSensor(CS_GYRO, 0x02, &raw_gx, &raw_gy, &raw_gz, false);
  SPI.end();

  // --- Filter & Convert ---
  // Accel
  float ax_phys = raw_ax * ACCEL_SCALE;
  float ay_phys = raw_ay * ACCEL_SCALE;
  float az_phys = raw_az * ACCEL_SCALE;

  f_ax = (alpha * ax_phys) + ((1.0 - alpha) * f_ax);
  f_ay = (alpha * ay_phys) + ((1.0 - alpha) * f_ay);
  f_az = (alpha * az_phys) + ((1.0 - alpha) * f_az);

  // Gyro
  float gx_phys = raw_gx * GYRO_SCALE;
  float gy_phys = raw_gy * GYRO_SCALE;
  float gz_phys = raw_gz * GYRO_SCALE;

  f_gx = (alpha * gx_phys) + ((1.0 - alpha) * f_gx);
  f_gy = (alpha * gy_phys) + ((1.0 - alpha) * f_gy);
  f_gz = (alpha * gz_phys) + ((1.0 - alpha) * f_gz);

  // --- Send Data ---
  // Using write is slightly faster than print for raw bytes, 
  // but for compatibility with your python parser, we keep text.
  Serial.print(f_ax, 2); Serial.print(",");
  Serial.print(f_ay, 2); Serial.print(",");
  Serial.print(f_az, 2); Serial.print(",");
  Serial.print(f_gx, 2); Serial.print(",");
  Serial.print(f_gy, 2); Serial.print(",");
  Serial.println(f_gz, 2); // 2 decimal places is enough
}

// Helper to write to registers
void writeRegister(int csPin, byte reg, byte val, bool isAccel) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  digitalWrite(csPin, LOW); 
  SPI.transfer(reg & 0x7F); // Write flag (bit 7 = 0)
  SPI.transfer(val);
  digitalWrite(csPin, HIGH); 
  SPI.endTransaction();
}

void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel) {
  byte data[6]; 
  
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0)); // Increased SPI speed to 2MHz
  digitalWrite(csPin, LOW); 
  SPI.transfer(startReg | 0x80); // Read flag
  for (int i = 0; i < 6; i++) {
    data[i] = SPI.transfer(0x00);
  }
  digitalWrite(csPin, HIGH); 
  SPI.endTransaction();

  if (isAccel) {
      // BMX055 Accel is 12-bit left aligned in 16-bit container
      *x = (int16_t)((data[1] << 8) | data[0]) >> 4; 
      *y = (int16_t)((data[3] << 8) | data[2]) >> 4;
      *z = (int16_t)((data[5] << 8) | data[4]) >> 4;
  } else {
      *x = (int16_t)((data[1] << 8) | data[0]);
      *y = (int16_t)((data[3] << 8) | data[2]);
      *z = (int16_t)((data[5] << 8) | data[4]);
  }
}