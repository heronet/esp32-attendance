#include <Adafruit_Fingerprint.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <time.h>

// WiFi credentials
const char *ssid = "Galaxy Note10+ 5G";
const char *password = "00000000";

// Google Script Deployment ID
const char *GScriptId = "AKfycbzntnBKPrP4zbYOWySvgm__OA4qExmtVHdiECoPXcAaBDUOblft7xTi4-2mCL1ZCobb6g";

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
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

// Function prototypes
void initSPIFFS();
void syncToGoogle();
void saveAttendanceToFile(String studentId, String userName);
String getCurrentDate();
String getCurrentTime();
void showMainMenu();

uint8_t readnumber()
{
  uint8_t num = 0;
  while (num == 0)
  {
    while (!Serial.available())
      ;
    num = Serial.parseInt();
  }
  return num;
}

void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // Check if attendance file exists, if not create it with headers
  if (!SPIFFS.exists(attendanceFilePath))
  {
    File file = SPIFFS.open(attendanceFilePath, FILE_WRITE);
    if (!file)
    {
      Serial.println("Failed to create file");
      return;
    }
    // Write CSV headers
    file.println("date,time,student_id,student_name,status,synced");
    file.close();
    Serial.println("Created attendance file with headers");
  }
  else
  {
    Serial.println("Attendance file exists");
  }
}

String getCurrentDate()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return "0000-00-00"; // Default date if time sync fails
  }

  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
  return String(dateStr);
}

String getCurrentTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return "00:00:00"; // Default time if time sync fails
  }

  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  return String(timeStr);
}

void saveAttendanceToFile(String studentId, String userName)
{
  String date = getCurrentDate();
  String time = getCurrentTime();

  File file = SPIFFS.open(attendanceFilePath, FILE_APPEND);
  if (!file)
  {
    Serial.println("Failed to open file for appending");
    return;
  }

  // Format: date,time,student_id,student_name,status,synced
  String record = date + "," + time + "," + studentId + "," + userName + ",present,0";
  file.println(record);
  file.close();

  Serial.println("Saved attendance record to file: " + record);
}

void syncToGoogle()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected. Cannot sync to Google Sheets.");
    return;
  }

  File file = SPIFFS.open(attendanceFilePath, FILE_READ);
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  // Read the header line and discard
  String header = file.readStringUntil('\n');

  // Temporary file to store synced records
  File tempFile = SPIFFS.open("/temp.csv", FILE_WRITE);
  if (!tempFile)
  {
    Serial.println("Failed to create temp file");
    file.close();
    return;
  }

  // Write the header to temp file
  tempFile.println(header);

  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure(); // Ignore SSL certificate validation
  String fullUrl = "https://" + String(host) + url;

  int syncCount = 0;

  // Read each line from the file
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

    // Only sync records that haven't been synced yet
    if (synced.toInt() == 0)
    {
      http.begin(client, fullUrl);
      http.addHeader("Content-Type", "application/json");

      // Build the payload
      String payload = "{\"command\": \"column_attendance\", \"sheet_name\": \"Attendance\", "
                       "\"student_id\": \"" +
                       studentId + "\", "
                                   "\"student_name\": \"" +
                       studentName + "\", "
                                     "\"date\": \"" +
                       date + "\", "
                              "\"time\": \"" +
                       time + "\", "
                              "\"status\": \"" +
                       status + "\"}";

      Serial.println("Publishing attendance data to Google Sheets...");
      Serial.println(payload);

      int httpResponseCode = http.POST(payload);
      if (httpResponseCode > 0)
      {
        String response = http.getString();
        Serial.println("HTTP Response code: " + String(httpResponseCode));
        Serial.println("Response: " + response);

        // Mark as synced
        tempFile.println(date + "," + time + "," + studentId + "," + studentName + "," + status + ",1");
        syncCount++;
      }
      else
      {
        Serial.println("Error publishing data. HTTP Response code: " + String(httpResponseCode));
        // Keep as unsynced
        tempFile.println(line);
      }

      http.end();
    }
    else
    {
      // Already synced, just copy to temp file
      tempFile.println(line);
    }
  }

  file.close();
  tempFile.close();

  // Replace the original file with the temp file
  SPIFFS.remove(attendanceFilePath);
  SPIFFS.rename("/temp.csv", attendanceFilePath);

  Serial.println("Sync completed. " + String(syncCount) + " records synced.");
}

uint8_t getFingerprintEnroll(uint8_t id)
{
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.println(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  p = finger.image2Tz(1);
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    Serial.println("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    return p;
  default:
    Serial.println("Unknown error");
    return p;
  }

  Serial.println("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER)
  {
    p = finger.getImage();
  }

  Serial.println("Place same finger again");
  p = -1;
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.print(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  p = finger.image2Tz(2);
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    Serial.println("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    return p;
  default:
    Serial.println("Unknown error");
    return p;
  }

  p = finger.createModel();
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Prints matched!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_ENROLLMISMATCH)
  {
    Serial.println("Fingerprints did not match");
    return p;
  }
  else
  {
    Serial.println("Unknown error");
    return p;
  }

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Stored!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not store in that location");
    return p;
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    return p;
  }
  else
  {
    Serial.println("Unknown error");
    return p;
  }

  return true;
}

void enrollFingerprint()
{
  Serial.println("Ready to enroll a fingerprint!");
  Serial.println("Please type in the ID # (from 1 to 127) you want to save this finger as...");
  uint8_t id = readnumber();
  if (id == 0)
  { // ID #0 not allowed
    return;
  }
  Serial.print("Enrolling ID #");
  Serial.println(id);

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
    return -1;

  Serial.print("Found ID #");
  Serial.print(finger.fingerID);
  Serial.print(" with confidence of ");
  Serial.println(finger.confidence);
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
    Serial.println("Unknown fingerprint ID");
    return;
  }

  // Save attendance to local file
  saveAttendanceToFile(studentId, userName);

  Serial.print("Welcome ");
  Serial.println(userName);

  delay(2000); // Show the welcome message for 2 seconds
}

void viewStoredRecords()
{
  File file = SPIFFS.open(attendanceFilePath, FILE_READ);
  if (!file)
  {
    Serial.println("Failed to open attendance file");
    return;
  }

  Serial.println("\n--- Stored Attendance Records ---");

  // Read and print all lines
  while (file.available())
  {
    String line = file.readStringUntil('\n');
    Serial.println(line);
  }

  file.close();
  Serial.println("--- End of Records ---\n");
}

void enrollMode()
{
  Serial.println("Entering Enroll Mode...");
  Serial.println("Follow instructions on serial monitor");

  while (true)
  {
    enrollFingerprint();

    Serial.println("\nEnrollment options:");
    Serial.println("1. Enroll another fingerprint");
    Serial.println("2. Return to main menu");

    while (!Serial.available())
      ;
    char option = Serial.read();

    if (option == '2')
    {
      break;
    }
  }
}

void attendanceMode()
{
  Serial.println("Entering Attendance Mode...");
  Serial.println("Place Finger...");

  while (true)
  {
    // Wait for a fingerprint to be detected
    int fingerprintID = -1;
    while (fingerprintID == -1)
    {
      fingerprintID = getFingerprintID();
      delay(50); // Add a small delay to avoid spamming the sensor

      // Check if there's a request to exit
      if (Serial.available())
      {
        char cmd = Serial.read();
        if (cmd == 'x' || cmd == 'X')
        {
          Serial.println("Exiting Attendance Mode...");
          return;
        }
      }
    }

    // Fingerprint found, add attendance
    addAttendance(fingerprintID);
    delay(2000); // Delay before next scan
    Serial.println("Place Finger... (Press 'X' to exit)");
  }
}

void clearAllFingerprints()
{
  Serial.println("Are you sure you want to clear all fingerprints? (Y/N)");

  while (!Serial.available())
    ;
  char confirmation = Serial.read();
  if (confirmation == 'Y' || confirmation == 'y')
  {
    Serial.println("Clearing all fingerprints...");

    uint8_t p = finger.emptyDatabase();
    if (p == FINGERPRINT_OK)
    {
      Serial.println("All fingerprints cleared successfully!");
    }
    else
    {
      Serial.println("Failed to clear fingerprints.");
    }
  }
  else
  {
    Serial.println("Clear operation canceled.");
  }
  delay(2000);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("System initialized");

  // Initialize SPIFFS
  initSPIFFS();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.println(" ...");

  int wifiCounter = 0;
  while (WiFi.status() != WL_CONNECTED && wifiCounter < 20) // Timeout after 20 seconds
  {
    delay(1000);
    Serial.print(".");
    wifiCounter++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println('\n');
    Serial.println("Connection established!");
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP());

    // Initialize and sync time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Time synchronized");
  }
  else
  {
    Serial.println('\n');
    Serial.println("WiFi connection failed! Continuing in offline mode...");
  }
  delay(2000);

  // Initialize fingerprint sensor
  Serial.println("Initializing sensor...");

  finger.begin(57600);
  if (finger.verifyPassword())
  {
    Serial.println("Found fingerprint sensor!");
  }
  else
  {
    Serial.println("Did not find fingerprint sensor :(");
    while (1)
    {
      delay(1000);
    }
  }
  delay(2000);

  finger.getTemplateCount();
  Serial.print("Stored Prints: ");
  Serial.println(finger.templateCount);

  if (finger.templateCount == 0)
  {
    Serial.println("Sensor doesn't contain any fingerprint data. Please enroll a fingerprint.");
  }
  else
  {
    Serial.print("Sensor contains ");
    Serial.print(finger.templateCount);
    Serial.println(" templates");
  }
  delay(2000);

  // Prompt user to select mode
  showMainMenu();
}

void showMainMenu()
{
  Serial.println("\n=== Attendance System Menu ===");
  Serial.println("1. Enroll Mode");
  Serial.println("2. Attendance Mode");
  Serial.println("3. Clear All Fingerprints");
  Serial.println("4. View Stored Records");
  Serial.println("5. Sync to Google Sheets");
  Serial.println("==============================");
}

void loop()
{
  // Wait for user input to select mode
  if (Serial.available())
  {
    char mode = Serial.read();
    if (mode == '1')
    {
      enrollMode();
      showMainMenu();
    }
    else if (mode == '2')
    {
      attendanceMode();
      showMainMenu();
    }
    else if (mode == '3')
    {
      clearAllFingerprints();
      showMainMenu();
    }
    else if (mode == '4')
    {
      viewStoredRecords();
      showMainMenu();
    }
    else if (mode == '5')
    {
      Serial.println("Syncing data to Google Sheets...");
      syncToGoogle();
      showMainMenu();
    }
    else
    {
      Serial.println("Invalid choice. Please enter 1-5.");
    }
  }
}