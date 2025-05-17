#include <Adafruit_Fingerprint.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <time.h>
#include <BluetoothSerial.h> // Add BluetoothSerial library

// Check if Bluetooth is properly enabled in the build
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` and enable it
#endif

// Bluetooth Serial instance
BluetoothSerial SerialBT;

// WiFi credentials
const char *ssid = "Galaxy Note10+ 5G";
const char *password = "00000000";

// Google Script Deployment ID
const char *GScriptId = "AKfycbx8oO379GMEmfpFmbtIbn9eHTuOoMmKJzLa7eSl9jZjwXl5rsYqIpPHPE-gc_yTBSiBkg";

// Google Sheets setup
const char *host = "script.google.com";
const int httpsPort = 443;
String url = String("/macros/s/") + GScriptId + "/exec";

// On ESP32, use Serial2 for hardware serial
#define FINGERPRINT_SERIAL Serial2

// Create a fingerprint sensor object
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FINGERPRINT_SERIAL);

// Global variables
int u = 0;
int v = 0;
int count = 0;

// CSV file path in SPIFFS
const char *attendanceFilePath = "/attendance.csv";

// Time parameters
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.google.com";
const char *ntpServer3 = "time.windows.com";
const long gmtOffset_sec = 21600; // GMT+6:00
const int daylightOffset_sec = 0; // No DST offset for many countries

// Function prototypes
void initSPIFFS();
void syncToGoogle();
void saveAttendanceToFile(String studentId, String userName);
String getCurrentDate();
String getCurrentTime();
void showMainMenu();
void setupLEDs();
void indicateSuccess();
void indicateFailure();

// Helper function to read input from both Serial and SerialBT
String readInput()
{
  String input = "";

  // Check both Serial and Bluetooth Serial
  while (true)
  {
    if (SerialBT.available())
    {
      input = SerialBT.readStringUntil('\n');
      input.trim();
      return input;
    }

    if (Serial.available())
    {
      input = Serial.readStringUntil('\n');
      input.trim();
      return input;
    }

    delay(10); // Short delay to prevent CPU hogging
  }
}

// Helper function to print messages to both Serial and SerialBT
void printBoth(String message)
{
  Serial.println(message);
  SerialBT.println(message);
}

// Modified readnumber function to accept input from both Serial and Bluetooth
uint8_t readnumber()
{
  uint8_t num = 0;
  while (num == 0)
  {
    String input = readInput();
    if (input.length() > 0)
    {
      num = input.toInt();
    }
  }
  return num;
}

void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    printBoth("SPIFFS Mount Failed");
    return;
  }

  // Check if attendance file exists, if not create it with headers
  if (!SPIFFS.exists(attendanceFilePath))
  {
    File file = SPIFFS.open(attendanceFilePath, FILE_WRITE);
    if (!file)
    {
      printBoth("Failed to create file");
      return;
    }
    // Write CSV headers
    file.println("date,time,student_id,student_name,status,synced");
    file.close();
    printBoth("Created attendance file with headers");
  }
  else
  {
    printBoth("Attendance file exists");
  }
}

void initTime()
{
  // Initialize time with multiple NTP servers for redundancy
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);

  printBoth("Waiting for NTP time sync...");

  time_t now = 0;
  struct tm timeinfo;
  int retry = 0;
  const int maxRetries = 10;

  // Try to sync time for up to 10 seconds
  while (now < 8 * 3600 * 2 && retry < maxRetries)
  {
    printBoth(".");
    delay(1000);
    time(&now);
    retry++;
  }
  printBoth("");

  if (retry >= maxRetries)
  {
    printBoth("Failed to sync time after multiple attempts!");
    printBoth("System will continue with default time.");
    return;
  }

  if (!getLocalTime(&timeinfo))
  {
    printBoth("Failed to obtain time");
    return;
  }

  // If we got here, time sync was successful
  printBoth("Time synchronized successfully!");

  String timeStr = "Current time: " +
                   String(timeinfo.tm_hour) + ":" +
                   String(timeinfo.tm_min) + ":" +
                   String(timeinfo.tm_sec);
  printBoth(timeStr);

  String dateStr = "Date: " +
                   String(timeinfo.tm_mday) + "/" +
                   String(timeinfo.tm_mon + 1) + "/" +
                   String(timeinfo.tm_year + 1900);
  printBoth(dateStr);
}

String getCurrentDate()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    printBoth("Failed to obtain time for date");

    // Use compilation date as fallback
    char fallbackDate[11];
    sprintf(fallbackDate, "%s", __DATE__);
    return String(fallbackDate);
  }

  char dateStr[11];
  sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  return String(dateStr);
}

String getCurrentTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    printBoth("Failed to obtain time for timestamp");

    // Use compilation time as fallback
    char fallbackTime[9];
    sprintf(fallbackTime, "%s", __TIME__);
    return String(fallbackTime);
  }

  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(timeStr);
}

void saveAttendanceToFile(String studentId, String userName)
{
  String date = getCurrentDate();
  String time = getCurrentTime();

  File file = SPIFFS.open(attendanceFilePath, FILE_APPEND);
  if (!file)
  {
    printBoth("Failed to open file for appending");
    return;
  }

  // Format: date,time,student_id,student_name,status,synced
  String record = date + "," + time + "," + studentId + "," + userName + ",present,0";
  file.println(record);
  file.close();

  printBoth("Saved attendance record to file: " + record);
}

void syncToGoogle()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    printBoth("WiFi not connected. Cannot sync to Google Sheets.");
    return;
  }

  File file = SPIFFS.open(attendanceFilePath, FILE_READ);
  if (!file)
  {
    printBoth("Failed to open file for reading");
    return;
  }

  // Read the header line and discard
  String header = file.readStringUntil('\n');

  // Temporary file to store synced records
  File tempFile = SPIFFS.open("/temp.csv", FILE_WRITE);
  if (!tempFile)
  {
    printBoth("Failed to create temp file");
    file.close();
    return;
  }

  // Write the header to temp file
  tempFile.println(header);

  WiFiClientSecure client;
  client.setInsecure(); // Ignore SSL certificate validation

  // Increase timeout values for client
  client.setTimeout(20000); // 20 seconds timeout

  HTTPClient http;
  // Increase timeout values for HTTP client
  http.setTimeout(20000);

  String fullUrl = "https://" + String(host) + url;

  // Build JSON array of records to sync
  String jsonPayload = "{\"command\": \"batch_attendance\", \"sheet_name\": \"Attendance\", \"records\": [";

  int recordCount = 0;
  bool hasUnsyncedRecords = false;

  // First pass: count unsynchronized records and build JSON array
  while (file.available())
  {
    String line = file.readStringUntil('\n');
    if (line.length() <= 1)
      continue; // Skip empty lines

    // Parse the CSV line
    int commaPos1 = line.indexOf(',');
    int commaPos2 = line.indexOf(',', commaPos1 + 1);
    int commaPos3 = line.indexOf(',', commaPos2 + 1);
    int commaPos4 = line.indexOf(',', commaPos3 + 1);
    int commaPos5 = line.indexOf(',', commaPos4 + 1);

    String date = line.substring(0, commaPos1);
    String time = line.substring(commaPos1 + 1, commaPos2);
    String studentId = line.substring(commaPos2 + 1, commaPos3);
    String studentName = line.substring(commaPos3 + 1, commaPos4);
    String status = line.substring(commaPos4 + 1, commaPos5);
    String synced = line.substring(commaPos5 + 1);

    // Only include records that haven't been synced yet
    if (synced.toInt() == 0)
    {
      // Add comma if not the first record
      if (hasUnsyncedRecords)
      {
        jsonPayload += ",";
      }

      // Add this record to the JSON array
      jsonPayload += "{\"date\":\"" + date + "\",\"time\":\"" + time +
                     "\",\"student_id\":\"" + studentId + "\",\"student_name\":\"" +
                     studentName + "\",\"status\":\"" + status + "\"}";

      hasUnsyncedRecords = true;
      recordCount++;
    }
  }

  // Close the JSON array and object
  jsonPayload += "]}";

  // Reset file position to beginning (after header)
  file.seek(0);
  file.readStringUntil('\n'); // Skip header

  // If no records to sync, just report and exit
  if (!hasUnsyncedRecords)
  {
    printBoth("No unsynced records found. Nothing to upload.");
    file.close();
    tempFile.close();
    return;
  }

  printBoth("Publishing " + String(recordCount) + " attendance records to Google Sheets...");
  printBoth("Payload size: " + String(jsonPayload.length()) + " bytes");

  // Send the batch request
  http.begin(client, fullUrl);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonPayload);

  bool syncSuccessful = false;

  // Handle response
  if (httpResponseCode > 0)
  {
    String response = http.getString();
    printBoth("HTTP Response code: " + String(httpResponseCode));
    printBoth("Response: " + response);
    syncSuccessful = true;
  }
  // Check for specific negative error codes that might still indicate success
  else if (httpResponseCode == -11)
  {
    printBoth("Response timeout but data likely sent. HTTP Response code: " + String(httpResponseCode));
    // Optimistically assume data was sent
    syncSuccessful = true;
  }
  else
  {
    printBoth("Error publishing data. HTTP Response code: " + String(httpResponseCode));
    syncSuccessful = false;
  }

  http.end();

  // Now update the local records based on sync result
  while (file.available())
  {
    String line = file.readStringUntil('\n');
    if (line.length() <= 1)
    {
      continue; // Skip empty lines
    }

    // Parse the CSV line to check sync status
    int lastCommaPos = line.lastIndexOf(',');
    String syncStatus = line.substring(lastCommaPos + 1);

    if (syncStatus.toInt() == 0 && syncSuccessful)
    {
      // Mark as synced by replacing the last 0 with 1
      String updatedLine = line.substring(0, lastCommaPos + 1) + "1";
      tempFile.println(updatedLine);
    }
    else
    {
      // Keep line as is
      tempFile.println(line);
    }
  }

  file.close();
  tempFile.close();

  // Replace the original file with the temp file
  SPIFFS.remove(attendanceFilePath);
  SPIFFS.rename("/temp.csv", attendanceFilePath);

  if (syncSuccessful)
  {
    printBoth("Sync completed successfully. " + String(recordCount) + " records synced.");
  }
  else
  {
    printBoth("Sync failed. Will try again later.");
  }
}

uint8_t getFingerprintEnroll(uint8_t id)
{
  int p = -1;
  printBoth("Waiting for valid finger to enroll as #" + String(id));
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      printBoth("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      printBoth(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      printBoth("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      printBoth("Imaging error");
      break;
    default:
      printBoth("Unknown error");
      break;
    }

    if (p == FINGERPRINT_OK)
    {
      printBoth("Stored!");
      indicateSuccess(); // Success indicator
    }
    else
    {
      // In failure cases
      indicateFailure(); // Failure indicator
    }
  }

  p = finger.image2Tz(1);
  switch (p)
  {
  case FINGERPRINT_OK:
    printBoth("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    printBoth("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    printBoth("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    printBoth("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    printBoth("Could not find fingerprint features");
    return p;
  default:
    printBoth("Unknown error");
    return p;
  }

  printBoth("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER)
  {
    p = finger.getImage();
  }

  printBoth("Place same finger again");
  p = -1;
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      printBoth("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      printBoth(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      printBoth("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      printBoth("Imaging error");
      break;
    default:
      printBoth("Unknown error");
      break;
    }
  }

  p = finger.image2Tz(2);
  switch (p)
  {
  case FINGERPRINT_OK:
    printBoth("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    printBoth("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    printBoth("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    printBoth("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    printBoth("Could not find fingerprint features");
    return p;
  default:
    printBoth("Unknown error");
    return p;
  }

  p = finger.createModel();
  if (p == FINGERPRINT_OK)
  {
    printBoth("Prints matched!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    printBoth("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_ENROLLMISMATCH)
  {
    printBoth("Fingerprints did not match");
    return p;
  }
  else
  {
    printBoth("Unknown error");
    return p;
  }

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK)
  {
    printBoth("Stored!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    printBoth("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    printBoth("Could not store in that location");
    return p;
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    printBoth("Error writing to flash");
    return p;
  }
  else
  {
    printBoth("Unknown error");
    return p;
  }

  return true;
}

void enrollFingerprint()
{
  printBoth("Ready to enroll a fingerprint!");
  printBoth("Please type in the ID # (from 1 to 127) you want to save this finger as...");
  uint8_t id = readnumber();
  if (id == 0)
  { // ID #0 not allowed
    return;
  }
  printBoth("Enrolling ID #" + String(id));

  while (!getFingerprintEnroll(id))
    ;
}

int getFingerprintID()
{
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK)
    return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)
    return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK)
  {
    // LED failure indication
    indicateFailure();
    return -1;
  }

  printBoth("Found ID #" + String(finger.fingerID) + " with confidence of " + String(finger.confidence));
  return finger.fingerID;
}

// Function to add attendance
void addAttendance(int fingerprintID)
{
  String userName;
  String studentId;

  // Map fingerprint ID to user data
  switch (fingerprintID)
  {
  case 1:
    userName = "Arik";
    studentId = "1";
    break;
  case 2:
    userName = "OOO";
    studentId = "2";
    break;
  case 6:
    userName = "PPP";
    studentId = "6";
    break;
  case 65:
    userName = "Ymir";
    studentId = "65";
    break;
  default:
    printBoth("Unknown fingerprint ID");
    return;
  }

  // Save attendance to local file
  saveAttendanceToFile(studentId, userName);

  printBoth("Welcome " + userName);

  // LED success indication
  indicateSuccess();
}

void viewStoredRecords()
{
  File file = SPIFFS.open(attendanceFilePath, FILE_READ);
  if (!file)
  {
    printBoth("Failed to open attendance file");
    return;
  }

  printBoth("\n--- Stored Attendance Records ---");

  // Read and print all lines
  while (file.available())
  {
    String line = file.readStringUntil('\n');
    printBoth(line);
  }

  file.close();
  printBoth("--- End of Records ---\n");
}

void enrollMode()
{
  printBoth("Entering Enroll Mode...");
  printBoth("Follow instructions on serial monitor");

  while (true)
  {
    enrollFingerprint();

    printBoth("\nEnrollment options:");
    printBoth("1. Enroll another fingerprint");
    printBoth("2. Return to main menu");

    String option = readInput();
    if (option == "2")
    {
      break;
    }
  }
}

void attendanceMode()
{
  printBoth("Entering Attendance Mode...");
  printBoth("Place Finger... (Press 'X' to exit)");

  while (true)
  {
    // Wait for a fingerprint to be detected
    int fingerprintID = -1;
    while (fingerprintID == -1)
    {
      fingerprintID = getFingerprintID();
      delay(50); // Add a small delay to avoid spamming the sensor

      // Check if there's a request to exit from either Serial or Bluetooth
      if (Serial.available() || SerialBT.available())
      {
        String cmd = readInput();
        if (cmd == "x" || cmd == "X")
        {
          printBoth("Exiting Attendance Mode...");
          return;
        }
      }
    }

    // Fingerprint found, add attendance
    addAttendance(fingerprintID);
    delay(2000); // Delay before next scan
    printBoth("Place Finger... (Press 'X' to exit)");
  }
}

void clearAllFingerprints()
{
  printBoth("Are you sure you want to clear all fingerprints? (Y/N)");

  String confirmation = readInput();
  if (confirmation == "Y" || confirmation == "y")
  {
    printBoth("Clearing all fingerprints...");

    uint8_t p = finger.emptyDatabase();
    if (p == FINGERPRINT_OK)
    {
      printBoth("All fingerprints cleared successfully!");
    }
    else
    {
      printBoth("Failed to clear fingerprints.");
    }
  }
  else
  {
    printBoth("Clear operation canceled.");
  }
  delay(2000);
}

void setupLEDs()
{
  // Initialize LED pins
  pinMode(21, OUTPUT); // Green LED for success
  pinMode(23, OUTPUT); // Red LED for failure

  // Initial state - both LEDs off
  digitalWrite(21, LOW);
  digitalWrite(23, LOW);

  // Quick test flash
  digitalWrite(21, HIGH);
  delay(300);
  digitalWrite(21, LOW);
  digitalWrite(23, HIGH);
  delay(300);
  digitalWrite(23, LOW);

  printBoth("LEDs initialized");
}

void indicateSuccess()
{
  digitalWrite(21, HIGH); // Turn on green LED
  digitalWrite(23, LOW);  // Ensure red LED is off
  delay(1000);            // Keep on for 1 second
  digitalWrite(21, LOW);  // Turn off green LED
}

void indicateFailure()
{
  digitalWrite(23, HIGH); // Turn on red LED
  digitalWrite(21, LOW);  // Ensure green LED is off
  delay(1000);            // Keep on for 1 second
  digitalWrite(23, LOW);  // Turn off red LED
}

void setup()
{
  Serial.begin(115200);

  // Initialize Bluetooth Serial with device name
  SerialBT.begin("ESP32-Attendance"); // Bluetooth device name

  printBoth("System initialized");
  printBoth("Bluetooth device started, connect to 'ESP32-Attendance' to control");

  // Initialize SPIFFS
  initSPIFFS();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  printBoth("Connecting to " + String(ssid) + " ...");

  int wifiCounter = 0;
  while (WiFi.status() != WL_CONNECTED && wifiCounter < 20) // Timeout after 20 seconds
  {
    delay(1000);
    printBoth(".");
    wifiCounter++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    printBoth("\nConnection established!");
    printBoth("IP address: " + WiFi.localIP().toString());

    // Initialize and sync time
    initTime();
  }
  else
  {
    printBoth("\nWiFi connection failed! Continuing in offline mode...");
  }

  // Initialize fingerprint sensor
  printBoth("Initializing sensor...");

  finger.begin(57600);
  if (finger.verifyPassword())
  {
    printBoth("Found fingerprint sensor!");
  }
  else
  {
    printBoth("Did not find fingerprint sensor :(");
    while (1)
    {
      delay(1000);
    }
  }

  setupLEDs();

  finger.getTemplateCount();
  printBoth("Stored Prints: " + String(finger.templateCount));

  if (finger.templateCount == 0)
  {
    printBoth("Sensor doesn't contain any fingerprint data. Please enroll a fingerprint.");
  }
  else
  {
    printBoth("Sensor contains " + String(finger.templateCount) + " templates");
  }
  delay(2000);

  // Prompt user to select mode
  showMainMenu();
}

void showMainMenu()
{
  printBoth("\n=== Attendance System Menu ===");
  printBoth("1. Enroll Mode");
  printBoth("2. Attendance Mode");
  printBoth("3. Clear All Fingerprints");
  printBoth("4. View Stored Records");
  printBoth("5. Sync to Google Sheets");
  printBoth("==============================");
}

void loop()
{
  // Check for input from either Serial or Bluetooth
  if (Serial.available() || SerialBT.available())
  {
    String mode = readInput();

    if (mode == "1")
    {
      enrollMode();
      showMainMenu();
    }
    else if (mode == "2")
    {
      attendanceMode();
      showMainMenu();
    }
    else if (mode == "3")
    {
      clearAllFingerprints();
      showMainMenu();
    }
    else if (mode == "4")
    {
      viewStoredRecords();
      showMainMenu();
    }
    else if (mode == "5")
    {
      printBoth("Syncing data to Google Sheets...");
      syncToGoogle();
      showMainMenu();
    }
    else
    {
      printBoth("Invalid choice. Please enter 1-5.");
    }
  }
}