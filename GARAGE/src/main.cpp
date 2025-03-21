#include <Arduino.h>
#include "Air_Quality_Sensor.h"

// DHT22
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#define DHTPIN 2
#define DHTTYPE DHT22

DHT_Unified dht(DHTPIN, DHTTYPE);

AirQualitySensor sensor(A0);

int Relay = 9;
byte sensorPin = 11;
byte indicator = 13;
const int ledPin = 4;

unsigned long lastPollutionCheck = 0;
unsigned long lastTemperatureCheck = 0;
unsigned long lastPIRCheck = 0;

const unsigned long pollutionInterval = 1000;   // 1 second
const unsigned long temperatureInterval = 1000; // 1 second
const unsigned long pirInterval = 500;          // 0.5 seconds

void setup(void)
{
  pinMode(Relay, OUTPUT); // Pin 9 als output
  pinMode(sensorPin, INPUT);
  pinMode(indicator, OUTPUT);
  pinMode(ledPin, OUTPUT);
  Serial.begin(9600);

  dht.begin();

  while (!Serial)
    ;

  Serial.println("Waiting sensor to init...");
  unsigned long startTime = millis();
  while (millis() - startTime < 1000)
    ; // Wait for 1 second

  if (sensor.init())
  {
    Serial.println("Sensor ready.");
  }
  else
  {
    Serial.println("Sensor ERROR!");
  }
}

void pollutionAndTemperatureCheck()
{
  // Get temperature event and print its value.
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature))
  {
    Serial.println(F("Error reading temperature!"));
    return; // Exit if temperature reading fails
  }
  else
  {
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature);
    Serial.println(F("°C"));
  }

  int quality = sensor.slope();

  Serial.print("Sensor value: ");
  Serial.println(sensor.getValue());

  if (event.temperature > 24)
  {
    // Turn relay on regardless of air quality when temperature is above 22°C
    digitalWrite(Relay, HIGH); // Relais inschakelen
    Serial.println("Ventilation is on due to high temperature.");
  }
  else
  {
    if (quality == AirQualitySensor::FRESH_AIR || quality == AirQualitySensor::LOW_POLLUTION)
    {
      digitalWrite(Relay, LOW); // Relais uitschakelen
      Serial.println("Ventilation is off.");
    }
    else if (quality == AirQualitySensor::HIGH_POLLUTION || quality == AirQualitySensor::FORCE_SIGNAL)
    {
      digitalWrite(Relay, HIGH); // Relais inschakelen
      Serial.println("Ventilation is on due to high pollution.");
    }
  }

  // Print air quality status
  if (quality == AirQualitySensor::FORCE_SIGNAL)
  {
    Serial.println("High pollution! Force signal active.");
  }
  else if (quality == AirQualitySensor::HIGH_POLLUTION)
  {
    Serial.println("High pollution!");
  }
  else if (quality == AirQualitySensor::LOW_POLLUTION)
  {
    Serial.println("Low pollution!");
  }
  else if (quality == AirQualitySensor::FRESH_AIR)
  {
    Serial.println("Fresh air.");
  }
}

void pir()
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
}

void loop(void)
{
  unsigned long currentTime = millis();

  if (currentTime - lastPollutionCheck >= pollutionInterval)
  {
    pollutionAndTemperatureCheck();
    lastPollutionCheck = currentTime;
  }

  if (currentTime - lastPIRCheck >= pirInterval)
  {
    pir();
    lastPIRCheck = currentTime;
  }
}
