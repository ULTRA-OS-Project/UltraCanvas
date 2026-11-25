- **A percentage typed into a spreadsheet cell was kept as text.** In
  `SpreadsheetCell::SetValueFromString`, `std::stod` stopped at the `%`, so
  the check that the whole entry was a number failed. The percentage branch
  after it was never reached, and "45%" could not be summed. The sign is now
  taken off first:
  - "45%", "12.5 %" and "-3%" are the percentages 0.45, 0.125 and -0.03;
  - "%", "abc%" and "45%%" stay text.

  A percentage under the General format now shows as one ("45%"), not as the
  fraction "0.45". That also applies to percentages read from a CSV file.
  What the editor shows types back to the same value.
