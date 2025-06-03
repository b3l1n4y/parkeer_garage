#include <Arduino.h>
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include <DFRobot_GM60.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <RTC.h>
#include <Servo.h>
#include "secrets.h"

// WiFi and Airtable settings
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;
const char *airtableHost = "api.airtable.com";
const int airtablePort = 443;

// Timezone configuration for Brussels/Belgium (UTC+1/UTC+2 with DST)
const int TIMEZONE_OFFSET_HOURS = 2; // CET (Central European Time)

// Time validation settings
const int EARLY_MINUTES_ALLOWED = 15; // Can scan 15 minutes early
const int LATE_MINUTES_ALLOWED = 10;  // Can scan 10 minutes late
const int EXPIRATION_MINUTES = 10;    // Auto-expire after 10 minutes
const int EXIT_GRACE_MINUTES = 5;     // 5 minutes grace period after end time

// LDR sensor configuration with individual thresholds
const int ENTRY_LDR_PIN = A0;
const int EXIT_LDR_PIN = A1;

// Individual thresholds based on each sensor's baseline
int entryLDRBaseline = 0;
int exitLDRBaseline = 0;
int entryLDRThreshold = 0; // Will be calculated based on baseline
int exitLDRThreshold = 0;  // Will be calculated based on baseline

// Car detection state tracking
bool entryCarDetected = false; // Track if car is currently passing
bool exitCarDetected = false;  // Track if car is currently passing

// Servo configuration
Servo entryServo; // Servo on pin 3 for entry
Servo exitServo;  // Servo on pin 6 for exit
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

// NTP Client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", TIMEZONE_OFFSET_HOURS * 3600, 60000);

// QR scanner setup
DFRobot_GM60_UART gm60;
#define FPSerial Serial1

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
const unsigned long TIME_SYNC_INTERVAL = 3600000; // Sync every hour

WiFiSSLClient client;

// Forward declarations
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
void checkLDRSensors();
void calibrateLDRSensors();

void setup()
{
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n=== QR CODE ENTRY/EXIT SYSTEM ===");

  // Initialize LDR sensors
  pinMode(ENTRY_LDR_PIN, INPUT);
  pinMode(EXIT_LDR_PIN, INPUT);
  Serial.println("LDR sensors initialized on pins A0 (Entry) and A1 (Exit)");

  // Calibrate LDR sensors (get baseline readings)
  calibrateLDRSensors();

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

  // Initialize time with NTP
  initializeTime();

  Serial.println("=== SYSTEM READY ===");
  Serial.println("Scan a QR code to record entry/exit");
  Serial.println("Time window: 15 minutes early to 10 minutes late");
  Serial.println("Auto-expiration: 10 minutes after start time");
  Serial.println("Double-scan prevention: 8 second cooldown");
  Serial.println("Servo control: Pin 3 (Entry), Pin 6 (Exit) - LDR controlled closing");
  Serial.println("LDR detection: Individual thresholds for each sensor");
}

void calibrateLDRSensors()
{
  Serial.println("Calibrating LDR sensors...");
  Serial.println("Make sure no cars are blocking the sensors for 3 seconds");

  delay(3000); // Give time for setup

  // Take multiple readings and average them
  long entrySum = 0;
  long exitSum = 0;
  int samples = 10;

  for (int i = 0; i < samples; i++)
  {
    entrySum += analogRead(ENTRY_LDR_PIN);
    exitSum += analogRead(EXIT_LDR_PIN);
    delay(100);
  }

  entryLDRBaseline = entrySum / samples;
  exitLDRBaseline = exitSum / samples;

  // Much more sensitive thresholds based on your actual readings
  // Your entry sensor only varies by 1-6, so use very low thresholds

  if (entryLDRBaseline > 900)
  {
    entryLDRThreshold = 10; // Very sensitive for stable high-baseline sensors
  }
  else if (entryLDRBaseline > 500)
  {
    entryLDRThreshold = 30; // Moderate sensitivity
  }
  else
  {
    entryLDRThreshold = entryLDRBaseline * 0.3; // 30% for dark sensors
  }

  if (exitLDRBaseline > 500)
  {
    exitLDRThreshold = exitLDRBaseline * 0.3; // Keep existing logic for exit (it works)
  }
  else
  {
    exitLDRThreshold = exitLDRBaseline * 0.3;
  }

  Serial.print("Entry LDR baseline: ");
  Serial.println(entryLDRBaseline);
  Serial.print("Entry LDR threshold: ");
  Serial.println(entryLDRThreshold);
  Serial.print("Exit LDR baseline: ");
  Serial.println(exitLDRBaseline);
  Serial.print("Exit LDR threshold: ");
  Serial.println(exitLDRThreshold);
  Serial.println("LDR calibration complete");
}

void openEntryServo()
{
  entryServo.write(SERVO_OPEN_ANGLE);
  entryServoOpen = true;
  waitingForEntryCar = true;
  entryServoOpenTime = millis();
  Serial.println("🚪 ENTRY SERVO OPENED (Pin 3) - Waiting for car to pass");
}

void openExitServo()
{
  exitServo.write(SERVO_OPEN_ANGLE);
  exitServoOpen = true;
  waitingForExitCar = true;
  exitServoOpenTime = millis();
  Serial.println("🚪 EXIT SERVO OPENED (Pin 6) - Waiting for car to pass");
}

void closeEntryServo()
{
  entryServo.write(SERVO_CLOSED_ANGLE);
  entryServoOpen = false;
  waitingForEntryCar = false;
  entryCarDetected = false; // Reset detection state
  Serial.println("🔒 ENTRY SERVO CLOSED (Pin 3) - Car detected and passed");
}

void closeExitServo()
{
  exitServo.write(SERVO_CLOSED_ANGLE);
  exitServoOpen = false;
  waitingForExitCar = false;
  exitCarDetected = false; // Reset detection state
  Serial.println("🔒 EXIT SERVO CLOSED (Pin 6) - Car detected and passed");
}

void checkLDRSensors()
{
  unsigned long currentTime = millis();

  // Read current LDR values
  int entryLDRValue = analogRead(ENTRY_LDR_PIN);
  int exitLDRValue = analogRead(EXIT_LDR_PIN);

  // Debug output every 2 seconds when waiting for cars
  static unsigned long lastDebugTime = 0;
  if (currentTime - lastDebugTime > 2000)
  {
    if (waitingForEntryCar && entryServoOpen)
    {
      Serial.print("🔍 Entry LDR: ");
      Serial.print(entryLDRValue);
      Serial.print(" (baseline: ");
      Serial.print(entryLDRBaseline);
      Serial.print(", threshold: ");
      Serial.print(entryLDRThreshold);
      Serial.print(", difference: ");
      Serial.print(abs(entryLDRBaseline - entryLDRValue));
      Serial.println(")");
    }
    if (waitingForExitCar && exitServoOpen)
    {
      Serial.print("🔍 Exit LDR: ");
      Serial.print(exitLDRValue);
      Serial.print(" (baseline: ");
      Serial.print(exitLDRBaseline);
      Serial.print(", threshold: ");
      Serial.print(exitLDRThreshold);
      Serial.print(", difference: ");
      Serial.print(abs(exitLDRBaseline - exitLDRValue));
      Serial.println(")");
    }
    lastDebugTime = currentTime;
  }

  // Check entry gate with very sensitive detection
  if (waitingForEntryCar && entryServoOpen)
  {
    int entryDifference = abs(entryLDRBaseline - entryLDRValue);

    // Check if car is currently blocking the sensor (any significant change)
    if (entryDifference >= entryLDRThreshold)
    {
      if (!entryCarDetected)
      {
        entryCarDetected = true;
        Serial.print("🚗 Car entering - sensor change detected! Difference: ");
        Serial.print(entryDifference);
        Serial.print(" (LDR value: ");
        Serial.print(entryLDRValue);
        Serial.println(")");
      }
    }
    // Check if car has finished passing (back to baseline)
    else if (entryCarDetected && entryDifference < (entryLDRThreshold / 2))
    {
      Serial.print("🚗 Car finished passing entry gate! LDR value: ");
      Serial.print(entryLDRValue);
      Serial.print(" (difference: ");
      Serial.print(entryDifference);
      Serial.println(")");
      entryCarDetected = false;
      closeEntryServo();
    }
    // Safety timeout - close gate after 30 seconds even if no car detected
    else if (currentTime - entryServoOpenTime > SERVO_TIMEOUT)
    {
      Serial.println("⏰ Entry gate timeout - closing for safety");
      Serial.print("Final entry LDR value: ");
      Serial.print(entryLDRValue);
      Serial.print(" (difference: ");
      Serial.print(entryDifference);
      Serial.print(", needed: ");
      Serial.print(entryLDRThreshold);
      Serial.println(")");
      entryCarDetected = false;
      closeEntryServo();
    }
  }

  // Keep existing exit gate logic (it works perfectly)
  if (waitingForExitCar && exitServoOpen)
  {
    int exitDifference = abs(exitLDRBaseline - exitLDRValue);

    if (exitDifference >= exitLDRThreshold)
    {
      if (!exitCarDetected)
      {
        exitCarDetected = true;
        Serial.println("🚗 Car exiting - sensor blocked");
      }
    }
    else if (exitCarDetected && exitDifference < (exitLDRThreshold / 2))
    {
      Serial.print("🚗 Car finished passing exit gate! LDR value: ");
      Serial.println(exitLDRValue);
      exitCarDetected = false;
      closeExitServo();
    }
    else if (currentTime - exitServoOpenTime > SERVO_TIMEOUT)
    {
      Serial.println("⏰ Exit gate timeout - closing for safety");
      exitCarDetected = false;
      closeExitServo();
    }
  }
}

void initializeTime()
{
  Serial.println("Initializing time with NTP server...");

  // Initialize RTC
  RTC.begin();

  // Start NTP client
  timeClient.begin();
  timeClient.update();

  // Get time from NTP server
  unsigned long epochTime = timeClient.getEpochTime();

  if (epochTime > 0)
  {
    // Set RTC with NTP time
    RTCTime timeToSet = RTCTime(epochTime);
    RTC.setTime(timeToSet);

    timeInitialized = true;
    lastTimeSync = millis();

    Serial.print("Time synchronized with NTP server: ");
    printCurrentTime();
  }
  else
  {
    Serial.println("Failed to get time from NTP server");
    timeInitialized = false;
  }
}

void syncTimeWithNTP()
{
  if (millis() - lastTimeSync > TIME_SYNC_INTERVAL)
  {
    Serial.println("Syncing time with NTP server...");

    if (timeClient.update())
    {
      unsigned long epochTime = timeClient.getEpochTime();
      RTCTime timeToSet = RTCTime(epochTime);
      RTC.setTime(timeToSet);

      lastTimeSync = millis();
      Serial.print("Time re-synchronized: ");
      printCurrentTime();
    }
    else
    {
      Serial.println("Failed to sync time with NTP server");
    }
  }
}

String getCurrentTimeString()
{
  if (!timeInitialized)
  {
    return "Time not available";
  }

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

  // Use sprintf to format the time string, then print it
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
    // Get current timestamp
    String timestamp = getCurrentTimeString();

    // Use the correct single record format (not records array)
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

    // Wait for response
    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 5000)
    {
      if (!client.connected())
      {
        Serial.println("Connection lost while expiring reservation");
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
        Serial.println("✅ Reservation expired successfully");
        client.stop();
        return true;
      }
      else
      {
        Serial.println("❌ Failed to expire reservation: " + statusLine);
      }
    }

    client.stop();
    return false;
  }
  else
  {
    Serial.println("Connection to Airtable failed for expiration!");
    return false;
  }
}

bool isWithinAllowedTimeWindow(String reservationDate, String startTime, String endTime, String reservationId)
{
  if (!timeInitialized)
  {
    Serial.println("⚠️ Time not available - allowing scan");
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
    Serial.println("❌ Wrong date for reservation");
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

  // Calculate time windows
  int earliestAllowed = startTotalMinutes - EARLY_MINUTES_ALLOWED; // 15 minutes before start
  int latestAllowed = endTotalMinutes + EXIT_GRACE_MINUTES;        // 5 minutes after end (GRACE PERIOD)
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
  Serial.print("Allowed scan window: ");
  Serial.print((earliestAllowed / 60) % 24);
  Serial.print(":");
  Serial.print((earliestAllowed % 60));
  Serial.print(" - ");
  Serial.print((latestAllowed / 60) % 24);
  Serial.print(":");
  Serial.println((latestAllowed % 60));

  // Check if reservation should be expired (10 minutes after start time)
  if (currentTotalMinutes > expirationTime)
  {
    Serial.println("⏰ Reservation has expired (10 minutes after start time)");
    expireReservation(reservationId, "Auto-expired: 10 minutes after start time");
    return false;
  }

  // Enhanced exit time validation
  if (currentTotalMinutes > endTotalMinutes && currentTotalMinutes <= latestAllowed)
  {
    Serial.println("⚠️ GRACE PERIOD: You have 5 minutes to exit after reservation end time");
  }

  // Check if current time is within allowed window (including grace period)
  if (currentTotalMinutes >= earliestAllowed && currentTotalMinutes <= latestAllowed)
  {
    Serial.println("✅ Within allowed time window");
    return true;
  }
  else
  {
    Serial.println("❌ Outside allowed time window");
    if (currentTotalMinutes < earliestAllowed)
    {
      int minutesTooEarly = earliestAllowed - currentTotalMinutes;
      Serial.print("Too early by ");
      Serial.print(minutesTooEarly);
      Serial.println(" minutes");
    }
    else
    {
      int minutesTooLate = currentTotalMinutes - latestAllowed;
      Serial.print("Too late by ");
      Serial.print(minutesTooLate);
      Serial.println(" minutes");
      Serial.println("🚫 GRACE PERIOD EXPIRED - Contact support: +32 489 66 00 93");
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
        // Create a copy of status
        String statusLower = status;
        statusLower.toLowerCase();

        // Then print concatenated string
        Serial.print("❌ This reservation has been ");
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
        Serial.println("⚠️ QR code has already been used for both entry and exit");
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
        Serial.print("✅ ");
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
        Serial.println("❌ Failed to update timestamp and status: " + statusLine);
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
      Serial.println("⏸️ System is processing previous scan, please wait...");
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
    Serial.print("🔄 Same QR code scanned too quickly. Please wait ");
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

  // Check LDR sensors for car detection
  checkLDRSensors();

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected! Reconnecting...");
    WiFi.begin(ssid, password);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
    {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("\nReconnection failed. Waiting before retry...");
      delay(5000);
      return;
    }

    Serial.println("\nReconnected to WiFi!");
  }

  // Check for QR code scan
  String qrData = gm60.detection();
  unsigned long currentTime = millis();

  // Enhanced duplicate prevention
  if (qrData.length() > 0 && qrData != "NULL" && !qrData.equalsIgnoreCase("null"))
  {
    // Check for duplicates first
    if (isDuplicateScan(qrData))
    {
      delay(100); // Small delay to prevent rapid checking
      return;     // Skip processing this scan
    }

    // Additional validation to ensure it's a proper QR code
    if (isValidQRCode(qrData))
    {
      // Set processing flag to prevent interruptions
      isProcessingScan = true;
      processingStartTime = currentTime;

      // Update last scan tracking
      lastScannedCode = qrData;
      lastScanTime = currentTime;

      Serial.println("\n=== QR CODE SCANNED ===");
      Serial.println("Data: " + qrData);
      Serial.print("Scan time: ");
      printCurrentTime();

      // Check if this is first or second scan (includes time validation and expiration)
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
            // Open entry servo and wait for car to pass
            openEntryServo();
          }
        }
        else
        {
          Serial.println("👋 SECOND SCAN - Recording exit time");
          if (updateTimestamp(qrData, false))
          {
            Serial.println("✅ Exit recorded successfully");
            // Open exit servo and wait for car to pass
            openExitServo();
          }
        }
      }
      else
      {
        Serial.println("❌ Invalid QR code, outside time window, expired, or already used twice");
      }

      Serial.println("=== SCAN COMPLETE ===\n");

      // Clear processing flag
      isProcessingScan = false;

      // Extended delay after successful processing to prevent accidental rescans
      delay(3000);
    }
  }

  // Small delay to prevent CPU overload
  delay(100);
}
