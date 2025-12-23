/*
 * Project: Conductors Wand (Smart Baton) - BPM & Pattern Recognition
 * Component: 6-Axis IMU Data Acquisition + Real-Time Beat Detection
 * Platform: ESP32 with BMX055 via SPI
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
#define ACCEL_DATA_START_REG 0x02 
#define GYRO_DATA_START_REG  0x02

// --- Conversion Constants ---
const float ACCEL_SCALE = 9.81 / 1024.0; 
const float GYRO_SCALE = 1.0 / 16.4;

// Filter Variables
const float alpha = 0.15;
float f_ax = 0, f_ay = 0, f_az = 0;
float f_gx = 0, f_gy = 0, f_gz = 0;

// ==========================================
// BEAT DETECTION VARIABLES
// ==========================================
float beat_threshold = 15.0; 
float min_threshold = 12.5;  
float decay_rate = 0.99;     

unsigned long last_beat_time = 0;
unsigned long last_loop_time = 0;
const int MIN_BEAT_INTERVAL = 250; 
const int MAX_BEAT_INTERVAL = 2000; 

// --- NEW: BPM TIMEOUT ---
const unsigned long BPM_TIMEOUT = 3000; // 3 seconds = 20 BPM. If slower, we assume STOP.
// ------------------------

float instant_bpm = 0;
float smoothed_bpm = 60; // Start at 60, but timeout will clear it quickly
float bpm_alpha = 0.2; 

// Interval Averaging
const int NUM_BEATS_AVG = 4;           
unsigned long beat_intervals[NUM_BEATS_AVG]; 
int beat_idx = 0;                      

// Printing
unsigned long last_print_time = 0;
const int PRINT_INTERVAL = 100;

// Function Prototypes
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void detectBeat(float ax, float ay, float az);

void setup() {
  Serial.begin(115200); // Make sure your Python uses 115200 too!
  while(!Serial); 
  
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  
  digitalWrite(CS_MAG, HIGH); 
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100); 
  Serial.println("Smart Baton Ready.");
}

void loop() {
  if (micros() - last_loop_time < 20000) return;
  last_loop_time = micros();

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  SPI.end(); 
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  readSensor(CS_ACCEL, ACCEL_DATA_START_REG, &raw_ax, &raw_ay, &raw_az, true);
  
  SPI.end();
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  readSensor(CS_GYRO, GYRO_DATA_START_REG, &raw_gx, &raw_gy, &raw_gz, false);

  float ax_phys = raw_ax * ACCEL_SCALE;
  float ay_phys = raw_ay * ACCEL_SCALE;
  float az_phys = raw_az * ACCEL_SCALE;

  f_ax = (alpha * ax_phys) + ((1.0 - alpha) * f_ax);
  f_ay = (alpha * ay_phys) + ((1.0 - alpha) * f_ay);
  f_az = (alpha * az_phys) + ((1.0 - alpha) * f_az);

  detectBeat(f_ax, f_ay, f_az);

  // --- NEW: TIMEOUT CHECK ---
  // If we haven't seen a beat in 3 seconds, force 0
  if (millis() - last_beat_time > BPM_TIMEOUT) {
      smoothed_bpm = 0;
      instant_bpm = 0;
      
      // Optional: Reset buffer so next start is clean
      // for(int i=0; i<NUM_BEATS_AVG; i++) beat_intervals[i] = 0; 
  }

  // Continuous Printing
  if (millis() - last_print_time > PRINT_INTERVAL) {
      Serial.print("BPM: ");
      Serial.println(smoothed_bpm);
      last_print_time = millis();
  }
}

void detectBeat(float ax, float ay, float az) {
  float magnitude = sqrt(ax*ax + ay*ay + az*az);

  if (magnitude > beat_threshold) {
    unsigned long now = millis();
    unsigned long interval = now - last_beat_time;

    if (interval > MIN_BEAT_INTERVAL) {
      last_beat_time = now;

      if (interval < MAX_BEAT_INTERVAL) {
        
        // Circular Buffer Logic
        beat_intervals[beat_idx] = interval;  
        beat_idx = (beat_idx + 1) % NUM_BEATS_AVG;

        unsigned long sum = 0;
        int valid_count = 0;
        for (int i = 0; i < NUM_BEATS_AVG; i++) {
          if (beat_intervals[i] > 0) {
             sum += beat_intervals[i];
             valid_count++;
          }
        }
        
        float avg_interval = 0;
        if (valid_count > 0) avg_interval = (float)sum / valid_count;

        float raw_bpm_float = 0;
        if (avg_interval > 0) raw_bpm_float = 60000.0 / avg_interval;

        // If we were at 0, jump straight to the new BPM (don't smooth from 0)
        if (smoothed_bpm == 0) {
            smoothed_bpm = raw_bpm_float; 
        } else {
            float smooth_float = (bpm_alpha * raw_bpm_float) + ((1.0 - bpm_alpha) * smoothed_bpm);
            smoothed_bpm = (int)round(smooth_float);
        }
        
        instant_bpm = (int)round(raw_bpm_float);
      }
      beat_threshold = magnitude * 0.7; 
    }
  }

  beat_threshold *= decay_rate;
  if (beat_threshold < min_threshold) beat_threshold = min_threshold;
}

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