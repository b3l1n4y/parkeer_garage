#include <Arduino.h>
const int ldrPin1 = A1; // Analog input pin for LDR 1 (Parking Spot 1)
const int ldrPin2 = A2; // Analog input pin for LDR 2 (Parking Spot 2)
const int ldrPin3 = A3; // Analog input pin for LDR 3 (Parking Spot 3)

void setup()
{
  Serial.begin(9600); // Initialize serial communication
}

void value(){

  int ldrValue1 = analogRead(ldrPin1); // Read the analog value from LDR 1
  int ldrValue2 = analogRead(ldrPin2); // Read the analog value from LDR 2
  int ldrValue3 = analogRead(ldrPin3); // Read the analog value from LDR 3

  Serial.print("Parking Spot Status: ");

  Serial.print("Spot 1: ");
  Serial.print(ldrValue1);
  Serial.print(", ");

  Serial.print("Spot 2: ");
  Serial.print(ldrValue2);
  Serial.print(", ");

  Serial.print("Spot 3: ");
  Serial.println(ldrValue3);

  delay(1000); // Wait for 1 second before checking again

}

void parking(){
  int ldrValue1 = analogRead(ldrPin1); // Read the analog value from LDR 1
  int ldrValue2 = analogRead(ldrPin2); // Read the analog value from LDR 2
  int ldrValue3 = analogRead(ldrPin3); // Read the analog value from LDR 3

  // Determine the threshold for shadow detection
  // Lower values indicate less light (shadow), adjust this threshold based on your setup
  const int shadowThreshold = 300;

  Serial.println("Parking Spot Status:");

  if (ldrValue1 < shadowThreshold)
  {
    Serial.println("Parking Spot 1: Occupied");
  }
  else
  {
    Serial.println("Parking Spot 1: Open");
  }

  if (ldrValue2 < shadowThreshold)
  {
    Serial.println("Parking Spot 2: Occupied");
  }
  else
  {
    Serial.println("Parking Spot 2: Open");
  }

  if (ldrValue3 < shadowThreshold)
  {
    Serial.println("Parking Spot 3: Occupied");
  }
  else
  {
    Serial.println("Parking Spot 3: Open");
  }

  delay(1000); // Wait for 1 second before checking again
}

void loop()
{
  value();
  // parking();
}
