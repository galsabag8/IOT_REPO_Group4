#include <SPI.h>

// --- הגדרות חיווט ---
#define SPI_CLK   18
#define SPI_MOSI  23

// פיני קריאת נתונים
#define MISO_ACCEL 19  // תאוצה
#define MISO_GYRO  4   // ג'יירו (המגנט גם פה, אבל נתעלם ממנו)

// פיני ניהול (Chip Select)
#define CS_ACCEL  5
#define CS_GYRO   13
#define CS_MAG    14   // נשתמש בו רק כדי לנעול את המגנט

// כתובות
#define ACCEL_DATA 0x02 
#define GYRO_DATA  0x02

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
  pinMode(CS_ACCEL, OUTPUT);
  pinMode(CS_GYRO, OUTPUT);
  pinMode(CS_MAG, OUTPUT);
  
  // --- נטרול המגנטומטר (חשוב מאוד!) ---
  // אנחנו נועלים אותו על HIGH כדי שלא יפריע לג'יירו בקו המשותף
  digitalWrite(CS_MAG, HIGH); 
  
  // אתחול התחלתי לאחרים
  digitalWrite(CS_ACCEL, HIGH);
  digitalWrite(CS_GYRO, HIGH);
  
  delay(100);
  // הג'יירו והתאוצה לא דורשים פקודות אתחול מיוחדות ב-BMX055,
  // הם מתעוררים במצב פעיל.
}

void loop() {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;

  // --- 1. קריאת תאוצה (פין 19) ---
  SPI.end(); 
  SPI.begin(SPI_CLK, MISO_ACCEL, SPI_MOSI, CS_ACCEL);
  readSensor(CS_ACCEL, ACCEL_DATA, &ax, &ay, &az, true); // true = shift bits for accel
  
  // --- 2. קריאת ג'יירו (פין 4) ---
  // עכשיו שהמגנט מנוטרל, הג'יירו אמור לעבוד חלק
  SPI.end();
  SPI.begin(SPI_CLK, MISO_GYRO, SPI_MOSI, CS_GYRO);
  readSensor(CS_GYRO, GYRO_DATA, &gx, &gy, &gz, false); // false = no shift for gyro

  // --- 3. פלט לגרף ---
  // נשלח 0 במגנט כדי שהפורמט יישמר, אבל לא נבזבז זמן על קריאה
  
  Serial.print("AX:"); Serial.print(ax);
  Serial.print(",AY:"); Serial.print(ay);
  Serial.print(",AZ:"); Serial.print(az);
  
  Serial.print(",GX:"); Serial.print(gx);
  Serial.print(",GY:"); Serial.print(gy);
  Serial.print(",GZ:"); Serial.println(gz);

  // קצב דגימה מהיר (כ-50 הרץ) לזיהוי תנועה בזמן אמת
  delay(20); 
}

// --- פונקציה אחידה לקריאה ---
void readSensor(int csPin, byte startReg, int16_t *x, int16_t *y, int16_t *z, bool isAccel) {
  byte data[6];
  
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); // חזרנו ל-1MHz
  digitalWrite(csPin, LOW);
  SPI.transfer(startReg | 0x80); 
  for (int i = 0; i < 6; i++) {
    data[i] = SPI.transfer(0x00);
  }
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();

  // המרות
  if (isAccel) {
      // תאוצה דורשת הזזה של 4 ביטים
      *x = (int16_t)((data[1] << 8) | data[0]) >> 4; 
      *y = (int16_t)((data[3] << 8) | data[2]) >> 4;
      *z = (int16_t)((data[5] << 8) | data[4]) >> 4;
  } else {
      // ג'יירו הוא מספר שלם רגיל
      *x = (int16_t)((data[1] << 8) | data[0]);
      *y = (int16_t)((data[3] << 8) | data[2]);
      *z = (int16_t)((data[5] << 8) | data[4]);
  }
}