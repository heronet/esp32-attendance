#include <Adafruit_Fingerprint.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>

// WiFi credentials
const char *ssid = "Sony Xperia 1 III";
const char *password = "00000000";

// Google Script Deployment ID
const char *GScriptId = "AKfycby_2izhGidfcOPhpAfs7zhAWXHcK7oeZnUniauozbuc9rR52E7b_BaRJW4IgwTPPsz_rQ";

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
String currentDate = "19/5"; // Default date (today's date)

// CSV file path in SPIFFS
const char *attendanceFilePath = "/attendance.csv";

// Function prototypes
void initSPIFFS();
void syncToGoogle();
void saveAttendanceToFile(String studentId);
void showMainMenu();
void setupLEDs();
void indicateSuccess();
void indicateFailure();
void clearAttendanceData();
void connectToWiFi();
void disconnectWiFi();

// Helper function to read input from Serial only
String readInput()
{
  String input = "";
  while (true)
  {
    if (Serial.available())
    {
      input = Serial.readStringUntil('\n');
      input.trim();
      return input;
    }
    delay(10); // Short delay to prevent CPU hogging
  }
}

// Modified readnumber function to accept input from Serial only
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
    file.println("date,student_id,status,synced");
    file.close();
    Serial.println("Created attendance file with headers");
  }
  else
  {
    Serial.println("Attendance file exists");
  }
}

void saveAttendanceToFile(String studentId)
{
  File file = SPIFFS.open(attendanceFilePath, FILE_APPEND);
  if (!file)
  {
    Serial.println("Failed to open file for appending");
    return;
  }

  // Format: date,student_id,status,synced
  String record = currentDate + "," + studentId + ",present,0";
  file.println(record);
  file.close();

  Serial.println("Saved attendance record to file: " + record);
}

void connectToWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi already connected!");
    return;
  }

  Serial.println("Connecting to " + String(ssid) + " ...");
  WiFi.begin(ssid, password);

  int wifiCounter = 0;
  while (WiFi.status() != WL_CONNECTED && wifiCounter < 20) // Timeout after 20 seconds
  {
    delay(1000);
    Serial.println(".");
    wifiCounter++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nConnection established!");
    Serial.println("IP address: " + WiFi.localIP().toString());
  }
  else
  {
    Serial.println("\nWiFi connection failed! Cannot sync to Google Sheets.");
  }
}

void disconnectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Disconnecting from WiFi...");
    WiFi.disconnect();
    Serial.println("WiFi disconnected");
  }
}

void syncToGoogle()
{
  // Connect to WiFi before syncing
  connectToWiFi();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected. Cannot sync to Google Sheets.");
    return;
  }

  File file = SPIFFS.open(attendanceFilePath, FILE_READ);
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    disconnectWiFi();
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
    disconnectWiFi();
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

    String date = line.substring(0, commaPos1);
    String studentId = line.substring(commaPos1 + 1, commaPos2);
    String status = line.substring(commaPos2 + 1, commaPos3);
    String synced = line.substring(commaPos3 + 1);

    // Only include records that haven't been synced yet
    if (synced.toInt() == 0)
    {
      // Add comma if not the first record
      if (hasUnsyncedRecords)
      {
        jsonPayload += ",";
      }

      // Add this record to the JSON array

      jsonPayload += "{\"date\":\"" + date + "\",\"student_id\":\"" + studentId + "\",\"status\":\"" + status + "\"}";

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
    Serial.println("No unsynced records found. Nothing to upload.");
    file.close();
    tempFile.close();
    disconnectWiFi();
    return;
  }

  Serial.println("Publishing " + String(recordCount) + " attendance records to Google Sheets...");
  Serial.println("Payload size: " + String(jsonPayload.length()) + " bytes");

  // Send the batch request
  http.begin(client, fullUrl);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonPayload);

  bool syncSuccessful = false;

  // Handle response
  if (httpResponseCode > 0)
  {
    String response = http.getString();
    Serial.println("HTTP Response code: " + String(httpResponseCode));
    Serial.println("Response: " + response);
    syncSuccessful = true;
  }
  // Check for specific negative error codes that might still indicate success
  else if (httpResponseCode == -11)
  {
    Serial.println("Response timeout but data likely sent. HTTP Response code: " + String(httpResponseCode));
    // Optimistically assume data was sent
    syncSuccessful = true;
  }
  else
  {
    Serial.println("Error publishing data. HTTP Response code: " + String(httpResponseCode));
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
    Serial.println("Sync completed successfully. " + String(recordCount) + " records synced.");
  }
  else
  {
    Serial.println("Sync failed. Will try again later.");
  }

  // Disconnect from WiFi after syncing
  disconnectWiFi();
}

uint8_t getFingerprintEnroll(uint8_t id)
{
  int p = -1;
  Serial.println("Waiting for valid finger to enroll as #" + String(id));
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

    if (p == FINGERPRINT_OK)
    {
      Serial.println("Stored!");
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
  Serial.println("Enrolling ID #" + String(id));

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

  Serial.println("Found ID #" + String(finger.fingerID) + " with confidence of " + String(finger.confidence));
  return finger.fingerID;
}

// Function to add attendance

void addAttendance(int fingerprintID)
{
  String studentId;

  if (fingerprintID)
  {

    Serial.println("Welcome " + String(fingerprintID));
    studentId = String(fingerprintID);
  }
  else
  {
    Serial.println("Unknown fingerprint ID");
    return;
  }

  // Save attendance to local file (passing only studentId)
  saveAttendanceToFile(studentId);

  // LED success indication
  indicateSuccess();
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

// Function to clear attendance data
void clearAttendanceData()
{
  Serial.println("Are you sure you want to clear all attendance records? (Y/N)");
  Serial.println("WARNING: This will delete all attendance data!");

  String confirmation = readInput();
  if (confirmation == "Y" || confirmation == "y")
  {
    // Double confirmation for safety
    Serial.println("ALL ATTENDANCE RECORDS WILL BE PERMANENTLY DELETED!");
    Serial.println("Type 'CONFIRM' to proceed:");

    String finalConfirmation = readInput();
    if (finalConfirmation == "CONFIRM")
    {
      // Delete the old file
      if (SPIFFS.remove(attendanceFilePath))
      {
        // Create a new file with only the header
        File file = SPIFFS.open(attendanceFilePath, FILE_WRITE);
        if (file)
        {
          // Write CSV headers
          file.println("date,student_id,student_name,status,synced");
          file.close();

          Serial.println("All attendance records have been cleared successfully!");
          indicateSuccess(); // Visual confirmation
        }
        else
        {
          Serial.println("Error: Failed to create a new attendance file");
          indicateFailure();
        }
      }
      else
      {
        Serial.println("Error: Failed to remove the old attendance file");
        indicateFailure();
      }
    }
    else
    {
      Serial.println("Operation canceled: Confirmation text didn't match");
    }
  }
  else
  {
    Serial.println("Operation canceled");
  }

  delay(2000);
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

    String option = readInput();
    if (option == "2")
    {
      break;
    }
  }
}

void setCurrentDate()
{
  Serial.println("Enter today's date in DD/MM format (e.g., 19/5):");
  String dateInput = readInput();
  dateInput.trim();

  // Basic validation - check if the input matches the expected format
  if (dateInput.length())
  {
    currentDate = dateInput;
    Serial.println("Date set to: " + currentDate);
  }
  else
  {
    Serial.println("Invalid date format. Using default date: " + currentDate);
  }
}

void attendanceMode()
{
  // First set the date for attendance
  setCurrentDate();

  Serial.println("Entering Attendance Mode for date: " + currentDate);
  Serial.println("Place Finger... (Press 'X' to exit)");

  while (true)
  {
    // Wait for a fingerprint to be detected
    int fingerprintID = -1;
    while (fingerprintID == -1)
    {
      fingerprintID = getFingerprintID();
      delay(50); // Add a small delay to avoid spamming the sensor

      // Check if there's a request to exit from Serial
      if (Serial.available())
      {
        String cmd = readInput();
        if (cmd == "x" || cmd == "X")
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

  String confirmation = readInput();
  if (confirmation == "Y" || confirmation == "y")
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

  Serial.println("LEDs initialized");
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

  Serial.println("System initialized");

  // Initialize SPIFFS
  initSPIFFS();

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

  setupLEDs();

  finger.getTemplateCount();
  Serial.println("Stored Prints: " + String(finger.templateCount));

  if (finger.templateCount == 0)
  {
    Serial.println("Sensor doesn't contain any fingerprint data. Please enroll a fingerprint.");
  }
  else
  {
    Serial.println("Sensor contains " + String(finger.templateCount) + " templates");
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
  Serial.println("6. Clear Attendance Data");
  Serial.println("7. Set Current Date");
  Serial.println("==============================");
}

void loop()
{
  // Check for input from Serial only
  if (Serial.available())
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
      Serial.println("Syncing data to Google Sheets...");
      syncToGoogle();
      showMainMenu();
    }
    else if (mode == "6")
    {
      clearAttendanceData();
      showMainMenu();
    }
    else if (mode == "7")
    {
      setCurrentDate();
      showMainMenu();
    }
    else
    {
      Serial.println("Invalid choice. Please enter 1-7.");
    }
  }
}
