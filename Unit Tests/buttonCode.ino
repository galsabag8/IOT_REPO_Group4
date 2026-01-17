/*
 * Button Toggle Logic (Pin-to-Pin Configuration)
 * * Hardware Connection:
 * - Button is bridging GPIO 2 and GPIO 15.
 * - NO connection to GND or VCC.
 * * Logic:
 * - D2 is set to OUTPUT HIGH (acting as 3.3V source).
 * - D15 is set to INPUT_PULLDOWN (reads LOW by default).
 * - When button is pressed, D15 receives 3.3V from D2 and reads HIGH.
 */

const int SOURCE_PIN = 2;    // Acts as VCC
const int SENSING_PIN = 15;  // Acts as Input

// Variables for logic
bool myParameter = false;      // The parameter to toggle
int buttonState;              // Current reading
int lastButtonState = LOW;    // Previous reading (Default LOW because of Pull-Down)

// Debouncing variables
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

void setup() {
  Serial.begin(921600);
  Serial.println("System Initialized - Pin to Pin Mode");

  // Configure D2 as a power source
  pinMode(SOURCE_PIN, OUTPUT);
  digitalWrite(SOURCE_PIN, HIGH); // Always output 3.3V

  // Configure D15 as input with internal resistor to GND
  // If the button is NOT pressed, this pin will read LOW (0).
  pinMode(SENSING_PIN, INPUT_PULLDOWN);

  Serial.print("Initial Parameter State: ");
  Serial.println(myParameter ? "TRUE" : "FALSE");
}

void loop() {
  // Read the state of the sensing pin
  // Unlike previous code, here HIGH means PRESSED (because D2 pushes HIGH)
  int reading = digitalRead(SENSING_PIN);

  // Check for state change (noise or press)
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // If the state is stable
    if (reading != buttonState) {
      buttonState = reading;

      // Logic trigger: We look for HIGH state (Voltage received from D2)
      if (buttonState == HIGH) {
        myParameter = !myParameter; // Toggle parameter

        Serial.print("Button Pressed! Parameter changed to: ");
        Serial.println(myParameter ? "TRUE" : "FALSE");
      }
    }
  }

  lastButtonState = reading;
}