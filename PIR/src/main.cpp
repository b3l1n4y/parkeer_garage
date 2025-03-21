#include <Arduino.h>
byte sensorPin = 11;
byte indicator = 13;

const int ledPin = 4;

void setup()
{
  pinMode(sensorPin, INPUT);
  pinMode(indicator, OUTPUT);
  pinMode(ledPin, OUTPUT);
  Serial.begin(9600);
}

void text(){
  byte state = digitalRead(sensorPin);
  digitalWrite(indicator, state);
  if (state == 1)
    Serial.println("Somebody is in this area!");
  else if (state == 0)
    Serial.println("No one!");
  delay(500);
}

void led()
{
  byte state = digitalRead(sensorPin);
  digitalWrite(indicator, state);
  if (state == 1)
  {
    digitalWrite(ledPin, HIGH); // Turn LED on when motion is detected
    Serial.println("Somebody is in this area!");
  }
  else
  {
    digitalWrite(ledPin, LOW); // Turn LED off when no motion is detected
  }

  delay(500);
}

void loop()
{
  led();
}