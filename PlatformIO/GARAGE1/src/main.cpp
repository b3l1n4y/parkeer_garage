#include <Arduino.h>
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include <DFRobot_GM60.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <RTC.h>
#include <Servo.h>
#include "Air_Quality_Sensor.h"
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <WiFiSSLClient.h>
#include <WiFiServer.h>
#include <U8g2lib.h>
#include "secrets.h"

// WiFi and Airtable settings
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;
const char *airtableHost = "api.airtable.com";
const int airtablePort = 443;

// WiFi Server for HTTP requests
WiFiServer server(80);

// FIXED: Timezone configuration for Brussels/Belgium (CET/CEST with DST)
const int TIMEZONE_OFFSET_HOURS = 2; // Current Brussels time (CEST = UTC+2)
const long TIMEZONE_OFFSET_SECONDS = TIMEZONE_OFFSET_HOURS * 3600;

// Time validation settings
const int EARLY_MINUTES_ALLOWED = 15; // Can scan 15 minutes early
const int LATE_MINUTES_ALLOWED = 60;  // Can scan 10 minutes late
const int EXPIRATION_MINUTES = 10;    // Auto-expire after 10 minutes
const int EXIT_GRACE_MINUTES = 5;     // 5 minutes grace period after end time

// Environmental sensors
#define DHTPIN 2
#define DHTTYPE DHT22
DHT_Unified dht(DHTPIN, DHTTYPE);
AirQualitySensor airSensor(A2);

// Environmental control pins
int Relay = 9;

// Servo configuration
Servo entryServo; // Servo on pin 3 for entry (QR system)
Servo exitServo;  // Servo on pin 6 for exit (QR system)
const int ENTRY_SERVO_PIN = 3;
const int EXIT_SERVO_PIN = 6;
const int SERVO_OPEN_ANGLE = 90;
const int SERVO_CLOSED_ANGLE = 0;
const unsigned long SERVO_TIMEOUT = 30000; // 30 seconds max open time (safety timeout)

// Servo state tracking
bool entryServoOpen = false;
bool exitServoOpen = false;
unsigned long entryServoOpenTime = 0;
unsigned long exitServoOpenTime = 0;
bool waitingForEntryCar = false; // Flag to track if we're waiting for a car to pass
bool waitingForExitCar = false;  // Flag to track if we're waiting for a car to pass

// Environmental monitoring intervals
unsigned long lastPollutionCheck = 0;
unsigned long lastPIRCheck = 0;
const unsigned long pollutionInterval = 1000;
const unsigned long pirInterval = 500;

// FIXED: NTP Client setup with proper timezone offset
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", TIMEZONE_OFFSET_SECONDS, 60000);

// QR scanner setup
DFRobot_GM60_UART gm60;
#define FPSerial Serial1

// OLED Display setup (SSD1315 compatible with SSD1306 library)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// Display state management
enum DisplayState
{
  DISPLAY_IDLE,
  DISPLAY_SCANNING,
  DISPLAY_PROCESSING,
  DISPLAY_SUCCESS,
  DISPLAY_ERROR
};

DisplayState currentDisplayState = DISPLAY_IDLE;
unsigned long displayUpdateTime = 0;
const unsigned long DISPLAY_TIMEOUT = 5000; // 5 seconds display timeout
String lastDisplayMessage = "";
String lastQRCode = "";
bool isFirstScanDisplay = false;

// Enhanced duplicate prevention system
String lastScannedCode = "";
unsigned long lastScanTime = 0;
const unsigned long SCAN_COOLDOWN = 8000;       // Increased to 8 seconds between scans
const unsigned long PROCESSING_LOCKOUT = 15000; // 15 seconds lockout during processing
bool isProcessingScan = false;
unsigned long processingStartTime = 0;

// Time sync variables
bool timeInitialized = false;
unsigned long lastTimeSync = 0;
const unsigned long TIME_SYNC_INTERVAL = 600000; // Sync every 10 minutes instead of 1 hour

WiFiSSLClient client;

// Forward declarations
void updateOLEDDisplay();
void showIdleScreen();
void showScanningScreen();
void showProcessingScreen();
void showSuccessScreen(bool isFirstScan, String qrCode);
void showErrorScreen(String errorMessage);
void showTimeoutScreen();
void initializeTime();
void syncTimeWithNTP();
String getCurrentTimeString();
void printCurrentTime();
bool isWithinAllowedTimeWindow(String reservationDate, String startTime, String endTime, String reservationId);
bool expireReservation(String reservationId, String reason);
void openEntryServo();
void openExitServo();
void closeEntryServo();
void closeExitServo();
void sendEnvironmentalDataToAirtable(float temperature, int airQuality);
void checkEnvironment();
void handleHTTPRequests();
bool checkQRCodeStatus(String qrCode, bool *isFirstScan);
bool updateTimestamp(String qrCode, bool isEntry);
bool isValidQRCode(String qrData);
bool isDuplicateScan(String qrData);

void setup()
{
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n=== COMBINED GARAGE MANAGEMENT SYSTEM ===");

  // Initialize environmental sensors
  pinMode(Relay, OUTPUT);
  digitalWrite(Relay, LOW);

  // Initialize OLED display
  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  // Welcome screen
  display.setCursor(0, 15);
  display.print("Car Parking");
  display.setCursor(0, 30);
  display.print("Setting 12");
  display.setCursor(0, 45);
  display.print("QR Scanner Ready");
  display.setCursor(0, 60);
  display.print("Scan QR Code...");
  display.sendBuffer();

  Serial.println("OLED Display initialized");

  dht.begin();
  airSensor.init();
  Serial.println("Environmental sensors initialized");

  // Initialize servos
  entryServo.attach(ENTRY_SERVO_PIN);
  exitServo.attach(EXIT_SERVO_PIN);
  entryServo.write(SERVO_CLOSED_ANGLE);
  exitServo.write(SERVO_CLOSED_ANGLE);
  Serial.println("Servos initialized on pins 3 (Entry) and 6 (Exit)");

  // Initialize QR code scanner
  Serial1.begin(9600);
  gm60.begin(Serial1);
  gm60.encode(gm60.eUTF8);
  gm60.setupCode(true, true);
  gm60.setIdentify(gm60.eEnableAllBarcode);
  Serial.println("QR code scanner initialized");

  // Connect to WiFi
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Start HTTP server
  server.begin();
  Serial.println("HTTP server started");

  // Initialize time with NTP
  initializeTime();

  Serial.println("=== SYSTEM READY ===");
  Serial.println("QR Code System: Scan QR codes for entry/exit");
  Serial.println("Environmental System: Temperature, Air Quality, PIR monitoring");
  Serial.println("HTTP API: /entry-gate and /exit-gate endpoints");
}

// OLED Display functions
void updateOLEDDisplay()
{
  unsigned long currentTime = millis();

  switch (currentDisplayState)
  {
  case DISPLAY_IDLE:
    if (currentTime - displayUpdateTime > 10000)
    { // Update idle screen every 10 seconds
      showIdleScreen();
      displayUpdateTime = currentTime;
    }
    break;

  case DISPLAY_SCANNING:
    showScanningScreen();
    break;

  case DISPLAY_PROCESSING:
    showProcessingScreen();
    break;

  case DISPLAY_SUCCESS:
    if (currentTime - displayUpdateTime > DISPLAY_TIMEOUT)
    {
      currentDisplayState = DISPLAY_IDLE;
    }
    break;

  case DISPLAY_ERROR:
    if (currentTime - displayUpdateTime > DISPLAY_TIMEOUT)
    {
      currentDisplayState = DISPLAY_IDLE;
    }
    break;
  }
}

void showIdleScreen()
{
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);

  // Header
  display.setCursor(0, 12);
  display.print("Car Parking 12");

  // Draw line
  display.drawLine(0, 16, 128, 16);

  display.setFont(u8g2_font_ncenB08_tr);
  display.setCursor(0, 30);
  display.print("Status: Ready");

  display.setCursor(0, 42);
  display.print("Scan QR Code");

  // Show current time
  display.setCursor(0, 54);
  display.print("Time: ");
  String timeStr = getCurrentTimeString();
  if (timeStr.length() > 10)
  {
    display.print(timeStr.substring(11, 16)); // Show only HH:MM
  }

  display.sendBuffer();
}

void showScanningScreen()
{
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);

  display.setCursor(0, 15);
  display.print("QR DETECTED!");

  display.setFont(u8g2_font_ncenB08_tr);
  display.setCursor(0, 30);
  display.print("Code: ");
  display.setCursor(0, 42);
  if (lastQRCode.length() > 16)
  {
    display.print(lastQRCode.substring(0, 16));
    display.setCursor(0, 54);
    display.print(lastQRCode.substring(16));
  }
  else
  {
    display.print(lastQRCode);
  }

  display.sendBuffer();
}

void showProcessingScreen()
{
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);

  display.setCursor(0, 15);
  display.print("PROCESSING...");

  display.setFont(u8g2_font_ncenB08_tr);
  display.setCursor(0, 35);
  display.print("Checking with");
  display.setCursor(0, 47);
  display.print("Airtable...");

  // Simple loading animation
  static int loadingDots = 0;
  loadingDots = (loadingDots + 1) % 4;
  display.setCursor(0, 59);
  for (int i = 0; i < loadingDots; i++)
  {
    display.print(".");
  }

  display.sendBuffer();
}

void showSuccessScreen(bool isFirstScan, String qrCode)
{
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);

  display.setCursor(0, 12);
  display.print("SUCCESS!");

  display.setFont(u8g2_font_ncenB08_tr);

  if (isFirstScan)
  {
    display.setCursor(0, 26);
    display.print("ENTRY RECORDED");
    display.setCursor(0, 38);
    display.print("Status: Scanned Once");
    display.setCursor(0, 50);
    display.print("Gate Opening...");
  }
  else
  {
    display.setCursor(0, 26);
    display.print("EXIT RECORDED");
    display.setCursor(0, 38);
    display.print("Status: Scanned Twice");
    display.setCursor(0, 50);
    display.print("Gate Opening...");
  }

  display.setCursor(0, 62);
  display.print("Time: ");
  String timeStr = getCurrentTimeString();
  if (timeStr.length() > 10)
  {
    display.print(timeStr.substring(11, 16));
  }

  display.sendBuffer();

  currentDisplayState = DISPLAY_SUCCESS;
  displayUpdateTime = millis();
}

void showErrorScreen(String errorMessage)
{
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);

  display.setCursor(0, 12);
  display.print("ERROR!");

  display.setFont(u8g2_font_ncenB08_tr);
  display.setCursor(0, 26);

  // Word wrap the error message
  if (errorMessage.length() > 21)
  {
    display.print(errorMessage.substring(0, 21));
    display.setCursor(0, 38);
    if (errorMessage.length() > 42)
    {
      display.print(errorMessage.substring(21, 42));
      display.setCursor(0, 50);
      display.print(errorMessage.substring(42));
    }
    else
    {
      display.print(errorMessage.substring(21));
    }
  }
  else
  {
    display.print(errorMessage);
  }

  display.setCursor(0, 62);
  display.print("Support: +32489660093");

  display.sendBuffer();

  currentDisplayState = DISPLAY_ERROR;
  displayUpdateTime = millis();
}

void showTimeoutScreen()
{
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);

  display.setCursor(0, 12);
  display.print("TIME EXPIRED");

  display.setFont(u8g2_font_ncenB08_tr);
  display.setCursor(0, 26);
  display.print("Reservation window");
  display.setCursor(0, 38);
  display.print("has closed");

  display.setCursor(0, 52);
  display.print("Contact Support:");
  display.setCursor(0, 62);
  display.print("+32 489 66 00 93");

  display.sendBuffer();

  currentDisplayState = DISPLAY_ERROR;
  displayUpdateTime = millis();
}

void openEntryServo()
{
  entryServo.write(SERVO_OPEN_ANGLE);
  entryServoOpen = true;
  waitingForEntryCar = true;
  entryServoOpenTime = millis();
  Serial.println("ðŸšª ENTRY SERVO OPENED (Pin 3) - Waiting for car to pass");
}

void openExitServo()
{
  exitServo.write(SERVO_OPEN_ANGLE);
  exitServoOpen = true;
  waitingForExitCar = true;
  exitServoOpenTime = millis();
  Serial.println("ðŸšª EXIT SERVO OPENED (Pin 6) - Waiting for car to pass");
}

void closeEntryServo()
{
  entryServo.write(SERVO_CLOSED_ANGLE);
  entryServoOpen = false;
  waitingForEntryCar = false;
  Serial.println("ðŸ”’ ENTRY SERVO CLOSED (Pin 3) - Car detected and passed");
}

void closeExitServo()
{
  exitServo.write(SERVO_CLOSED_ANGLE);
  exitServoOpen = false;
  waitingForExitCar = false;
  Serial.println("ðŸ”’ EXIT SERVO CLOSED (Pin 6) - Car detected and passed");
}


void sendEnvironmentalDataToAirtable(float temperature, int airQuality)
{
  Serial.println("\nSending environmental data to Airtable...");

  if (client.connect("api.airtable.com", 443))
  {
    String payload = "{";
    payload += "\"fields\": {";
    payload += "\"Temperature\": " + String(temperature) + ",";
    payload += "\"AirQuality\": " + String(airQuality);
    payload += "}}";

    Serial.println("Environmental payload: " + payload);

    client.println("POST /v0/" + String(AIRTABLE_BASE_ID) + "/Environment HTTP/1.1");
    client.println("Host: api.airtable.com");
    client.println("Authorization: Bearer " + String(AIRTABLE_API_KEY));
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(payload.length()));
    client.println("Connection: close");
    client.println();
    client.println(payload);

    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 5000)
    {
      delay(10);
    }

    while (client.available())
    {
      String line = client.readStringUntil('\n');
      if (line.startsWith("HTTP/1.1"))
      {
        if (line.indexOf("200 OK") > 0)
        {
          Serial.println("Environmental data sent successfully!");
        }
        else
        {
          Serial.println("Environmental data error: " + line);
        }
      }
    }
    client.stop();
  }
  else
  {
    Serial.println("Connection to Airtable failed for environmental data!");
  }
}

void checkEnvironment()
{
  static unsigned long lastSend = 0;
  sensors_event_t event;

  // Get temperature
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature))
  {
    Serial.println("Failed to read temperature!");
    return;
  }
  float temperature = event.temperature;

  // Get air quality
  int quality = airSensor.slope();
  int sensorValue = airSensor.getValue();

  // Enhanced Ventilation Control Logic with debugging
  if (temperature > 26)
  {
    digitalWrite(Relay, HIGH);
    Serial.println("ðŸŒ¡ï¸ Ventilation ON (High temp: " + String(temperature) + "Â°C)");
    Serial.println("DEBUG: Relay pin 9 set to HIGH");
  }
  else
  {
    if (quality == airSensor.FRESH_AIR || quality == airSensor.LOW_POLLUTION)
    {
      digitalWrite(Relay, LOW);
      Serial.println("ðŸŒ¿ Ventilation OFF (Good air quality)");
      Serial.println("DEBUG: Relay pin 9 set to LOW");

      // Force multiple attempts to turn off relay
      delay(100);
      digitalWrite(Relay, LOW);
      delay(100);
      digitalWrite(Relay, LOW);
      Serial.println("DEBUG: Multiple LOW commands sent to relay");
    }
    else if (quality == airSensor.HIGH_POLLUTION || quality == airSensor.FORCE_SIGNAL)
    {
      digitalWrite(Relay, HIGH);
      Serial.println("ðŸ’¨ Ventilation ON (Poor air quality)");
      Serial.println("DEBUG: Relay pin 9 set to HIGH");
    }
  }

  // Send to Airtable every 30 seconds
  if (millis() - lastSend >= 30000)
  {
    sendEnvironmentalDataToAirtable(temperature, sensorValue);
    lastSend = millis();
  }
}

void handleHTTPRequests()
{
  WiFiClient httpClient = server.available();
  if (httpClient)
  {
    String request = httpClient.readStringUntil('\r');
    Serial.println("HTTP Request: " + request);

    if (request.indexOf("/entry-gate") != -1)
    {
      openEntryServo();
      Serial.println("HTTP: Entry gate opened");
    }
    else if (request.indexOf("/exit-gate") != -1)
    {
      openExitServo();
      Serial.println("HTTP: Exit gate opened");
    }

    httpClient.println("HTTP/1.1 200 OK");
    httpClient.println("Content-Type: application/json");
    httpClient.println("Connection: close");
    httpClient.println();
    httpClient.println("{\"status\":\"OK\",\"system\":\"garage_management\"}");
    httpClient.stop();
  }
}

// FIXED: Enhanced time initialization with proper timezone handling
void initializeTime()
{
  Serial.println("Initializing time with NTP server...");
  Serial.print("Timezone: Brussels (UTC");
  Serial.print(TIMEZONE_OFFSET_HOURS >= 0 ? "+" : "");
  Serial.print(TIMEZONE_OFFSET_HOURS);
  Serial.println(")");

  RTC.begin();
  timeClient.begin();

  // Force multiple NTP updates to ensure we get correct time
  bool timeSet = false;
  int attempts = 0;

  while (!timeSet && attempts < 15) // Increased attempts
  {
    Serial.print("NTP attempt ");
    Serial.print(attempts + 1);
    Serial.print("/15... ");

    if (timeClient.update())
    {
      unsigned long epochTime = timeClient.getEpochTime();

      // More lenient epoch validation for 2025
      if (epochTime > 1700000000 && epochTime < 1800000000) // Between 2023-2027
      {
        RTCTime timeToSet = RTCTime(epochTime);
        RTC.setTime(timeToSet);
        timeInitialized = true;
        lastTimeSync = millis();
        timeSet = true;

        Serial.println("SUCCESS!");
        Serial.print("NTP Epoch: ");
        Serial.println(epochTime);
        Serial.print("UTC Time: ");
        Serial.println(timeClient.getFormattedTime());
        Serial.print("Brussels Time: ");
        printCurrentTime();
      }
      else
      {
        Serial.print("Invalid epoch: ");
        Serial.println(epochTime);
      }
    }
    else
    {
      Serial.println("FAILED");
    }

    attempts++;
    delay(3000); // Wait 3 seconds between attempts
  }

  if (!timeSet)
  {
    Serial.println("âŒ Failed to get time from NTP server after 15 attempts");
    Serial.println("âš ï¸ System will continue but time may be incorrect");
    timeInitialized = false;
  }
}

// FIXED: Enhanced time sync with better error handling
void syncTimeWithNTP()
{
  if (millis() - lastTimeSync > TIME_SYNC_INTERVAL)
  {
    Serial.println("ðŸ”„ Syncing time with NTP server...");

    if (timeClient.update())
    {
      unsigned long epochTime = timeClient.getEpochTime();

      // Validate epoch time before setting
      if (epochTime > 1700000000 && epochTime < 1800000000)
      {
        RTCTime timeToSet = RTCTime(epochTime);
        RTC.setTime(timeToSet);
        lastTimeSync = millis();
        timeInitialized = true;
        Serial.println("âœ… Time sync successful");
        Serial.print("New Brussels time: ");
        printCurrentTime();
      }
      else
      {
        Serial.print("âŒ Invalid epoch from NTP: ");
        Serial.println(epochTime);
      }
    }
    else
    {
      Serial.println("âŒ NTP sync failed");
    }
  }
}

String getCurrentTimeString()
{
  if (!timeInitialized)
    return "Time not available";

  RTCTime currentTime;
  RTC.getTime(currentTime);

  char timeStr[20];
  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
          currentTime.getYear(),
          Month2int(currentTime.getMonth()),
          currentTime.getDayOfMonth(),
          currentTime.getHour(),
          currentTime.getMinutes(),
          currentTime.getSeconds());

  return String(timeStr);
}

void printCurrentTime()
{
  if (!timeInitialized)
  {
    Serial.println("Time not available");
    return;
  }

  RTCTime currentTime;
  RTC.getTime(currentTime);

  char timeStr[50];
  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d (Brussels Time)",
          currentTime.getYear(),
          Month2int(currentTime.getMonth()),
          currentTime.getDayOfMonth(),
          currentTime.getHour(),
          currentTime.getMinutes(),
          currentTime.getSeconds());

  Serial.println(timeStr);
}

bool expireReservation(String reservationId, String reason)
{
  Serial.println("Expiring reservation: " + reservationId);
  Serial.println("Reason: " + reason);

  client.stop();
  delay(500);

  if (client.connect(airtableHost, airtablePort))
  {
    String timestamp = getCurrentTimeString();
    String payload = "{\"fields\":{\"Statuss\":\"Expired\",\"Notes\":\"" + reason + " at " + timestamp + "\"}}";

    String request = "PATCH /v0/" + String(AIRTABLE_BASE_ID) + "/Reservations/" + reservationId;
    request += " HTTP/1.1\r\n";
    request += "Host: api.airtable.com\r\n";
    request += "Authorization: Bearer " + String(AIRTABLE_API_KEY) + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + String(payload.length()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    request += payload;

    client.print(request);

    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 5000)
    {
      if (!client.connected())
      {
        client.stop();
        return false;
      }
      delay(100);
    }

    if (client.available())
    {
      String statusLine = client.readStringUntil('\n');
      if (statusLine.indexOf("200 OK") > 0)
      {
        Serial.println("âœ… Reservation expired successfully");
        client.stop();
        return true;
      }
    }
    client.stop();
  }
  return false;
}

bool isWithinAllowedTimeWindow(String reservationDate, String startTime, String endTime, String reservationId)
{
  if (!timeInitialized)
  {
    Serial.println("âš ï¸ Time not available - allowing scan");
    return true;
  }

  RTCTime currentTime;
  RTC.getTime(currentTime);

  // Get current date and time
  int currentYear = currentTime.getYear();
  int currentMonth = Month2int(currentTime.getMonth());
  int currentDay = currentTime.getDayOfMonth();
  int currentHour = currentTime.getHour();
  int currentMinute = currentTime.getMinutes();

  // Parse reservation date (format: YYYY-MM-DD)
  int resYear = reservationDate.substring(0, 4).toInt();
  int resMonth = reservationDate.substring(5, 7).toInt();
  int resDay = reservationDate.substring(8, 10).toInt();

  // Parse start time (format: HH:MM)
  int startHour = startTime.substring(0, 2).toInt();
  int startMinute = startTime.substring(3, 5).toInt();

  // Parse end time (format: HH:MM)
  int endHour = endTime.substring(0, 2).toInt();
  int endMinute = endTime.substring(3, 5).toInt();

  // Detect if this is a midnight-crossing reservation
  bool crossesMidnight = (startHour >= 20 && endHour <= 6);

  // Check if we're on the correct date
  bool isCorrectDate = false;

  if (currentYear == resYear && currentMonth == resMonth && currentDay == resDay)
  {
    isCorrectDate = true;
  }
  else if (crossesMidnight)
  {
    // For midnight-crossing reservations, also check if we're on the previous day
    int prevDay = resDay - 1;
    int prevMonth = resMonth;
    int prevYear = resYear;

    if (prevDay < 1)
    {
      prevMonth--;
      if (prevMonth < 1)
      {
        prevMonth = 12;
        prevYear--;
      }
      prevDay = 31;
    }

    if (currentYear == prevYear && currentMonth == prevMonth && currentDay == prevDay)
    {
      isCorrectDate = true;
    }
  }

  if (!isCorrectDate)
  {
    Serial.println("âŒ Wrong date for reservation");
    return false;
  }

  // Convert times to minutes for easier calculation
  int currentTotalMinutes = currentHour * 60 + currentMinute;
  int startTotalMinutes = startHour * 60 + startMinute;
  int endTotalMinutes = endHour * 60 + endMinute;

  // Handle midnight crossover for end time
  if (crossesMidnight)
  {
    endTotalMinutes += 24 * 60;
  }

  if (crossesMidnight && currentDay == resDay && currentHour <= 6)
  {
    currentTotalMinutes += 24 * 60;
  }

  // UPDATED TIME WINDOWS - Changed from LATE_MINUTES_ALLOWED to using end time
  int earliestAllowed = startTotalMinutes - EARLY_MINUTES_ALLOWED; // 15 minutes before start
  int latestAllowed = endTotalMinutes + EXIT_GRACE_MINUTES;        // 5 minutes after end (for EXIT only)
  int entryLatestAllowed = endTotalMinutes;                        // NEW: Entry allowed until end time
  int expirationTime = startTotalMinutes + EXPIRATION_MINUTES;     // 10 minutes after start

  Serial.print("Current time: ");
  Serial.print(currentHour);
  Serial.print(":");
  Serial.println(currentMinute);
  Serial.print("Reservation window: ");
  Serial.print(startHour);
  Serial.print(":");
  Serial.print(startMinute);
  Serial.print(" - ");
  Serial.print(endHour);
  Serial.print(":");
  Serial.println(endMinute);
  Serial.print("Entry allowed: ");
  Serial.print((earliestAllowed / 60) % 24);
  Serial.print(":");
  Serial.print((earliestAllowed % 60));
  Serial.print(" - ");
  Serial.print((entryLatestAllowed / 60) % 24);
  Serial.print(":");
  Serial.println((entryLatestAllowed % 60));

  // Check if reservation should be expired (10 minutes after start time)
  if (currentTotalMinutes > expirationTime)
  {
    Serial.println("â° Reservation has expired (10 minutes after start time)");
    expireReservation(reservationId, "Auto-expired: 10 minutes after start time");
    return false;
  }

  // Check if current time is within allowed ENTRY window (15 min early to end time)
  if (currentTotalMinutes >= earliestAllowed && currentTotalMinutes <= entryLatestAllowed)
  {
    Serial.println("âœ… Within allowed ENTRY time window");
    return true;
  }
  // Special case: Allow EXIT during grace period (5 minutes after end time)
  else if (currentTotalMinutes > endTotalMinutes && currentTotalMinutes <= latestAllowed)
  {
    Serial.println("âš ï¸ GRACE PERIOD: Exit allowed (entry not permitted)");
    return true; // Allow for exit scans only
  }
  else
  {
    Serial.println("âŒ Outside allowed time window");
    if (currentTotalMinutes < earliestAllowed)
    {
      int minutesTooEarly = earliestAllowed - currentTotalMinutes;
      Serial.print("Too early by ");
      Serial.print(minutesTooEarly);
      Serial.println(" minutes");
    }
    else if (currentTotalMinutes > entryLatestAllowed)
    {
      int minutesTooLate = currentTotalMinutes - entryLatestAllowed;
      Serial.print("Entry window closed ");
      Serial.print(minutesTooLate);
      Serial.println(" minutes ago");
      Serial.println("ðŸš« ENTRY PERIOD EXPIRED - Contact support: +32 489 66 00 93");
    }
    return false;
  }
}

bool checkQRCodeStatus(String qrCode, bool *isFirstScan)
{
  Serial.println("\nChecking QR code status: " + qrCode);

  client.stop();
  delay(500);

  if (client.connect(airtableHost, airtablePort))
  {
    Serial.println("Connected to Airtable server");

    // Get the record to check its status
    String request = "GET /v0/" + String(AIRTABLE_BASE_ID) + "/Reservations/" + qrCode;
    request += " HTTP/1.1\r\n";
    request += "Host: api.airtable.com\r\n";
    request += "Authorization: Bearer " + String(AIRTABLE_API_KEY) + "\r\n";
    request += "Connection: close\r\n\r\n";

    client.print(request);

    Serial.println("Request sent, waiting for response...");

    // Wait for response with timeout
    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 10000)
    {
      if (!client.connected())
      {
        Serial.println("Connection lost while waiting for response");
        client.stop();
        return false;
      }
      delay(100);
    }

    // Skip HTTP headers
    bool headersEnded = false;
    while (client.available() && !headersEnded)
    {
      String line = client.readStringUntil('\n');
      if (line == "\r")
      {
        headersEnded = true;
      }

      // Check if record exists
      if (line.indexOf("HTTP/1.1 404") >= 0)
      {
        Serial.println("QR code not found in database");
        client.stop();
        return false;
      }
    }

    // Read the JSON response body
    String jsonResponse = "";
    timeout = millis();

    while (client.available() && millis() - timeout < 5000)
    {
      char c = client.read();
      jsonResponse += c;

      // Prevent buffer overflow
      if (jsonResponse.length() > 4000)
      {
        Serial.println("Response too large, truncating...");
        break;
      }
    }

    client.stop();

    if (jsonResponse.length() > 0)
    {
      // Parse the JSON response
      DynamicJsonDocument doc(4096);
      DeserializationError error = deserializeJson(doc, jsonResponse);

      if (error)
      {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return false;
      }

      // Check reservation status first
      String status = doc["fields"]["Statuss"].as<String>();

      // If the reservation is cancelled or expired, reject it
      if (status == "Cancelled" || status == "Expired")
      {
        String statusLower = status;
        statusLower.toLowerCase();
        Serial.print("âŒ This reservation has been ");
        Serial.println(statusLower);
        return false;
      }

      // Get reservation time details for validation
      String reservationDate = doc["fields"]["Date"].as<String>();
      String startTime = doc["fields"]["StartTime"].as<String>();
      String endTime = doc["fields"]["EndTime"].as<String>();

      // Check if we're within the allowed time window (includes expiration check)
      if (!isWithinAllowedTimeWindow(reservationDate, startTime, endTime, qrCode))
      {
        return false; // Time validation failed or reservation expired
      }

      // Check if EntryTime exists
      bool hasEntryTime = doc["fields"].containsKey("EntryTime");
      bool hasExitTime = doc["fields"].containsKey("ExitTime");

      Serial.print("Entry time recorded: ");
      Serial.println(hasEntryTime ? "Yes" : "No");
      Serial.print("Exit time recorded: ");
      Serial.println(hasExitTime ? "Yes" : "No");

      // If both entry and exit times exist, this QR code has been used twice already
      if (hasEntryTime && hasExitTime)
      {
        Serial.println("âš ï¸ QR code has already been used for both entry and exit");
        return false;
      }

      // Determine if this is first or second scan
      *isFirstScan = !hasEntryTime;
      return true;
    }
    else
    {
      Serial.println("No data received from Airtable");
      return false;
    }
  }
  else
  {
    Serial.println("Connection to Airtable failed!");
    return false;
  }
}

bool updateTimestamp(String qrCode, bool isEntry)
{
  Serial.print("Recording ");
  Serial.print(isEntry ? "entry" : "exit");
  Serial.println(" timestamp...");

  client.stop();
  delay(500);

  if (client.connect(airtableHost, airtablePort))
  {
    // Get current timestamp in Brussels timezone
    String timestamp = getCurrentTimeString();

    // Set the appropriate status based on entry/exit
    String newStatus = isEntry ? "Scanned Once" : "Scanned Twice";

    // Create PATCH request to update the record
    String fieldName = isEntry ? "EntryTime" : "ExitTime";

    // Create JSON payload with both timestamp and status update
    String payload = "{\"fields\":{\"" + fieldName + "\":\"" + timestamp + "\",\"Statuss\":\"" + newStatus + "\"}}";

    String request = "PATCH /v0/" + String(AIRTABLE_BASE_ID) + "/Reservations/" + qrCode;
    request += " HTTP/1.1\r\n";
    request += "Host: api.airtable.com\r\n";
    request += "Authorization: Bearer " + String(AIRTABLE_API_KEY) + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + String(payload.length()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    request += payload;

    client.print(request);

    // Wait for response with timeout
    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 5000)
    {
      if (!client.connected())
      {
        Serial.println("Connection lost while waiting for response");
        client.stop();
        return false;
      }
      delay(100);
    }

    // Check response status
    if (client.available())
    {
      String statusLine = client.readStringUntil('\n');

      if (statusLine.indexOf("200 OK") > 0)
      {
        Serial.print("âœ… ");
        Serial.print(isEntry ? "Entry" : "Exit");
        Serial.print(" time recorded and status updated to ");
        Serial.println(newStatus);
        Serial.print("Timestamp: ");
        Serial.println(timestamp);
        client.stop();
        return true;
      }
      else
      {
        Serial.println("âŒ Failed to update timestamp and status: " + statusLine);
        client.stop();
        return false;
      }
    }

    client.stop();
    return false;
  }
  else
  {
    Serial.println("Connection to Airtable failed for update!");
    return false;
  }
}

bool isValidQRCode(String qrData)
{
  // Check if the QR code is a valid Airtable record ID
  // Airtable record IDs typically start with "rec" and are 17 characters long
  if (qrData.length() < 3)
    return false;
  if (qrData.startsWith("rec") && qrData.length() >= 17)
    return true;

  // If it doesn't match the typical Airtable ID pattern,
  // check if it's at least not "null", "NULL", or empty
  if (qrData.equalsIgnoreCase("null") || qrData.length() == 0)
    return false;

  // Other validation criteria can be added here
  return true;
}

bool isDuplicateScan(String qrData)
{
  unsigned long currentTime = millis();

  // Check if we're currently processing a scan
  if (isProcessingScan)
  {
    if (currentTime - processingStartTime < PROCESSING_LOCKOUT)
    {
      Serial.println("â¸ï¸ System is processing previous scan, please wait...");
      return true; // Treat as duplicate during processing
    }
    else
    {
      // Processing timeout, reset the flag
      isProcessingScan = false;
    }
  }

  // Check if this is the same code scanned recently
  if (qrData == lastScannedCode && (currentTime - lastScanTime < SCAN_COOLDOWN))
  {
    unsigned long remainingCooldown = (SCAN_COOLDOWN - (currentTime - lastScanTime)) / 1000;
    Serial.print("ðŸ”„ Same QR code scanned too quickly. Please wait ");
    Serial.print(remainingCooldown);
    Serial.println(" more seconds.");
    return true;
  }

  return false;
}

void loop()
{
  // Sync time periodically
  syncTimeWithNTP();

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected! Reconnecting...");
    WiFi.begin(ssid, password);
    delay(5000);
    return;
  }

  // Handle HTTP requests for gate control
  handleHTTPRequests();

  // Environmental monitoring
  unsigned long currentTime = millis();
  if (currentTime - lastPollutionCheck >= pollutionInterval)
  {
    checkEnvironment();
    lastPollutionCheck = currentTime;
  }

  // --- SERVO AUTO-CLOSE LOGIC (5 seconds) ---
  if (entryServoOpen && (currentTime - entryServoOpenTime >= 5000))
  {
    closeEntryServo();
    Serial.println("🔒 Entry servo auto-closed after 5 seconds");
  }
  if (exitServoOpen && (currentTime - exitServoOpenTime >= 5000))
  {
    closeExitServo();
    Serial.println("🔒 Exit servo auto-closed after 5 seconds");
  }
  // ------------------------------------------

  // Update OLED display
  updateOLEDDisplay();

  // QR code scanning with OLED feedback
  String qrData = gm60.detection();

  if (qrData.length() > 0 && qrData != "NULL" && !qrData.equalsIgnoreCase("null"))
  {
    // Check for duplicates first
    if (isDuplicateScan(qrData))
    {
      delay(100);
      return;
    }

    if (isValidQRCode(qrData))
    {
      // Show QR detected on OLED
      lastQRCode = qrData;
      currentDisplayState = DISPLAY_SCANNING;
      delay(1000); // Show scanning screen for 1 second

      // Show processing screen
      currentDisplayState = DISPLAY_PROCESSING;

      // Set processing flag
      isProcessingScan = true;
      processingStartTime = millis();
      lastScannedCode = qrData;
      lastScanTime = millis();

      Serial.println("\n=== QR CODE SCANNED ===");
      Serial.println("Data: " + qrData);
      Serial.print("Scan time: ");
      printCurrentTime();

      bool isFirstScan = true;
      bool isValid = checkQRCodeStatus(qrData, &isFirstScan);

      if (isValid)
      {
        if (isFirstScan)
        {
          Serial.println("👋 FIRST SCAN - Recording entry time");
          if (updateTimestamp(qrData, true))
          {
            Serial.println("✅ Entry recorded successfully");
            showSuccessScreen(true, qrData);
            openEntryServo(); // This sets entryServoOpenTime
          }
          else
          {
            showErrorScreen("Failed to record entry");
          }
        }
        else
        {
          Serial.println("👋 SECOND SCAN - Recording exit time");
          if (updateTimestamp(qrData, false))
          {
            Serial.println("✅ Exit recorded successfully");
            showSuccessScreen(false, qrData);
            openExitServo(); // This sets exitServoOpenTime
          }
          else
          {
            showErrorScreen("Failed to record exit");
          }
        }
      }
      else
      {
        Serial.println("❌ Invalid QR code, outside time window, expired, or already used twice");
        showErrorScreen("Invalid QR Code or Time Window Expired");
      }

      Serial.println("=== SCAN COMPLETE ===\n");
      isProcessingScan = false;
      delay(3000);
    }
  }

  delay(100);
}
