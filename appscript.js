// This function is the main handler for HTTP requests from the ESP32
function doPost(e) {
  try {
    // Parse the JSON data from the request
    const data = JSON.parse(e.postData.contents);
    const command = data.command;

    // Route to the appropriate function based on the command
    if (command === "column_attendance") {
      return markColumnAttendance(data);
    } else if (command === "batch_attendance") {
      return handleBatchAttendance(data);
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
    Logger.log("Error in doPost: " + error.toString());
    return ContentService.createTextOutput(
      JSON.stringify({ result: "error", message: error.toString() })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// New function to handle batch attendance records
function handleBatchAttendance(data) {
  try {
    // Extract data from the request
    const sheetName = data.sheet_name;
    const records = data.records;

    if (!records || !Array.isArray(records) || records.length === 0) {
      return ContentService.createTextOutput(
        JSON.stringify({
          result: "error",
          message: "No valid records provided",
        })
      ).setMimeType(ContentService.MimeType.JSON);
    }

    Logger.log("Processing batch attendance: " + records.length + " records");

    // Open the spreadsheet and get the sheet
    const ss = SpreadsheetApp.getActiveSpreadsheet();
    let sheet = ss.getSheetByName(sheetName);

    // Create the sheet if it doesn't exist
    if (!sheet) {
      sheet = ss.insertSheet(sheetName);
      initializeSheetHeaders(sheet);
      Logger.log("Created new sheet: " + sheetName);
    } else {
      // Ensure the headers exist even if the sheet already exists
      ensureHeaders(sheet);
    }

    // Process each record
    const results = [];
    for (let i = 0; i < records.length; i++) {
      const record = records[i];

      // Process each individual record using the existing function logic
      // but without returning after each one
      const result = processAttendanceRecord(sheet, record);
      results.push(result);

      // Log progress for large batches
      if (i > 0 && i % 10 === 0) {
        Logger.log(`Processed ${i} of ${records.length} records`);
      }
    }

    // Update statistics after all records have been processed
    updateAttendanceStatistics(sheet);

    // Sort the sheet by student ID
    sortSheetByStudentId(sheet);

    return ContentService.createTextOutput(
      JSON.stringify({
        result: "success",
        message: `Successfully processed ${records.length} attendance records`,
        details: results,
      })
    ).setMimeType(ContentService.MimeType.JSON);
  } catch (error) {
    Logger.log("Error in batch attendance: " + error.toString());
    return ContentService.createTextOutput(
      JSON.stringify({ result: "error", message: error.toString() })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// Helper function to process an individual attendance record
// Extracted from markColumnAttendance for reuse in batch processing
function processAttendanceRecord(sheet, data) {
  try {
    // Extract data from the record
    const studentId = data.student_id;
    const studentName = data.student_name;

    // Instead of status, we'll use the time if provided
    // If time is not provided, fall back to "present"
    const attendanceValue = data.time || "present";

    // Use provided date from the ESP32 if available, otherwise use today's date
    let formattedDate;
    if (data.date) {
      // Convert from YYYY-MM-DD to MM/DD/YYYY format for Google Sheets
      const dateParts = data.date.split("-");
      if (dateParts.length === 3) {
        formattedDate = dateParts[1] + "/" + dateParts[2] + "/" + dateParts[0];
      } else {
        // If date format is unexpected, use today's date
        formattedDate = Utilities.formatDate(
          new Date(),
          Session.getScriptTimeZone(),
          "MM/dd/yyyy"
        );
      }
    } else {
      // If no date provided, use today's date
      formattedDate = Utilities.formatDate(
        new Date(),
        Session.getScriptTimeZone(),
        "MM/dd/yyyy"
      );
    }

    // Get all headers at once
    const lastColumn = Math.max(sheet.getLastColumn(), 2);
    const headerRange = sheet.getRange(1, 1, 1, lastColumn);
    const headerValues = headerRange.getValues()[0];

    // More robust date column search
    let dateColumn = -1;

    // First pass: exact match
    for (let i = 0; i < headerValues.length; i++) {
      if (headerValues[i] && headerValues[i].toString() === formattedDate) {
        dateColumn = i + 1;
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
          if (headerDate === formattedDate) {
            dateColumn = i + 1;
            break;
          }
        }
      }
    }

    // If still no match, create a new column
    if (dateColumn === -1) {
      dateColumn = lastColumn + 1;
      sheet.getRange(1, dateColumn).setValue(formattedDate);
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

    // Mark attendance with time value instead of just "present"
    sheet.getRange(studentRow, dateColumn).setValue(attendanceValue);

    // Format the sheet to make it more readable (only in single record mode)
    if (sheet.getLastColumn() < 20) {
      // Only auto-resize for smaller sheets
      sheet.autoResizeColumns(1, dateColumn);
    }

    return {
      student_id: studentId,
      student_name: studentName,
      date: formattedDate,
      success: true,
    };
  } catch (error) {
    Logger.log("Error processing record: " + error.toString());
    return {
      student_id: data.student_id,
      success: false,
      error: error.toString(),
    };
  }
}

// Modified markColumnAttendance to use the shared processing function
function markColumnAttendance(data) {
  try {
    // Open the spreadsheet and get the sheet
    const ss = SpreadsheetApp.getActiveSpreadsheet();
    let sheet = ss.getSheetByName(data.sheet_name);

    // Create the sheet if it doesn't exist
    if (!sheet) {
      sheet = ss.insertSheet(data.sheet_name);
      initializeSheetHeaders(sheet);
    } else {
      // Ensure the headers exist even if the sheet already exists
      ensureHeaders(sheet);
    }

    // Process the attendance record
    const result = processAttendanceRecord(sheet, data);

    // Update statistics
    ensureStatisticColumns(sheet);
    updateAttendanceStatistics(sheet);

    // Sort the sheet by student ID
    sortSheetByStudentId(sheet);

    return ContentService.createTextOutput(
      JSON.stringify({
        result: "success",
        message:
          "Attendance marked for " + data.student_name + " on " + result.date,
        student: data.student_name,
        date: result.date,
      })
    ).setMimeType(ContentService.MimeType.JSON);
  } catch (error) {
    Logger.log("Error in markColumnAttendance: " + error.toString());
    return ContentService.createTextOutput(
      JSON.stringify({ result: "error", message: error.toString() })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// Function to test the batch attendance processing
function testBatchAttendance() {
  const testData = {
    command: "batch_attendance",
    sheet_name: "Attendance",
    records: [
      {
        student_id: "1",
        student_name: "Arik",
        date: "2025-05-17",
        time: "09:15:30",
      },
      {
        student_id: "2",
        student_name: "OOO",
        date: "2025-05-17",
        time: "09:20:45",
      },
      {
        student_id: "65",
        student_name: "Ymir",
        date: "2025-05-17",
        time: "09:35:12",
      },
    ],
  };

  const result = handleBatchAttendance(testData);
  Logger.log(result);
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
        const value = sheet.getRange(studentRow, col).getValue();

        // Count as present if the cell is not empty
        // This change handles time values as well as "present" text
        if (value && value.toString() !== "") {
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
