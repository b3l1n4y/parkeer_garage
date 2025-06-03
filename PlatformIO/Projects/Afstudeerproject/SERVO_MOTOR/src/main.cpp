#include <Arduino.h>
#include <Servo.h>

Servo servo1; // Positional servo on pin 6
Servo servo2; // Continuous servo (FS90R) on pin 7

const int SERVO1_PIN = 6;
const int SERVO2_PIN = 7;

void setup()
{
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);

  servo1.write(0);  // Closed position
  servo2.write(90); // Stop FS90R
}

void loop()
{
  // Move positional servo (servo1) from 0° to 90°, then back
  servo1.write(90); // Move to 90°
  delay(2000);      // Wait 2 seconds
  servo1.write(0);  // Move back to 0°
  delay(2000);      // Wait 2 seconds

  servo2.write(90);  // Stop
  delay(1000);       // Wait 1 second
  servo2.write(0);   // Full speed opposite direction
  delay(2000);       // Spin 2 seconds
}
