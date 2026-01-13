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
#include "config.h"

// --- Calibration Variables ---
bool gravity_calibrated = false;
int calibration_count = 0;
float gravity_accumulator = 0;
float gravity_mag = 9.80665f; // Default, will be updated

// --- Smoothing Constants ---
float accel_mag_history[SMOOTH_WINDOW] = {0};
int smooth_idx = 0;

// --- Global Variable for Time Signature ---
int TIME_SIGNATURE = 4; // Default to 4/4. Can be changed via Serial command later.
int next_expected_beat = 1;   
int warmup_beats_remaining = 4; //Number of beats remaining to complete warmup

// --- Beat Detection Variables ---
unsigned long last_beat_time = 0;


float smoothed_bpm = 60;

unsigned long beat_intervals[NUM_BEATS_AVG]; 
int beat_idx = 0;                      

unsigned long last_print_time = 0;

// Timing
unsigned long last_loop_time = 0;
float dt = 0.01f;
// --- Smoothed Accel Magnitude Variables ---
const int RECENT_HISTORY_SIZE = 20;
float recent_magnitudes[RECENT_HISTORY_SIZE] = {0};
int recent_idx = 0;

// Button LOGIC
bool buttonStatus = false;      // The parameter to toggle
int buttonReader;              // Current reading
int lastbuttonReader = LOW;    // Previous reading (Default LOW because of Pull-Down)
bool buttonEnabled = false;    // To track if button logic is active
bool firstButtonPress = true;  // Track if this is the first press

struct IMUData {
  float ax_phys, ay_phys, az_phys; // Physical acceleration (g)
  float gx_phys, gy_phys, gz_phys; // Physical gyroscope (deg/s)
  float gx_rad, gy_rad, gz_rad;    // Gyroscope in radians (rad/s)
};

IMUData currentIMUData;

struct VisualizationData {
  float x_vis, y_vis, z_vis;
  float screen_x, screen_y, screen_z;
};

VisualizationData currentVisData;

struct AccelerationData {
  float magnitude;
  float gyro_mag;
};

AccelerationData currentAccelData;

// --- State Definitions + variables ---
enum SystemState {
    STATE_IDLE,
    STATE_WARMUP,
    STATE_PLAYBACK
};

SystemState currentState = STATE_IDLE;
bool isGUICalibrationInProgress = false;
bool isFileLoaded = false;
bool sendConnectionStatus = true;
int connectionStatusTimeCounter = 0;


// --- Prototypes ---
void writeRegister(int csPin, byte reg, byte val, bool isAccel);
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel);
void updateBPM();
void detectBeat(float x, float z, float gyro_mag, float smoothed_mag);
bool handleMetric2(float x, float z, float magnitude, float gyro_mag);
bool handleMetric3(float x, float z, float magnitude, float gyro_mag);
bool handleMetric4(float x, float z, float magnitude, float gyro_mag);

void setup() {
  // 1. High Speed Serial (Matches Friend's code)
  Serial.begin(921600); 
  while(!Serial); 
  
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);

  //Setup Button Pins
  // Configure D2 as a power source
  pinMode(SOURCE_PIN, OUTPUT);
  // Configure D15 as input with internal resistor to GND, If the button is NOT pressed, this pin will read LOW (0).
  pinMode(SENSING_PIN, INPUT_PULLDOWN);
  
  digitalWrite(CS_MAG, HIGH); 
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  digitalWrite(SOURCE_PIN, HIGH);
  
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
  // Always check for Serial commands regardless of state
  handleSerialCommands();
  
  // Check Button State
  checkButton();

  // 100Hz Loop
  unsigned long current_time = micros();
  if (current_time - last_loop_time < LOOP_DELAY_US) return;
  last_loop_time = micros();
  last_loop_time = current_time;

  readIMUData();

  if (!gravity_calibrated) {
    performGravityCalibration();
    return; // Wait until calibration is finished
  }

  // --- Run Embedded Algorithm (from MadgwickAlgo.cpp) ---
  updateOrientationAndLinearAccel();

  updateVisData();

  updateAndSmoothAccelMagnitude();

  //--- OUTPUT 1: Visualization Data (CSV) ---
  printXYZOutput();

  switch(currentState) {
    case STATE_IDLE:
      if (sendConnectionStatus) {
        connectionStatusTimeCounter++;
        if (connectionStatusTimeCounter >= 100) {
          connectionStatusTimeCounter = 0;
          Serial.println("STATUS: CONNECTED");
        }
      }
      return;
    case STATE_WARMUP:
      detectBeat(currentVisData.screen_x, 
                currentVisData.screen_z, 
                currentAccelData.gyro_mag, 
                currentAccelData.magnitude);
      return;
    case STATE_PLAYBACK:
      detectBeat(currentVisData.screen_x, 
                currentVisData.screen_z, 
                currentAccelData.gyro_mag, 
                currentAccelData.magnitude);
      printBPMOutput();
      break;
    default:
      break;
  }
}

// --- Task: Serial Command Handling ---
void handleSerialCommands() {
  // --- 1. Check for incoming Command (Non-blocking) ---
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim(); 

    if (input.equals("RESET")) {
      //If we got reset command from the GUI, reset the system state and variables
      currentState = STATE_IDLE;
      isGUICalibrationInProgress = false;
      isFileLoaded = false;
      return;
    }
    if (input.equals("WAND STATUS ACK")) {
      sendConnectionStatus = false;
      return;
    }
    if (input.equals("CALIBRATION STARTED")) {
      isGUICalibrationInProgress = true;
    }
    if (input.equals("CALIBRATION FINISHED")) {
      isGUICalibrationInProgress = false;
    }
    // Protocol: "SET_SIG:3"
    if (input.startsWith("WEIGHT:")) {
      int new_sig = input.substring(7).toInt();
      if (new_sig >= 2 && new_sig <= 4) {
        TIME_SIGNATURE = new_sig;
        next_expected_beat = 1; // Reset beat counter
        isFileLoaded = true;
        warmup_beats_remaining = new_sig;
      }
    }
  }
}

void readIMUData() {
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
  currentIMUData.ax_phys = raw_ax * ACCEL_SCALE;
  currentIMUData.ay_phys = raw_ay * ACCEL_SCALE;
  currentIMUData.az_phys = raw_az * ACCEL_SCALE;
  currentIMUData.gx_phys = raw_gx * GYRO_SCALE;
  currentIMUData.gy_phys = raw_gy * GYRO_SCALE;
  currentIMUData.gz_phys = raw_gz * GYRO_SCALE;
  currentIMUData.gx_rad = currentIMUData.gx_phys * (M_PI / 180.0f);
  currentIMUData.gy_rad = currentIMUData.gy_phys * (M_PI / 180.0f);
  currentIMUData.gz_rad = currentIMUData.gz_phys * (M_PI / 180.0f);
}

void performGravityCalibration() {
  float current_mag = sqrt(currentIMUData.ax_phys*currentIMUData.ax_phys 
                          + currentIMUData.ay_phys*currentIMUData.ay_phys 
                          + currentIMUData.az_phys*currentIMUData.az_phys);
      gravity_accumulator += current_mag;
      calibration_count++;
      
      if (calibration_count >= 100) {
          gravity_mag = gravity_accumulator / 100.0f;
          gravity_calibrated = true;
      }
}

void updateVisData() {
  // 5. Visualization Data [cite: 36]
  currentVisData.x_vis = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
  currentVisData.y_vis = 2.0f * (q1 * q2 + q0 * q3);
  currentVisData.z_vis = 2.0f * (q1 * q3 - q0 * q2);

  currentVisData.screen_x = -currentVisData.y_vis;
  currentVisData.screen_y = currentVisData.x_vis;
  currentVisData.screen_z = -currentVisData.z_vis;
}

void updateOrientationAndLinearAccel() {
  MadgwickUpdate(currentIMUData.gx_rad, 
                  currentIMUData.gy_rad, 
                  currentIMUData.gz_rad, 
                  currentIMUData.ax_phys, 
                  currentIMUData.ay_phys, 
                  currentIMUData.az_phys,  
                  dt);

  // 3. Gravity Vector from Quaternions [cite: 33-35]
  float gravity_x = 2.0f * (q1 * q3 - q0 * q2);
  float gravity_y = 2.0f * (q0 * q1 + q2 * q3);
  float gravity_z = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

  // 4. Linear Acceleration (Subtract measured gravity)
  currentIMUData.ax_phys = currentIMUData.ax_phys - (gravity_x * gravity_mag);
  currentIMUData.ay_phys = currentIMUData.ay_phys - (gravity_y * gravity_mag);
  currentIMUData.az_phys = currentIMUData.az_phys - (gravity_z * gravity_mag);
}

void updateAndSmoothAccelMagnitude() {
  // Simple Smoothing for Accel Magnitude
  float raw_mag = sqrt(currentIMUData.ax_phys*currentIMUData.ax_phys 
                    + currentIMUData.ay_phys*currentIMUData.ay_phys 
                    + currentIMUData.az_phys*currentIMUData.az_phys);

   // Store in recent history
  recent_magnitudes[recent_idx] = raw_mag;
  recent_idx = (recent_idx + 1) % RECENT_HISTORY_SIZE;

  accel_mag_history[smooth_idx] = raw_mag;
  smooth_idx = (smooth_idx + 1) % SMOOTH_WINDOW;
  float smooth_mag = 0;
  for(int i=0; i<SMOOTH_WINDOW; i++) smooth_mag += accel_mag_history[i];
  smooth_mag /= SMOOTH_WINDOW;

  // --- CALIBRATION METRICS ---
  currentAccelData.magnitude = smooth_mag;
  currentAccelData.gyro_mag = sqrt(currentIMUData.gx_rad*currentIMUData.gx_rad 
                              + currentIMUData.gy_rad*currentIMUData.gy_rad 
                              + currentIMUData.gz_rad*currentIMUData.gz_rad);
}

void printXYZOutput() {
  //Format: DATA,x,y,z
  Serial.print("DATA,");
  Serial.print(currentVisData.screen_x, 4); 
  Serial.print(",");
  Serial.print(currentVisData.screen_y, 4); 
  Serial.print(",");
  Serial.println(currentVisData.screen_z, 4);
}

void printBPMOutput() {
  // --- Timeout Check (Force 0 BPM if idle) ---
  if (millis() - last_beat_time > BPM_TIMEOUT) {
      smoothed_bpm = 0;
      next_expected_beat = 1;
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
          float smooth_float = (BPM_SMOOTHING_ALPHA * raw_bpm_float) + ((1.0 - BPM_SMOOTHING_ALPHA) * smoothed_bpm);
          smoothed_bpm = (int)round(smooth_float);
      }
    }
}
// --- LOGIC: DETECT BEAT & BPM ---
void detectBeat(float x, float z, float gyro_mag, float magnitude) {
  bool beatConfirmed = false;
  switch (TIME_SIGNATURE) {
    case 2:
      beatConfirmed = handleMetric2(x, z, magnitude, gyro_mag);
      break;
    case 3:
      beatConfirmed = handleMetric3(x, z, magnitude, gyro_mag);
      break;
    case 4:
      beatConfirmed = handleMetric4(x, z,magnitude, gyro_mag);
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

          // --- NEW: Send Trigger to Python ---
          Serial.println("BEAT_TRIG");
          Serial.print("BEAT: "); Serial.println(next_expected_beat - 1 == 0 ? TIME_SIGNATURE : next_expected_beat - 1);
      }

      //Update warmup counter
      if (currentState == STATE_WARMUP) {
        warmup_beats_remaining--;
        if (warmup_beats_remaining == 0) {
          currentState = STATE_PLAYBACK;
        }
      }
  }
}
// --- Logic for 2/4 Time Signature ---
bool handleMetric2(float x, float z, float magnitude, float gyro_mag) {
  accel_tracker.update(magnitude);

  ValleyInfo valley_info = checkForValley(z, x, gyro_mag, accel_tracker.getJerkMagnitude());
  if (!valley_info.detected) return false;

  // --- IF WE ARE HERE, A VALLEY WAS JUST DETECTED ---

  if (valley_info.peak_jerk_during_descent < RESTING_ACCEL_CHANGE_THRESHOLD) {
    if (DEBUG_MODE){
      Serial.print("peak_jerk_during_descent: "); Serial.print(valley_info.peak_jerk_during_descent); Serial.print(" < threshold"); Serial.println(RESTING_ACCEL_CHANGE_THRESHOLD);
    }
    return false;
  }

  // Rule A: The wand must not be resting
  if (magnitude < RESTING_MAGNITUDE) {
    if(DEBUG_MODE){
      Serial.print("magnitude: "); Serial.println(magnitude);  Serial.print(" < Resting_Magnitude"); Serial.println(RESTING_MAGNITUDE);
    }
    return false;
  }



  // Rule B: Check Beat Expectations
  switch (next_expected_beat) {
    case 1:
        if (checkBeat1LogicWithWeight2(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    case 2:
        if (checkBeat2LogicWithWeight2(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    default:
      break;
  }
  // if we get here, we detect a beat but it didn't match expectations, so returing to beat1
  next_expected_beat = 1;

  return false;
}

// --- Logic for 3/4 Time Signature ---
// Pattern: 1 (Down), 2 (Out/Right), 3 (Up)
bool handleMetric3(float x, float z, float magnitude, float gyro_mag) {
  accel_tracker.update(magnitude);

  ValleyInfo valley_info = checkForValley(z, x, gyro_mag, accel_tracker.getJerkMagnitude());
  if (!valley_info.detected) return false;

  // --- IF WE ARE HERE, A VALLEY WAS JUST DETECTED ---

  if (valley_info.peak_jerk_during_descent < RESTING_ACCEL_CHANGE_THRESHOLD) {
    if (DEBUG_MODE){
      Serial.print("peak_jerk_during_descent: "); Serial.print(valley_info.peak_jerk_during_descent); Serial.print(" < threshold"); Serial.println(RESTING_ACCEL_CHANGE_THRESHOLD);
    }
    return false;
  }
  // Rule A: The wand must not be resting
  if (magnitude < RESTING_MAGNITUDE) {
    if(DEBUG_MODE){
      Serial.print("magnitude: "); Serial.println(magnitude);  Serial.print(" < Resting_Magnitude"); Serial.println(RESTING_MAGNITUDE);
    }
    return false;
  }
  // Rule B: Check Beat Expectations
  switch (next_expected_beat) {
    case 1:
        if (checkBeat1LogicWithWeight3(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    case 2:
        if (checkBeat2LogicWithWeight3(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    case 3:
        if (checkBeat3LogicWithWeight3(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    default:
      break;
  }
  // if we get here, we detect a beat but it didn't match expectations, so returing to beat1
  next_expected_beat = 1;

  return false;
}

// --- Logic for 4/4 Time Signature ---
// Pattern: 1 (Down), 2 (In/Left), 3 (Out/Right), 4 (Up)
bool handleMetric4(float x, float z, float magnitude, float gyro_mag) {
  accel_tracker.update(magnitude);

  ValleyInfo valley_info = checkForValley(z, x, gyro_mag, accel_tracker.getJerkMagnitude());
  if (!valley_info.detected) return false;

  // --- IF WE ARE HERE, A VALLEY WAS JUST DETECTED ---

  if (valley_info.peak_jerk_during_descent < RESTING_ACCEL_CHANGE_THRESHOLD) {
    if (DEBUG_MODE){
      Serial.print("peak_jerk_during_descent: "); Serial.print(valley_info.peak_jerk_during_descent); Serial.print(" < threshold"); Serial.println(RESTING_ACCEL_CHANGE_THRESHOLD);
    }
    return false;
  }
  // Rule A: The wand must not be resting
  if (magnitude < RESTING_MAGNITUDE) {
    if(DEBUG_MODE){
      Serial.print("magnitude: "); Serial.println(magnitude);  Serial.print(" < Resting_Magnitude"); Serial.println(RESTING_MAGNITUDE);
    }
    return false;
  }
  // Rule B: Check Beat Expectations
  switch (next_expected_beat) {
    case 1:
        if (checkBeat1LogicWithWeight4(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    case 2:
        if (checkBeat2LogicWithWeight4(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    case 3:
        if (checkBeat3LogicWithWeight4(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    case 4:
        if (checkBeat4LogicWithWeight4(magnitude, z, x, next_expected_beat, valley_info)) return true;
        break;
    default:
      break;
  }
  // if we get here, we detect a beat but it didn't match expectations, so returing to beat1
  next_expected_beat = 1;
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

unsigned long lastDebounceTime = 0;

/*
 * Function: checkButton
 * ---------------------
 * Handles the reading of the hardware button, debouncing,
 * and toggling of the global 'buttonStatus' variable.
 */
void checkButton() {
  // Read the state of the sensing pin
  // HIGH means PRESSED (because D2 pushes HIGH to D15)
  // if(isGUICalibrationInProgress || !isFileLoaded || currentState == STATE_IDLE) return; // currentState will never change unless button is pressed
  if(isGUICalibrationInProgress || !isFileLoaded) return; // Skip if button logic is not enabled
  int reading = digitalRead(SENSING_PIN);

  // Check if the reading is different from the last loop (noise or press)
  if (reading != lastbuttonReader) {
    lastDebounceTime = millis(); // Reset the timer
  }

  // Check if enough time has passed to consider this a stable reading
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {

    // If the stable state has changed:
    if (reading != buttonReader) {
      buttonReader = reading;

      // Logic trigger: Action happens only when button goes HIGH
      if (buttonReader == HIGH) {
        if (firstButtonPress) {
          // First press = Start playing
          buttonStatus = true;
          Serial.println("Button: PLAY");
          firstButtonPress = false;
          // Reset beat detection state before starting
          resetBeatDetectionState();
          next_expected_beat = 1;
          last_beat_time = millis();  // Prevent immediate beat detection
          currentState = STATE_WARMUP;
        } else {
          // Second press = Stop
          buttonStatus = false;
          Serial.println("Button: STOP");
          buttonEnabled = false; // Disable further button presses until re-enabled
          firstButtonPress = true;  // Reset for next session
          currentState = STATE_IDLE;
        }
      }
    }
  }

  // Save the reading for the next loop iteration
  lastbuttonReader = reading;
}