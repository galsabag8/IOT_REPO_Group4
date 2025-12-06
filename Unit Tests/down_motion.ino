/*
 * Smart Baton - FINAL WORKING FIRMWARE
 * ------------------------------------
 * Fixes: Restored SPI Pin Switching for Accel (Pin 19) vs Gyro (Pin 4).
 * Compatible with saver.py
 */

#include <SPI.h>

// ================================================================
// ===               HARDWARE & PIN DEFINITIONS                 ===
// ================================================================

#define SPI_CLK    18
#define SPI_MOSI   23
#define MISO_ACCEL 19 
#define MISO_GYRO  4 

#define CS_ACCEL   5
#define CS_GYRO    17
#define CS_MAG     14 

#define ACCEL_DATA_START_REG 0x02 
#define GYRO_DATA_START_REG  0x02

const float ACCEL_SCALE = 9.81 / 1024.0; 
const float GYRO_SCALE  = 1.0 / 16.4;    

// ================================================================
// ===               ALGORITHM CONFIGURATION                    ===
// ================================================================

const float ALPHA = 0.2;            
const int   SAMPLE_DELAY_MS = 10;   

const float THRESH_DOWN = 3.5;      
const float THRESH_RESET = 1.0;     
const int   MIN_BEAT_INTERVAL = 250;

// ================================================================
// ===                   DATA STRUCTURES                        ===
// ================================================================

struct SensorData {
    int16_t raw_ax, raw_ay, raw_az;
    int16_t raw_gx, raw_gy, raw_gz;
    float ax, ay, az;
    float base_ax, base_ay, base_az;
    float dev_x, dev_y;
    float magnitude;
};

struct BeatState {
    bool is_active;             
    unsigned long last_time;    
    int count;                  
    bool event_trigger;         
    bool is_down_type;          
};

SensorData imu;
BeatState beat = {false, 0, 0, false, false};

// ================================================================
// ===                      HELPER FUNCTIONS                    ===
// ================================================================

/**
 * FIXED: Initializes SPI with specific MISO pin before reading.
 * This is crucial for your specific hardware setup.
 */
void readSensorSPI(int csPin, int misoPin, byte startReg, int16_t *x, int16_t *y, int16_t *z) {
    byte data[6]; 
    
    // 1. Configure SPI for the specific sensor (Critical Step!)
    SPI.end(); // Close previous connection
    SPI.begin(SPI_CLK, misoPin, SPI_MOSI, csPin);
    
    // 2. Perform Transaction
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
    digitalWrite(csPin, LOW); 
    
    SPI.transfer(startReg | 0x80); 
    for (int i = 0; i < 6; i++) {
        data[i] = SPI.transfer(0x00);
    }
    
    digitalWrite(csPin, HIGH); 
    SPI.endTransaction();
    
    // 3. Parse Data
    *x = (int16_t)((data[1] << 8) | data[0]) >> 4; 
    *y = (int16_t)((data[3] << 8) | data[2]) >> 4;
    *z = (int16_t)((data[5] << 8) | data[4]) >> 4;
}

void calibrateSensors() {
    // Note: Python script filters out lines with "STARTING"
    Serial.println("STARTING CALIBRATION (10 Seconds)..."); 
    
    long sum_x = 0, sum_y = 0, sum_z = 0;
    long count = 0;
    unsigned long start_cal = millis();
    
    while (millis() - start_cal < 10000) {
        int16_t rx, ry, rz;
        // Pass MISO_ACCEL specifically
        readSensorSPI(CS_ACCEL, MISO_ACCEL, ACCEL_DATA_START_REG, &rx, &ry, &rz);
        
        sum_x += rx; 
        sum_y += ry; 
        sum_z += rz;
        count++;
        
        delay(2); 
    }
    
    imu.base_ax = (sum_x / (float)count) * ACCEL_SCALE;
    imu.base_ay = (sum_y / (float)count) * ACCEL_SCALE;
    imu.base_az = (sum_z / (float)count) * ACCEL_SCALE;
    
    Serial.println("CALIBRATION DONE.");
    // This header matches exactly what saver.py expects to skip or check
    Serial.println("ax,ay,az,gx,gy,gz,magnitude,beat_event,bpm,beat_count,threshold,dev_x,dev_y,is_down");
}

void updateSensorData() {
    // 1. Read Raw Data (Specifying correct MISO pins)
    readSensorSPI(CS_ACCEL, MISO_ACCEL, ACCEL_DATA_START_REG, &imu.raw_ax, &imu.raw_ay, &imu.raw_az);
    readSensorSPI(CS_GYRO,  MISO_GYRO,  GYRO_DATA_START_REG,  &imu.raw_gx, &imu.raw_gy, &imu.raw_gz);
    
    // 2. Physical Conversion
    float curr_ax = imu.raw_ax * ACCEL_SCALE;
    float curr_ay = imu.raw_ay * ACCEL_SCALE;
    float curr_az = imu.raw_az * ACCEL_SCALE;
    
    // 3. Filter
    imu.ax = (ALPHA * curr_ax) + ((1.0 - ALPHA) * imu.ax);
    imu.ay = (ALPHA * curr_ay) + ((1.0 - ALPHA) * imu.ay);
    imu.az = (ALPHA * curr_az) + ((1.0 - ALPHA) * imu.az);
    
    // 4. Magnitude & Deviation
    imu.magnitude = sqrt(imu.ax*imu.ax + imu.ay*imu.ay + imu.az*imu.az);
    imu.dev_x = imu.ax - imu.base_ax;
    imu.dev_y = imu.ay - imu.base_ay; 
}

void detectBeat() {
    beat.event_trigger = false;
    beat.is_down_type = false;

    if (beat.is_active && abs(imu.dev_y) < THRESH_RESET) {
        beat.is_active = false;
    }

    if (!beat.is_active && (millis() - beat.last_time > MIN_BEAT_INTERVAL)) {
        if (imu.dev_y > THRESH_DOWN) {
            beat.last_time = millis();
            beat.is_active = true;
            beat.count++;
            
            beat.event_trigger = true; 
            beat.is_down_type = true; 
        }
    }
}

void printCSV() {
    Serial.print(imu.ax, 2); Serial.print(",");
    Serial.print(imu.ay, 2); Serial.print(",");
    Serial.print(imu.az, 2); Serial.print(",");
    
    Serial.print(imu.raw_gx); Serial.print(",");
    Serial.print(imu.raw_gy); Serial.print(",");
    Serial.print(imu.raw_gz); Serial.print(",");
    
    Serial.print(imu.magnitude, 2); Serial.print(",");
    
    Serial.print(beat.event_trigger); Serial.print(",");
    Serial.print(0); Serial.print(","); 
    Serial.print(beat.count); Serial.print(",");
    Serial.print(THRESH_DOWN); Serial.print(","); 
    
    Serial.print(imu.dev_x, 2); Serial.print(",");
    Serial.print(imu.dev_y, 2); Serial.print(",");
    Serial.println(beat.is_down_type); 
}

// ================================================================
// ===                    MAIN SETUP & LOOP                     ===
// ================================================================

void setup() {
    Serial.begin(115200);
    while(!Serial);
    
    pinMode(CS_ACCEL, OUTPUT);
    pinMode(CS_GYRO, OUTPUT);
    pinMode(CS_MAG, OUTPUT);
    
    digitalWrite(CS_MAG, HIGH); 
    digitalWrite(CS_ACCEL, HIGH);
    digitalWrite(CS_GYRO, HIGH);
    
    delay(500);
    
    calibrateSensors();
}

void loop() {
    updateSensorData();
    detectBeat();
    printCSV();
    
    delay(SAMPLE_DELAY_MS);
}