#include <Arduino.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#define DHTPIN 2 

#define DHTTYPE DHT22 


DHT_Unified dht(DHTPIN, DHTTYPE);

int Relay = 9;

void setup()
{
  pinMode(Relay, OUTPUT); // Pin 9 als output
  Serial.begin(9600);
  dht.begin();
}

void temperature()
{
  // Get temperature event and print its value.
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature))
  {
    Serial.println(F("Error reading temperature!"));
  }
  else
  {
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature);
    Serial.println(F("°C"));
  }
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity))
  {
    Serial.println(F("Error reading humidity!"));
  }
  else
  {
    Serial.print(F("Humidity: "));
    Serial.print(event.relative_humidity);
    Serial.println(F("%"));
  }
  if (event.temperature > 20)
  {
    digitalWrite(Relay, HIGH); // Relais inschakelen
    Serial.println("Ventilaiton is on");
  }
  if (event.temperature < 20)
  {
    digitalWrite(Relay, LOW); // Relais inschakelen
    Serial.println("Ventilaiton is off");
  }
  delay(1000);
}

void loop()
{
  // // Get temperature event and print its value.
  // sensors_event_t event;
  // dht.temperature().getEvent(&event);
  // if (isnan(event.temperature))
  // {
  //   Serial.println(F("Error reading temperature!"));
  // }
  // else
  // {
  //   Serial.print(F("Temperature: "));
  //   Serial.print(event.temperature);
  //   Serial.println(F("°C"));
  // }
  // // Get humidity event and print its value.
  // dht.humidity().getEvent(&event);
  // if (isnan(event.relative_humidity))
  // {
  //   Serial.println(F("Error reading humidity!"));
  // }
  // else
  // {
  //   Serial.print(F("Humidity: "));
  //   Serial.print(event.relative_humidity);
  //   Serial.println(F("%"));
  // }
  // delay(1000);
  temperature();
}