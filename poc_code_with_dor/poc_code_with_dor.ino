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

// --- Constants ---
// Using Friend's ranges (+/- 4g, 2000dps) for better tracking
const float ACCEL_SCALE = 0.009807; 
const float GYRO_SCALE = 1.0 / 16.4;

// --- Dual Filters ---
// Filter 1: VISUALIZATION (Responsive, alpha 0.8)
const float alpha_vis = 0.8;
float v_ax = 0, v_ay = 0, v_az = 0;
float v_gx = 0, v_gy = 0, v_gz = 0;

// Filter 2: BEAT DETECTION (Smooth, alpha 0.15)
const float alpha_beat = 0.15;
float b_ax = 0, b_ay = 0, b_az = 0;

// --- Beat Detection Variables (From Your Code) ---
float beat_threshold = 15.0; 
float min_threshold = 12.5;  
float decay_rate = 0.99;
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
void detectBeat(float ax, float ay, float az);

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
  // Serial.println("Hybrid System Ready."); // Commented out to keep data stream clean
}

void loop() {
  // 100Hz Loop
  if (micros() - last_loop_time < LOOP_DELAY_US) return;
  last_loop_time = micros();

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

  // --- 1. VISUALIZATION FILTER (Responsive) ---
  v_ax = (alpha_vis * ax_phys) + ((1.0 - alpha_vis) * v_ax);
  v_ay = (alpha_vis * ay_phys) + ((1.0 - alpha_vis) * v_ay);
  v_az = (alpha_vis * az_phys) + ((1.0 - alpha_vis) * v_az);
  v_gx = (alpha_vis * gx_phys) + ((1.0 - alpha_vis) * v_gx);
  v_gy = (alpha_vis * gy_phys) + ((1.0 - alpha_vis) * v_gy);
  v_gz = (alpha_vis * gz_phys) + ((1.0 - alpha_vis) * v_gz);

  // --- 2. BEAT DETECTION FILTER (Smooth) ---
  b_ax = (alpha_beat * ax_phys) + ((1.0 - alpha_beat) * b_ax);
  b_ay = (alpha_beat * ay_phys) + ((1.0 - alpha_beat) * b_ay);
  b_az = (alpha_beat * az_phys) + ((1.0 - alpha_beat) * b_az);

  // --- OUTPUT 1: Visualization Data (CSV) ---
  // Format: DATA,ax,ay,az,gx,gy,gz
  Serial.print("DATA,");
  Serial.print(v_ax, 2); Serial.print(",");
  Serial.print(v_ay, 2); Serial.print(",");
  Serial.print(v_az, 2); Serial.print(",");
  Serial.print(v_gx, 2); Serial.print(",");
  Serial.print(v_gy, 2); Serial.print(",");
  Serial.println(v_gz, 2);

  // --- OUTPUT 2: Beat Detection Logic ---
  detectBeat(b_ax, b_ay, b_az);

  // --- Timeout Check (Force 0 BPM if idle) ---
  if (millis() - last_beat_time > BPM_TIMEOUT) {
      smoothed_bpm = 0;
  }

  // --- Send BPM Update ---
  // We check this every loop, but print intermittently or on change
  if (millis() - last_print_time > PRINT_INTERVAL) {
      // Your Python app listens for "BPM: "
      Serial.print("BPM: ");
      Serial.println((int)smoothed_bpm); 
      last_print_time = millis();
  }
}

// --- LOGIC: DETECT BEAT & BPM ---
void detectBeat(float ax, float ay, float az) {
  float magnitude = sqrt(ax*ax + ay*ay + az*az);

  if (magnitude > beat_threshold) {
    unsigned long now = millis();
    unsigned long interval = now - last_beat_time;

    if (interval > MIN_BEAT_INTERVAL) {
      last_beat_time = now;

      if (interval < MAX_BEAT_INTERVAL) {
        // Average Logic
        beat_intervals[beat_idx] = interval;  
        beat_idx = (beat_idx + 1) % NUM_BEATS_AVG;

        unsigned long sum = 0;
        int valid_count = 0;
        for (int i = 0; i < NUM_BEATS_AVG; i++) {
          if (beat_intervals[i] > 0) { sum += beat_intervals[i]; valid_count++; }
        }
        
        float avg_interval = 0;
        if (valid_count > 0) avg_interval = (float)sum / valid_count;

        float raw_bpm_float = 0;
        if (avg_interval > 0) raw_bpm_float = 60000.0 / avg_interval;

        if (smoothed_bpm == 0) {
            smoothed_bpm = raw_bpm_float; 
        } else {
            float smooth_float = (bpm_alpha * raw_bpm_float) + ((1.0 - bpm_alpha) * smoothed_bpm);
            smoothed_bpm = (int)round(smooth_float);
        }
      }
      beat_threshold = magnitude * 0.7; 
    }
  }

  beat_threshold *= decay_rate;
  if (beat_threshold < min_threshold) beat_threshold = min_threshold;
}

// --- HELPER: WRITE REGISTER ---
void writeRegister(int csPin, byte reg, byte val, bool isAccel) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  digitalWrite(csPin, LOW); 
  SPI.transfer(reg & 0x7F); // Write flag
  SPI.transfer(val);
  digitalWrite(csPin, HIGH); 
  SPI.endTransaction();
}

// --- HELPER: READ SENSOR ---
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel) {
  byte data[6]; 
  // Increased SPI speed to 2MHz for faster reads
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