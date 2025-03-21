#include <Arduino.h>

const unsigned long intervalNormaal = 5000; // IntervalNormaal tussen elke statusverandering (in milliseconden)
unsigned long previousMillisNormaal = 0;    // Variabele om de vorige tijd op te slaan

const unsigned long intervalVeel = 3000;
unsigned long previousMillisVeel = 0;

int Relay = 9;

void setup()
{
    pinMode(Relay, OUTPUT); // Pin 9 als output
    Serial.begin(9600);
    Serial.println(" ");
}

void loop()
{

    digitalWrite(Relay, HIGH); // Relais inschakelen
    Serial.println("AAN");
    delay(3000);
    digitalWrite(Relay, LOW); // Relais inschakelen
    Serial.println("UIT");
    delay(3000);
}

/*AANSLUITING
Ventilator:
Rode > COM
Zwart > GND

Relais:

Voorkant:
Groen > 9
Rood > 3.3V
Zwart > GND

Actherkant:
NO > 5V

https://www.robotique.tech/robotics/controlling-a-dc-motor-with-arduino/ 

*/