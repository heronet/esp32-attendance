// This function is the main handler for HTTP requests from the ESP32
function doPost(e) {
  try {
    // Parse the JSON data from the request
    const data = JSON.parse(e.postData.contents);
    const command = data.command;

    // Route to the appropriate function based on the command
    if (command === "column_attendance") {
      return markColumnAttendance(data);
    } else if (command === "mark_attendance") {
      // Keep the old function for compatibility if needed
      return JSON.stringify({
        result: "error",
        message: "Using old attendance system",
      });
    }

    // If no valid command is found
    return ContentService.createTextOutput(
      JSON.stringify({ result: "error", message: "Invalid command" })
    ).setMimeType(ContentService.MimeType.JSON);
  } catch (error) {
    // Handle any errors
    return ContentService.createTextOutput(
      JSON.stringify({ result: "error", message: error.toString() })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// Function to mark attendance using a column-based system
function markColumnAttendance(data) {
  try {
    // Extract data from the request
    const sheetName = data.sheet_name;
    const studentId = data.student_id;
    const studentName = data.student_name;
    const status = data.status || "present";

    // Generate today's date using Apps Script
    const today = new Date();
    const formattedDate = Utilities.formatDate(
      today,
      Session.getScriptTimeZone(),
      "MM/dd/yyyy"
    );

    // Open the spreadsheet and get the sheet
    const ss = SpreadsheetApp.getActiveSpreadsheet();
    let sheet = ss.getSheetByName(sheetName);

    // Create the sheet if it doesn't exist
    if (!sheet) {
      sheet = ss.insertSheet(sheetName);
      // Initialize header row with "Student ID" and "Student Name"
      initializeSheetHeaders(sheet);
    } else {
      // Ensure the headers exist even if the sheet already exists
      ensureHeaders(sheet);
    }

    // Get all headers at once
    const lastColumn = Math.max(sheet.getLastColumn(), 2);
    const headerRange = sheet.getRange(1, 1, 1, lastColumn);
    const headerValues = headerRange.getValues()[0];

    // More robust date column search
    let dateColumn = -1;

    // Log the headers for debugging
    Logger.log("Looking for date: " + formattedDate);
    Logger.log("Headers: " + JSON.stringify(headerValues));

    // First pass: exact match
    for (let i = 0; i < headerValues.length; i++) {
      if (headerValues[i] && headerValues[i].toString() === formattedDate) {
        dateColumn = i + 1;
        Logger.log("Exact match found at column " + dateColumn);
        break;
      }
    }

    // If no exact match, try to handle date objects
    if (dateColumn === -1) {
      for (let i = 0; i < headerValues.length; i++) {
        if (headerValues[i] && headerValues[i] instanceof Date) {
          const headerDate = Utilities.formatDate(
            headerValues[i],
            Session.getScriptTimeZone(),
            "MM/dd/yyyy"
          );
          Logger.log("Comparing date object: " + headerDate);
          if (headerDate === formattedDate) {
            dateColumn = i + 1;
            Logger.log("Date object match found at column " + dateColumn);
            break;
          }
        }
      }
    }

    // If still no match, create a new column
    if (dateColumn === -1) {
      dateColumn = lastColumn + 1;
      sheet.getRange(1, dateColumn).setValue(formattedDate);
      Logger.log(
        "Created new column at " + dateColumn + " with date " + formattedDate
      );
    }

    // Find the student's row or create a new one
    let studentRow = -1;

    if (sheet.getLastRow() > 1) {
      const studentIdColumn = sheet
        .getRange(2, 1, sheet.getLastRow() - 1, 1)
        .getValues();

      for (let i = 0; i < studentIdColumn.length; i++) {
        if (
          studentIdColumn[i][0] &&
          studentIdColumn[i][0].toString() === studentId.toString()
        ) {
          studentRow = i + 2; // +2 because i is 0-based and we're skipping the header row
          break;
        }
      }
    }

    // If student doesn't exist, add a new row
    if (studentRow === -1) {
      studentRow = sheet.getLastRow() + 1;
      sheet.getRange(studentRow, 1).setValue(studentId);
      sheet.getRange(studentRow, 2).setValue(studentName);
    }

    // Mark attendance
    sheet.getRange(studentRow, dateColumn).setValue(status);

    // Format the sheet to make it more readable
    sheet.autoResizeColumns(1, dateColumn);

    // Ensure statistics columns exist and update them
    ensureStatisticColumns(sheet);
    updateAttendanceStatistics(sheet);

    // Sort the sheet by student ID
    sortSheetByStudentId(sheet);

    return ContentService.createTextOutput(
      JSON.stringify({
        result: "success",
        message:
          "Attendance marked for " + studentName + " on " + formattedDate,
        student: studentName,
        date: formattedDate,
        dateColumn: dateColumn, // Include for debugging
      })
    ).setMimeType(ContentService.MimeType.JSON);
  } catch (error) {
    Logger.log("Error: " + error.toString());
    return ContentService.createTextOutput(
      JSON.stringify({ result: "error", message: error.toString() })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// Initialize a new sheet with proper headers
function initializeSheetHeaders(sheet) {
  sheet.getRange("A1").setValue("Student ID");
  sheet.getRange("B1").setValue("Student Name");

  // Add statistics columns
  sheet.getRange("C1").setValue("Attended Days");
  sheet.getRange("D1").setValue("Percentage");

  // Make the header row bold and freeze it
  sheet.getRange("1:1").setFontWeight("bold");
  sheet.setFrozenRows(1);

  // Highlight header row for better visibility
  sheet.getRange("1:1").setBackground("#E0E0E0");
}

// Ensure headers exist in existing sheets
function ensureHeaders(sheet) {
  // Check if headers exist, if not add them
  const headerA = sheet.getRange("A1").getValue();
  const headerB = sheet.getRange("B1").getValue();

  if (headerA === "" || headerA !== "Student ID") {
    sheet.getRange("A1").setValue("Student ID");
  }

  if (headerB === "" || headerB !== "Student Name") {
    sheet.getRange("B1").setValue("Student Name");
  }

  // Make header row bold and freeze it if not already
  sheet.getRange("1:1").setFontWeight("bold");
  if (sheet.getFrozenRows() < 1) {
    sheet.setFrozenRows(1);
  }

  // Ensure statistics columns
  ensureStatisticColumns(sheet);
}

// Function to sort the sheet by Student ID (Column A)
function sortSheetByStudentId(sheet) {
  try {
    // Only sort if there are at least 2 data rows (header + at least 1 data row)
    if (sheet.getLastRow() > 2) {
      // Get the data range (excluding header)
      const range = sheet.getRange(
        2,
        1,
        sheet.getLastRow() - 1,
        sheet.getLastColumn()
      );

      // Sort by the first column (Student ID)
      range.sort({ column: 1, ascending: true });
      Logger.log("Sheet sorted by Student ID");
    }
  } catch (error) {
    Logger.log("Error during sorting: " + error.toString());
  }
}

// Simple test function that can be run from the script editor
function testColumnAttendance() {
  const testData = {
    command: "column_attendance",
    sheet_name: "Attendance",
    student_id: "35",
    student_name: "Test Student",
    status: "present",
  };

  const result = markColumnAttendance(testData);
  Logger.log(result);
}

// Function to manually update attendance statistics
function updateAllStatistics() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sheets = ss.getSheets();

  for (let i = 0; i < sheets.length; i++) {
    const sheet = sheets[i];
    // Only apply to sheets that might be attendance sheets
    if (sheet.getName().includes("Attendance")) {
      ensureStatisticColumns(sheet);
      updateAttendanceStatistics(sheet);
      Logger.log("Updated statistics for sheet: " + sheet.getName());
    }
  }

  return "Statistics updated for all attendance sheets";
}

// Add this utility function to help diagnose the issue
function inspectHeaders() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sheet = ss.getSheetByName("Attendance");

  if (!sheet) {
    Logger.log("Sheet not found");
    return;
  }

  const headerRange = sheet.getRange(1, 1, 1, sheet.getLastColumn());
  const headerValues = headerRange.getValues()[0];

  for (let i = 0; i < headerValues.length; i++) {
    Logger.log(
      "Column " +
        (i + 1) +
        ": " +
        headerValues[i] +
        " (Type: " +
        typeof headerValues[i] +
        ")"
    );
    if (headerValues[i] instanceof Date) {
      Logger.log(
        "  Date value: " +
          Utilities.formatDate(
            headerValues[i],
            Session.getScriptTimeZone(),
            "MM/dd/yyyy"
          )
      );
    }
  }
}

// Function to manually sort the sheet
function manualSortByStudentId() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sheet = ss.getSheetByName("Attendance");

  if (!sheet) {
    Logger.log("Sheet not found");
    return;
  }

  sortSheetByStudentId(sheet);
}

// Function to fix header issues in existing sheets
function fixAllSheetHeaders() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sheets = ss.getSheets();

  for (let i = 0; i < sheets.length; i++) {
    const sheet = sheets[i];
    // Only apply to sheets that might be attendance sheets
    if (sheet.getName().includes("Attendance")) {
      ensureHeaders(sheet);
      updateAttendanceStatistics(sheet);
      Logger.log("Fixed headers for sheet: " + sheet.getName());
    }
  }

  return "Headers fixed for all attendance sheets";
}

// Function to ensure statistics columns exist
function ensureStatisticColumns(sheet) {
  // Get all existing headers
  const lastColumn = sheet.getLastColumn();
  const headerRange = sheet.getRange(1, 1, 1, lastColumn);
  const headerValues = headerRange.getValues()[0];

  // Check if statistics columns already exist
  let attendedDaysCol = -1;
  let percentageCol = -1;

  for (let i = 0; i < headerValues.length; i++) {
    if (headerValues[i] === "Attended Days") {
      attendedDaysCol = i + 1;
    } else if (headerValues[i] === "Percentage") {
      percentageCol = i + 1;
    }
  }

  // If columns don't exist, create them at the end
  if (attendedDaysCol === -1) {
    const newColumn = lastColumn + 1;
    sheet.getRange(1, newColumn).setValue("Attended Days");
    sheet.getRange(1, newColumn).setFontWeight("bold");
    sheet.getRange(1, newColumn).setBackground("#E0E0E0");
    attendedDaysCol = newColumn;
  }

  if (percentageCol === -1) {
    const newColumn = attendedDaysCol + 1;
    sheet.getRange(1, newColumn).setValue("Percentage");
    sheet.getRange(1, newColumn).setFontWeight("bold");
    sheet.getRange(1, newColumn).setBackground("#E0E0E0");
    percentageCol = newColumn;
  }

  return { attendedDaysCol, percentageCol };
}

// Function to update attendance statistics for all students
function updateAttendanceStatistics(sheet) {
  try {
    // Skip if the sheet is empty or only has headers
    if (sheet.getLastRow() <= 1) {
      return;
    }

    // Get column positions
    const columns = ensureStatisticColumns(sheet);
    const attendedDaysCol = columns.attendedDaysCol;
    const percentageCol = columns.percentageCol;

    // Find the columns that contain attendance data (exclude first 2 columns and statistic columns)
    const headerRange = sheet.getRange(1, 1, 1, sheet.getLastColumn());
    const headerValues = headerRange.getValues()[0];

    const dateColumns = [];
    for (let i = 2; i < headerValues.length; i++) {
      // Skip the statistics columns
      if (i + 1 === attendedDaysCol || i + 1 === percentageCol) {
        continue;
      }

      // Check if it's a date column
      const header = headerValues[i];
      if (
        header &&
        (header instanceof Date || isDateString(header.toString()))
      ) {
        dateColumns.push(i + 1); // 1-based column index
      }
    }

    // For each student, calculate attendance statistics
    const studentCount = sheet.getLastRow() - 1; // Exclude header row
    for (let i = 0; i < studentCount; i++) {
      const studentRow = i + 2; // Row index is 1-based and skip header

      // Count present days
      let presentCount = 0;
      let totalDays = dateColumns.length;

      for (let j = 0; j < dateColumns.length; j++) {
        const col = dateColumns[j];
        const status = sheet.getRange(studentRow, col).getValue();

        // Count as present if the cell contains "present" (case-insensitive)
        if (status && status.toString().toLowerCase() === "present") {
          presentCount++;
        }
      }

      // Update attendance statistics
      sheet.getRange(studentRow, attendedDaysCol).setValue(presentCount);

      // Calculate percentage (avoid division by zero)
      if (totalDays > 0) {
        const percentage = (presentCount / totalDays) * 100;
        sheet.getRange(studentRow, percentageCol).setValue(percentage + "%");

        // Format the percentage cell
        sheet.getRange(studentRow, percentageCol).setNumberFormat("0.0%");
      } else {
        sheet.getRange(studentRow, percentageCol).setValue("N/A");
      }
    }

    // AutoResize the columns for better visibility
    sheet.autoResizeColumn(attendedDaysCol);
    sheet.autoResizeColumn(percentageCol);

    Logger.log(
      "Updated attendance statistics for " + studentCount + " students"
    );
  } catch (error) {
    Logger.log("Error updating statistics: " + error.toString());
  }
}

// Helper function to check if a string is a date
function isDateString(str) {
  // Check for common date formats (MM/DD/YYYY, DD/MM/YYYY, YYYY-MM-DD)
  const datePatterns = [
    /^\d{1,2}\/\d{1,2}\/\d{4}$/, // MM/DD/YYYY or DD/MM/YYYY
    /^\d{4}-\d{1,2}-\d{1,2}$/, // YYYY-MM-DD
  ];

  for (let i = 0; i < datePatterns.length; i++) {
    if (datePatterns[i].test(str)) {
      return true;
    }
  }

  return false;
}
