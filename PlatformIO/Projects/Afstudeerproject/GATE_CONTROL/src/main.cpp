#include <Arduino.h>
#include <WiFiS3.h>
#include <Servo.h>
#include "secret_IoT.h" // Contains WiFi credentials

Servo gateServo;
const int SERVO_PIN = 7;
bool gateOpen = false;
unsigned long gateTimer = 0;
const unsigned long GATE_OPEN_DURATION = 5000;

WiFiServer server(80);

void setup()
{
  Serial.begin(9600);

  // Wait for serial connection or timeout after 10 seconds
  unsigned long serialTimeout = millis();
  while (!Serial && (millis() - serialTimeout < 10000))
  {
    ; // Wait for serial port to connect
  }

  gateServo.attach(SERVO_PIN);
  gateServo.write(0); // Start closed

  // Connect to WiFi
  Serial.print("\nConnecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);

  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiTimeout < 20000))
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    server.begin();
  }
  else
  {
    Serial.println("\nFailed to connect to WiFi!");
    while (true)
      ; // Halt if no connection
  }
}

void openGate()
{
  gateServo.write(90);
  gateOpen = true;
  gateTimer = millis();
  Serial.println("Gate opened");
}

void closeGate()
{
  gateServo.write(0);
  gateOpen = false;
  Serial.println("Gate closed");
}

void loop()
{
  WiFiClient client = server.available();

  if (client)
  {
    String request = client.readStringUntil('\r');
    client.flush();

    if (request.indexOf("GET /open") != -1)
    {
      openGate();
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();
      client.println("Gate opened!");
    }

    client.stop();
  }

  // Auto-close gate after duration
  if (gateOpen && millis() - gateTimer > GATE_OPEN_DURATION)
  {
    closeGate();
  }
}
