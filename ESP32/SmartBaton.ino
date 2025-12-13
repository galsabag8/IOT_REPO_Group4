/*
 * Project: Conductors Wand (Smart Baton) - Improved v2
 * Component: 6-Axis IMU Data Acquisition + Dynamic Beat Detection
 * Platform: ESP32 with BMX055 via SPI
 * * Improvements in v2:
 * 1. Gravity Removal (High Pass Filter) - Ignores static tilt.
 * 2. Peak Detection - Triggers on the "snap" apex, preventing multi-triggers.
 * 3. Dynamic Thresholding.
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
// Stronger Filter: Lower alpha (0.15) smooths out hand jitters
const float alpha = 0.15;

// Filter Variables (Low Pass for signal smoothing)
float f_ax = 0, f_ay = 0, f_az = 0;
float f_gx = 0, f_gy = 0, f_gz = 0;

// ==========================================
// IMPROVED BEAT DETECTION VARIABLES
// ==========================================
// Thresholds (Values are lower because we removed gravity)
float beat_threshold = 4.0;  // Initial delta threshold
float min_threshold = 2.0;   // Minimum sensitivity floor
float decay_rate = 0.90;     // Slower decay for stability

// Gravity Removal Variables
float avg_magnitude = 9.81;      // Starts at approx 1G
const float gravity_alpha = 0.9; // Slow update to filter out constant gravity

// Peak Detection Logic
boolean rising = false;      // Are we currently in the rising edge of a beat?
float last_magnitude_val = 0; 

// Timing
unsigned long last_beat_time = 0;
unsigned long last_loop_time = 0;
const int MIN_BEAT_INTERVAL = 400; // Raised slightly to 300ms (Max 200 BPM) to avoid noise
const int MAX_BEAT_INTERVAL = 2000; // ms (Min 30 BPM)
const int stop_param = 2;   // For Baton Pause


// BPM Calculation
float instant_bpm = 0;
float smoothed_bpm = 60;
float bpm_alpha = 0.2; 

// ==========================================
// FUNCTION PROTOTYPES
// ==========================================
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void detectBeat(float ax, float ay, float az);

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
  Serial.println("Smart Baton v2 Ready. Waiting for motion...");
}

void loop() {
  // Maintain a fixed sample rate (approx 50Hz = 20ms)
  if (micros() - last_loop_time < 20000) {
    return;
  }
  last_loop_time = micros();

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  // --- 1. Read Raw Data (Swapping SPI pins dynamically) ---
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

  // --- 3. Low Pass Filter (Smoothing) ---
  f_ax = (alpha * ax_phys) + ((1.0 - alpha) * f_ax);
  f_ay = (alpha * ay_phys) + ((1.0 - alpha) * f_ay);
  f_az = (alpha * az_phys) + ((1.0 - alpha) * f_az);

  // --- 4. IMPROVED BEAT DETECTION ALGORITHM ---
  detectBeat(f_ax, f_ay, f_az);

  // --- 5. CHECK FOR BATON PAUSE ---
  checkStop();
}

// ==========================================
// CORE ALGORITHM: PEAK DETECTION & GRAVITY REMOVAL
// ==========================================
void detectBeat(float ax, float ay, float az) {
  // 1. Calculate Absolute Magnitude
  float raw_magnitude = sqrt(ax*ax + ay*ay + az*az);

  // 2. Remove Gravity (High Pass Filter)
  // Determine the "DC offset" (Gravity + Tilt) slowly
  avg_magnitude = (gravity_alpha * avg_magnitude) + ((1.0 - gravity_alpha) * raw_magnitude);
  
  // The Signal is the difference (The "Snap" motion)
  float dynamic_magnitude = abs(raw_magnitude - avg_magnitude);

  // 3. Peak Detection Logic
  // Check if we are currently rising above the threshold
  if (dynamic_magnitude > beat_threshold && dynamic_magnitude > min_threshold) {
     rising = true; 
  }

  // Trigger ONLY when the signal starts dropping after being high (The Peak)
  if (rising && (dynamic_magnitude < last_magnitude_val)) {
      
      unsigned long now = millis();
      unsigned long interval = now - last_beat_time;

      if (interval > MIN_BEAT_INTERVAL) {
          // --- VALID BEAT DETECTED ---
          last_beat_time = now;

          // BPM Calculation
          if (interval < MAX_BEAT_INTERVAL) {
            float raw_bpm_float = 60000.0 / interval;
            float smooth_float = (bpm_alpha * raw_bpm_float) + ((1.0 - bpm_alpha) * smoothed_bpm);
            smoothed_bpm = (int)round(smooth_float);
            instant_bpm = (int)round(raw_bpm_float);

            // Output for Python / Plotter
            Serial.print("BPM:");
            Serial.println((int)smoothed_bpm);
          }

          // Auto-Gain: Raise threshold to ignore echo/rebound
          beat_threshold = last_magnitude_val * 0.85; 
      }
      
      // Reset rising flag - we found the peak
      rising = false;
  }

  // Update history
  last_magnitude_val = dynamic_magnitude;

  // 4. Threshold Decay
  beat_threshold *= decay_rate;
  if (beat_threshold < min_threshold) {
    beat_threshold = min_threshold;
  }
}

void checkStop() {
  // חישוב הזמן המקסימלי להמתנה
  unsigned long timeout = (unsigned long)MAX_BEAT_INTERVAL * stop_param;
  
  // בדיקה: האם עבר יותר מדי זמן מאז הפעמה האחרונה?
  if (millis() - last_beat_time > timeout) {
    
    // מאפסים רק אם זה לא מאופס כבר (כדי לא להספים את ה-Serial)
    if (smoothed_bpm != 0) {
       smoothed_bpm = 0;
       instant_bpm = 0;
       float pause_bpm = -1;

       // Output for Python
        Serial.print("BPM:");
        Serial.println((int)pause_bpm);
       
       // Update Threshold for next time
       beat_threshold = 4.0; 
    }
  }
}

// ==========================================
// SPI HELPER
// ==========================================
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel) {
  byte data[6]; 
  
  // Note: settings are re-applied here, safe for loop usage
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