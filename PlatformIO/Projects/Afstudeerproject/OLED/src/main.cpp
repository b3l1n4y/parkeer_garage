// Add these includes at the top with your other includes
#include <Arduino.h>
#include <U8g2lib.h>

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

// Add this to your setup() function after other initializations
void setupOLED()
{
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
}

// Add display functions
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

void loop()
{
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
            openEntryServo();
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
            openExitServo();
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
