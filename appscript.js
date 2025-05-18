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

    // Use status field directly - if available, otherwise default to "present"
    const attendanceValue = data.status || "present";

    // Use provided date from the ESP32 if available, otherwise use today's date
    let formattedDate;
    if (data.date) {
      // Use the date as-is without format checking
      formattedDate = data.date;
    } else {
      // If no date provided, use today's date
      formattedDate = Utilities.formatDate(
        new Date(),
        Session.getScriptTimeZone(),
        "MM/dd/yyyy"
      );
    }

    // Get all headers at once
    const lastColumn = Math.max(sheet.getLastColumn(), 1);
    const headerRange = sheet.getRange(1, 1, 1, lastColumn);
    const headerValues = headerRange.getValues()[0];

    // Find the date column - more robust search
    let dateColumn = -1;

    // Search for the date column with exact match or case-insensitive match
    for (let i = 0; i < headerValues.length; i++) {
      if (headerValues[i]) {
        const headerVal = headerValues[i].toString().trim();
        const dateVal = formattedDate.toString().trim();

        // Try both exact match and case-insensitive match
        if (
          headerVal === dateVal ||
          headerVal.toLowerCase() === dateVal.toLowerCase()
        ) {
          dateColumn = i + 1;
          break;
        }
      }
    }

    // If no match, create a new column
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
    }

    // Mark attendance with status value
    sheet.getRange(studentRow, dateColumn).setValue(attendanceValue);

    // Format the sheet to make it more readable (only in single record mode)
    if (sheet.getLastColumn() < 20) {
      // Only auto-resize for smaller sheets
      sheet.autoResizeColumns(1, dateColumn);
    }

    return {
      student_id: studentId,
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
          "Attendance marked for student ID " +
          data.student_id +
          " on " +
          result.date,
        student_id: data.student_id,
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
        date: "21/5",
        status: "present",
      },
      {
        student_id: "2",
        date: "21/5",
        status: "present",
      },
      {
        student_id: "65",
        date: "19/5",
        status: "present",
      },
    ],
  };

  const result = handleBatchAttendance(testData);
  Logger.log(result);
}

// Initialize a new sheet with proper headers
function initializeSheetHeaders(sheet) {
  sheet.getRange("A1").setValue("Student ID");

  // Add statistics columns
  sheet.getRange("B1").setValue("Attended Days");
  sheet.getRange("C1").setValue("Percentage");

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

  if (headerA === "" || headerA !== "Student ID") {
    sheet.getRange("A1").setValue("Student ID");
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

  // If columns don't exist, create them
  if (attendedDaysCol === -1) {
    // Add it right after the Student ID column
    attendedDaysCol = 2;
    sheet.insertColumnAfter(1);
    sheet.getRange(1, attendedDaysCol).setValue("Attended Days");
    sheet.getRange(1, attendedDaysCol).setFontWeight("bold");
    sheet.getRange(1, attendedDaysCol).setBackground("#E0E0E0");
  }

  if (percentageCol === -1) {
    // Add it right after the Attended Days column
    percentageCol = attendedDaysCol + 1;
    sheet.insertColumnAfter(attendedDaysCol);
    sheet.getRange(1, percentageCol).setValue("Percentage");
    sheet.getRange(1, percentageCol).setFontWeight("bold");
    sheet.getRange(1, percentageCol).setBackground("#E0E0E0");
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

    // Find the columns that contain attendance data (exclude first column and statistic columns)
    const headerRange = sheet.getRange(1, 1, 1, sheet.getLastColumn());
    const headerValues = headerRange.getValues()[0];

    const dateColumns = [];
    for (let i = 0; i < headerValues.length; i++) {
      // Skip the first column (Student ID) and statistics columns
      if (i + 1 === 1 || i + 1 === attendedDaysCol || i + 1 === percentageCol) {
        continue;
      }

      // Any other column is considered a date column
      dateColumns.push(i + 1); // 1-based column index
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
