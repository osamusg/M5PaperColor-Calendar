#include <M5Unified.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <YAMLDuino.h>
#include <ctime>
#include <cmath>
#include <vector>
#include <esp_sntp.h>
#include <esp_sleep.h>
#include "moon_jpg.h"
#include "Jp-holidays.h"

#ifdef ARDUINO_M5STACK_PAPERCOLOR
#include <Adafruit_NeoPixel.h>
#endif

// Beep tones
#define NOTE_C4 261.626
#define NOTE_C5 523.251   // DO
#define NOTE_D5 587.330
#define NOTE_E5 659.255   // MI
#define NOTE_F5 698.456
#define NOTE_G5 783.991   // SO
#define NOTE_A5 880.000
#define NOTE_B5 987.767
#define NOTE_C6 1046.502  // DO, one octave higher.
#define NOTE_A7 3520.000  // RA, two octaves higher.

// Global variables
int scn_x, scn_y;
bool isPortalRunning = false;
bool shouldSaveParamConfig = false;
bool shouldSaveWiFiConfig = false;
bool staticIpEnabled = false;
IPAddress ip, gw, sn, dns1, dns2;
ulong startTimeMillis = 0;
RTC_DATA_ATTR bool skipBootToneOnWake = false;
#ifdef DEBUG_SLEEP_LOG
ulong debugSleepStartMillis = 0;
bool debugSleepLogged = false;
#endif
ulong previousLedMillisRight = 0;
ulong previousLedMillisLeft = 0;
bool ledRightOn = false;
bool ledLeftOn = false;
bool batteryLow = false;
volatile bool isLedRightWorking = false;
int8_t currentDisplayMonthShift = 0;
bool holidayRulesLoaded = false;
char drawCircleTodayHtml[512] = "";
char isWkStartsMonHtml[512] = "";
char localCalendarEnabledHtml[512] = "";
char localHolidaysEditorHtml[12288] = "";

// AP mode Web UI configuration parameters
char static_ip[24]          = "";
char gateway[24]            = "";
char subnetMask[24]         = "255.255.255.0";
char pri_dns[24]            = "";
char sec_dns[24]            = "";
char ntp_server[64]         = "ntp.nict.jp";
char user_tz[32]            = "JST-9";
int refreshEvery            = 0; // in o'clock(0-24), -1 means no auto reload
bool drawCircleToday        = true;
bool isWkStartsMon          = false;
bool localCalendarEnabled   = false;
char localEraName[8]        = "R.";
int localEraStartYear       = 2019;
char localHolidays[4096]    = "";

// constants
constexpr double SYNODIC_MONTH = 29.53058853;// Average length of a lunar cycle in days
constexpr uint8_t IMAGE_SIZE = 120;          // Size of the moon face image canvas (120x120 pixels)
constexpr uint8_t PANEL_SIZE = 120;          // Size of the month panel canvas (120x120 pixels)
constexpr uint8_t MARGIN = 6;                // Margin for the moon image
constexpr uint8_t BEZEL_SIZE = 5;            // What a heck!
constexpr uint8_t LED_L = 0;                 // On this board, NeoPixel index 0 is the left LED
constexpr uint8_t LED_R = 1;                 //   and index 1 is the right LED.
constexpr uint8_t BATTERY_LOW_ENTER = 20;
constexpr uint8_t BATTERY_LOW_EXIT = 25;
constexpr uint8_t MAX_HOLIDAYS_PER_MONTH = 32;
constexpr uint32_t RUNTIME_MS = 5UL * 60UL * 1000UL; // 5 minutes in milliseconds
constexpr auto CAL_FONT = &fonts::Font6;
constexpr auto MONTH_FONT = &fonts::Font8;
constexpr auto MONTH_FONT_SMALL = &fonts::FreeSansBold9pt7b;
constexpr auto MINI_MONTH_FONT = &fonts::FreeSans9pt7b;
constexpr const char* monthString[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
constexpr const char* AP_PASS = "00000000";
constexpr char CHECKBOX_HTML_FORMAT[] =
  "<input id='%s' type='checkbox'%s>"
  "<label for='%s'>%s</label>%s"
  "<script>(function(){var hidden=document.getElementsByName('%s')[0];var editor=document.getElementById('%s');"
  "if(hidden&&editor){hidden.value=editor.checked?'T':'';editor.onchange=function(){hidden.value=this.checked?'T':'';};}})();</script>";

// Data structures
struct Point { uint16_t x, y; };
struct Circle { int32_t x, y, radius;};
struct Holidays {
  int count;
  int days[MAX_HOLIDAYS_PER_MONTH];
};

// Global objects
WiFiManager wm_config;
WiFiManagerParameter* param_ntp_server;
WiFiManagerParameter* param_user_tz;
WiFiManagerParameter* param_refreshEvery;
WiFiManagerParameter* param_drawCircleToday;
WiFiManagerParameter* param_drawCircleTodayEditor;
WiFiManagerParameter* param_isWkStartsMon;
WiFiManagerParameter* param_isWkStartsMonEditor;
WiFiManagerParameter* param_localCalendarEnabled;
WiFiManagerParameter* param_localCalendarEnabledEditor;
WiFiManagerParameter* param_localEraName;
WiFiManagerParameter* param_localEraStartYear;
WiFiManagerParameter* param_localHolidays;
WiFiManagerParameter* param_localHolidaysEditor;
Preferences prefs;
YAMLNode holidayRoot;
M5Canvas screenCanvas(&M5.Display);
M5Canvas panelCanvas(&M5.Display);
#ifdef ARDUINO_M5STACK_PAPERCOLOR
Adafruit_NeoPixel pixels(2, 21, NEO_GRB + NEO_KHZ800); // Initialize NeoPixel with 2 LEDs on pin 21
#endif
TaskHandle_t drawTaskHandle = NULL; // Handle for the E-Ink draw task


// put function declarations here:


/**
 * @brief Attempts to get the current time from an NTP server and synchronize the system time.
 * @return true if the time was successfully obtained from the NTP server, false otherwise.
 */
bool getNtpTime() {
   bool ntp_success = false;
  if (!M5.Rtc.isEnabled()) {
    screenCanvas.println("RTC not found.");
  }
  else if (WiFi.status() == WL_CONNECTED) {
    configTzTime(user_tz, ntp_server);
    
    // M5.begin() automatically loads RTC time into system time, 
    // so checking year > 2020 is not reliable for NTP sync completion.
    // We must check SNTP sync status instead.
    for (int i = 0; i < 20; i++) {
      if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        ntp_success = true;
        break;
      }
      delay(500);
    }
  }
  if (ntp_success) {
    struct tm timeInfo;
    getLocalTime(&timeInfo);
    M5.Rtc.setDateTime(&timeInfo);
    // M5.Display.println("RTC updated from NTP.");
    Serial.println("RTC updated from NTP.");
  }
  else {
    // M5.Display.println("Failed to get time from NTP.");
    Serial.println("Failed to get time from NTP.");
    if (M5.Rtc.isEnabled()) {
      auto dt = M5.Rtc.getDateTime();
      struct tm t{
          .tm_sec = dt.time.seconds,
          .tm_min = dt.time.minutes,
          .tm_hour = dt.time.hours,
          .tm_mday = dt.date.date,
          .tm_mon = dt.date.month - 1,
          .tm_year = dt.date.year - 1900,
          .tm_isdst = -1,
      };
      time_t localEpoch = mktime(&t);
      struct timeval tv = {
          .tv_sec = (long)localEpoch,
          .tv_usec = 0,
      };
      settimeofday(&tv, NULL);
      // M5.Display.println("System time loaded from RTC.");
      Serial.println("System time loaded from RTC.");
    }
  }
  return ntp_success;
}

/**
 * @brief Calculates the Julian Day for a given date and time.
 * @param utc_time Pointer to a tm structure representing the UTC date and time.
 * @return The Julian Day for the given date and time.
 */
double calcJulianDay(const tm* utc_time) {
  int year = utc_time->tm_year + 1900; // tm_year is years since 1900
  int month = utc_time->tm_mon + 1;    // tm_mon is months since January (0-11)
  int day = utc_time->tm_mday;
  int hour = utc_time->tm_hour;
  int minute = utc_time->tm_min;
  int second = utc_time->tm_sec;

  if (month <= 2) {
    year -= 1;
    month += 12;
  }

  // Astronomical adjustment for the Gregorian calendar
  long a = year / 100;
  long b = 2 - a + (a / 4);
  long c = (long)(365.25 * (year + 4716));
  long d = (long)(30.6001 * (month + 1));

  // Calculate the fractional part of the day
  double day_fraction = day + (hour + minute / 60.0 + second / 3600.0) / 24.0;
  return (double)b + day_fraction + (double)c + (double)d - 1524.5;
}

/**
 * @brief Simply calculates the precise age of the moon in days for a given time.
 * @param currentTime The current time in seconds since the epoch (January 1, 1970).
 * @return The age of the moon in days.
 */
double getMoonAge(time_t currentTime) {
  struct tm *utc_time = gmtime(&currentTime);

  double julianDay = calcJulianDay(utc_time); // Calculate the Julian Day for the current date and time

  // Reference new moon date: January 6, 2000, at 18:14 UTC
  const double referenceJulianDay = 2451550.26; // Julian Day for the reference new moon

  double deltaDays = julianDay - referenceJulianDay;
  double moonAge = fmod(deltaDays, SYNODIC_MONTH); // Moon's age

  if (moonAge < 0)
    moonAge += SYNODIC_MONTH; // Ensure moonAge is positive

  return moonAge;
}

/**
 * @brief Calculates the center and radius of the moon mask based on the age of the moon.
 * @param age The age of the moon in days.
 * @return A Circle struct containing the center coordinates (x, y) and radius of the moon mask.
 * @return If the radius is -1, it indicates a half moon (rectangle mask needed). If the radius is 0, no mask is needed (Full moon).
 */
Circle getMoonMask(double age) {
  constexpr uint8_t MOON_SIZE = IMAGE_SIZE - MARGIN * 2; // 108 px, Size of the moon image in pixels
  constexpr uint8_t MID = IMAGE_SIZE / 2; // Midpoint of the X and Y axis for the moon image
  constexpr Point   MOON_TOP = {MID, MARGIN};
  constexpr Point   MOON_BOTTOM = {MID, IMAGE_SIZE - MARGIN};
  constexpr double  AGE_UNIT = MOON_SIZE / SYNODIC_MONTH * 2; // 108 pixels for a full lunar cycle of 29.53 days * twice(AND + NAND)

  if ((int)age == (int)(SYNODIC_MONTH / 2)) return Circle{0, 0, 0}; // Full moon, no mask needed

  double _age = age;
  if (_age > SYNODIC_MONTH / 2) _age -= (SYNODIC_MONTH / 2); // Ensure age is within the lunar cycle
  uint8_t midPointX = (int)(MOON_SIZE - AGE_UNIT * _age) + MARGIN; // Calculate the midpoint of the X axis based on the age of the moon
  if (midPointX == MID) return Circle{0, 0, -1}; // Half moon, rectangle mask needed

  double determinant = 2.0 * (MOON_TOP.x * (MID - MOON_BOTTOM.y) +
                              midPointX * (MOON_BOTTOM.y - MOON_TOP.y) +
                              MOON_BOTTOM.x * (MOON_TOP.y - MID));  // Calculate the determinant for the circle equation

  double sqTop = MOON_TOP.x * MOON_TOP.x + MOON_TOP.y * MOON_TOP.y;
  double sqMid = midPointX * midPointX + MID * MID;
  double sqBottom = MOON_BOTTOM.x * MOON_BOTTOM.x + MOON_BOTTOM.y * MOON_BOTTOM.y;

  double centerX = (sqTop * (MID - MOON_BOTTOM.y) +
                    sqMid * (MOON_BOTTOM.y - MOON_TOP.y) +
                    sqBottom * (MOON_TOP.y - MID)) / determinant;

  double centerY = (sqTop * (MOON_BOTTOM.x - midPointX) +
                    sqMid * (MOON_TOP.x - MOON_BOTTOM.x) +
                    sqBottom * (midPointX - MOON_TOP.x)) / determinant;

  double radius = std::hypot(centerX - MOON_TOP.x, centerY - MOON_TOP.y);

  return {
    static_cast<int32_t>(std::round(centerX)),
    static_cast<int32_t>(std::round(centerY)),
    static_cast<int32_t>(std::round(radius))
  };
}

/**
 * @brief Draws the moon image with a mask based on the age of the moon.
 * @param age The age of the moon in days.
 * @return An M5Canvas object containing the moon image with the applied mask.
 */
void drawMoonPanel(double age) {
  M5Canvas maskSprite(&M5.Display);

  maskSprite.setColorDepth(16);
  maskSprite.createSprite(IMAGE_SIZE, IMAGE_SIZE);

  panelCanvas.fillSprite(TFT_BLACK);
  panelCanvas.drawJpg(moon_jpg, sizeof(moon_jpg), 0, 0);
  
  Circle mask = getMoonMask(age);
  uint16_t posi = TFT_RED;
  uint16_t nega = 0;
  if (age < SYNODIC_MONTH / 4 || age > 3 * SYNODIC_MONTH / 4) {
    std::swap(posi, nega);
  }
  maskSprite.fillSprite(nega);  // Fill the mask with black
  if (mask.radius != 0) {       // If the radius is 0, it's a full moon, no mask needed
    if (mask.radius != -1)
      maskSprite.fillCircle(mask.x, mask.y, mask.radius, posi);
    else {                        // If the radius is -1, it's a half moon, rectangle mask needed
      maskSprite.fillRect(0, 0, IMAGE_SIZE / 2, IMAGE_SIZE, posi);
      maskSprite.fillRect(IMAGE_SIZE / 2, 0, IMAGE_SIZE / 2, IMAGE_SIZE, nega);
    }
  }

  maskSprite.pushSprite(&panelCanvas, 0, 0, TFT_RED); // Apply the mask to the image sprite with transpearent color (TFT_RED)
  
  return;
}

/**
 * @brief Draws the month panel with the year and month information, manipulate panelCanvas(Global) to draw.
 * @param year The year to be displayed in tm struct format (e.g., 126, means 2026).
 * @param month The month to be displayed in tm struct format (0-11).
 */
void drawMonthPanel(int tm_year, int tm_month) {
  int year = tm_year + 1900;
  int month = tm_month + 1;
  int center = PANEL_SIZE / 2;
  String localCalendarString = "";
  Serial.printf("localCalendar: %s\n", localCalendarEnabled ? "true" : "false");
  if (localCalendarEnabled) localCalendarString = "  (R." + String(year - 2018) + ")";
  panelCanvas.fillSprite(TFT_WHITE);
  panelCanvas.setTextColor(TFT_BLACK, TFT_WHITE);
  panelCanvas.setTextSize(1);
  panelCanvas.setTextDatum(textdatum_t::top_center);
  panelCanvas.drawString(String(year) + localCalendarString, center, 2, MONTH_FONT_SMALL);
  panelCanvas.setTextSize(1);
  panelCanvas.drawString(monthString[month - 1], center, 100, MONTH_FONT_SMALL);
  panelCanvas.setTextSize(1);
  if (month < 10) {
    panelCanvas.drawString(String(month), center, 20, MONTH_FONT);
  }
  else {
    panelCanvas.drawString(String(month).substring(0, 1), center - 20, 20, MONTH_FONT);
    panelCanvas.drawString(String(month).substring(1, 2), center + 20, 20, MONTH_FONT);
  }
}

/**
 * @brief Returns the day of the week for a given date.
 * @param tm_year The year in tm struct format (years since 1900).
 * @param tm_month The month in tm struct format (0-11).
 * @param tm_mday The day of the month (1-31).
 * @return The day of the week (0 = Sunday, 1 = Monday, ..., 6 = Saturday).
 */
uint8_t getWdayOfTheDay(int tm_year, int tm_month, int tm_mday) {
  struct tm timeinfo = {0};
  timeinfo.tm_year = tm_year;         // tm_year is years since 1900
  timeinfo.tm_mon = tm_month;         // tm_mon is months since January (0-11)
  timeinfo.tm_mday = tm_mday;         // tm_mday is day of the month
  timeinfo.tm_isdst = -1;             // Not considering daylight saving time

  // Normalize the time structure and get the weekday
  mktime(&timeinfo);
  return timeinfo.tm_wday; // Returns 0 (Sunday) to 6 (Saturday)
}

/**
 * @brief Returns the last day of the month for a given year and month.
 * @param tm_year The year in tm struct format (years since 1900).
 * @param tm_month The month in tm struct format (0-11).
 * @return The last day of the month (28-31).
 */
uint8_t getLastDayOfTheMonth(int tm_year, int tm_month) {
  struct tm timeinfo = {0};
  timeinfo.tm_year = tm_year;         // tm_year is years since 1900
  timeinfo.tm_mon = tm_month + 1;     // Move to the next month
  timeinfo.tm_mday = 0;               // Set day to 0 to get the last day of the previous month
  timeinfo.tm_isdst = -1;             // Not considering daylight saving time

  // Normalize the time structure and get the last day of the month
  mktime(&timeinfo);
  return timeinfo.tm_mday; // Returns the last day of the month (28-31)
}

/**
 * @brief (Helper function) Appends the holiday tokens for a specific month from the YAML node to the output vector.
 * @param node The YAML node containing the holiday rules.
 * @param monthKey The key representing the month in the YAML node.
 * @param outTokens Vector to store the collected holiday tokens.
 * @return true if the month tokens were successfully appended, false otherwise.
 */
bool appendMonthTokens(const YAMLNode& node, const char* monthKey, std::vector<String>& outTokens) {
  if (!node.isMap()) return false;

  YAMLNode monthNode = node[monthKey];
  if (!monthNode.isMap()) return false;

  yaml_document_t* document = monthNode.getDocument();
  yaml_node_t* month = monthNode.getNode();
  if (!document || !month || month->type != YAML_MAPPING_NODE) return false;

  for (yaml_node_pair_t* pair = month->data.mapping.pairs.start; pair < month->data.mapping.pairs.top; ++pair) {
    yaml_node_t* keyNode = yaml_document_get_node(document, pair->key);
    if (keyNode && keyNode->type == YAML_SCALAR_NODE) {
      const char* token = reinterpret_cast<const char*>(keyNode->data.scalar.value);
      if (token && *token != '\0') {
        outTokens.emplace_back(token);
      }
    }
  }

  return true;
}

/**
 * @brief Retrieves the holidays for a specific month and year based on the loaded YAML holiday rules.
 * @param tm_year The year in tm struct format (years since 1900).
 * @param tm_month The month in tm struct format (0-11).
 * @return A Holidays struct containing the holidays for the specified month.
 */
Holidays getHolidaysOfTheMonth(int tm_year, int tm_month) {
  Holidays holidays;
  holidays.count = 0;

  if (!holidayRulesLoaded) return holidays;

  int totalMonths = tm_year * 12 + tm_month;
  int normalizedYear = totalMonths / 12;
  int normalizedMonth = totalMonths % 12;
  if (normalizedMonth < 0) {
    normalizedMonth += 12;
    --normalizedYear;
  }

  const int targetYear = normalizedYear + 1900;
  char monthKey[3];
  char yearKey[5];
  snprintf(monthKey, sizeof(monthKey), "%02d", normalizedMonth + 1);
  snprintf(yearKey, sizeof(yearKey), "%d", targetYear);

  auto addDay = [&](int day) {
    if (day < 1 || day > getLastDayOfTheMonth(normalizedYear, normalizedMonth))
      return;

    for (int i = 0; i < holidays.count; ++i) {
      if (holidays.days[i] == day) return;
    }

    if (holidays.count < MAX_HOLIDAYS_PER_MONTH)
      holidays.days[holidays.count++] = day;
  };

  auto resolveToken = [&](const char* token) {
    if (token == nullptr || token[0] == '\0') return;

    const char* dash = strchr(token, '-');
    if (dash) {
      char prefix[8] = {0};
      char dowName[8] = {0};

      size_t prefixLen = static_cast<size_t>(dash - token);
      if (prefixLen >= sizeof(prefix)) prefixLen = sizeof(prefix) - 1;
      memcpy(prefix, token, prefixLen);
      prefix[prefixLen] = '\0';

      strncpy(dowName, dash + 1, sizeof(dowName) - 1);

      int ordinal = 0;
      if (strcmp(prefix, "1st") == 0) ordinal = 1;
      else if (strcmp(prefix, "2nd") == 0) ordinal = 2;
      else if (strcmp(prefix, "3rd") == 0) ordinal = 3;
      else if (strcmp(prefix, "4th") == 0) ordinal = 4;
      else if (strcmp(prefix, "5th") == 0) ordinal = 5;

      if (ordinal == 0) return;

      int dow = -1;
      const char* const names[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
      for (int i = 0; i < 7; ++i)
        if (strcmp(names[i], dowName) == 0) { dow = i; break; }
      if (dow < 0) return;

      uint8_t firstWday = getWdayOfTheDay(normalizedYear, normalizedMonth, 1);
      int targetDay = 1 + (dow - firstWday + 7) % 7;
      int resolvedDay = targetDay + (ordinal - 1) * 7;
      addDay(resolvedDay);
      return;
    }

    if (isdigit((unsigned char)token[0])) {
      addDay(atoi(token));
      return;
    }
  };

  if (holidayRoot.isMap()) {
    YAMLNode commonSection = holidayRoot["year-common"];
    {
      std::vector<String> tokens;
      if (appendMonthTokens(commonSection, monthKey, tokens)) {
        for (const String& token : tokens) {
          resolveToken(token.c_str());
        }
      }
    }

    YAMLNode yearsSection = holidayRoot["years"];
    YAMLNode yearNode = yearsSection[yearKey];
    {
      std::vector<String> tokens;
      if (appendMonthTokens(yearNode, monthKey, tokens)) {
        for (const String& token : tokens) {
          resolveToken(token.c_str());
        }
      }
    }
  }

  return holidays;
}

/**
 * @brief Calculates the axis (x, y) position of a specific day in the calendar grid.
 * @param tm_year The year in tm struct format (years since 1900).
 * @param tm_month The month in tm struct format (0-11).
 * @param tm_mday The day of the month (1-31).
 * @param isMini Whether to calculate for the mini version of the calendar.
 * @return A Point struct containing the x and y coordinates of the specified day in the calendar grid.
 */
Point calcAxisOfTheDay(int tm_year, int tm_month, int tm_mday, bool isMini) {
  uint8_t tm_wday = getWdayOfTheDay(tm_year, tm_month, tm_mday); // 0 (Sunday) to 6 (Saturday)
  uint8_t firstWday = getWdayOfTheDay(tm_year, tm_month, 1);   // 0 (Sunday) to 6 (Saturday)
  Point axis {0,0};

  uint8_t lMargin = 50, tMargin = 32, hMargin = 0, cellUnit = 60;
  if (isMini) { lMargin = 15; tMargin = 40; hMargin = 20; cellUnit = 16; }

  int col = !isWkStartsMon ? tm_wday : (tm_wday + 6) % 7;   // Adjust x position based on week start day
  int row = !isWkStartsMon ? (tm_mday + firstWday - 1) / 7 : (tm_mday + (firstWday + 6) % 7 - 1) / 7; // Adjust y position based on week start day
  axis.x = lMargin + col * cellUnit;
  axis.y = tMargin + row * cellUnit;
  return axis;
}

/**
 * @brief Draws the holidays on the calendar for a specific month and year.
 * @param Holidays The Holidays struct containing the holidays for the specified month.
 * @param tm_year The year in tm struct format (years since 1900).
 * @param tm_month The month in tm struct format (0-11).
 * @param isMini Whether to draw the mini version of the calendar.
 */
void drawHolidays(const Holidays Holidays, int tm_year, int tm_month, bool isMini) {
  // Placeholder for future implementation of holiday drawing logic
  int count = Holidays.count;
  if (count == 0) return;
  
  panelCanvas.setTextSize(0.9);
  panelCanvas.setTextColor(TFT_RED, TFT_WHITE);
  char dayText[4];
  if (isMini) panelCanvas.setFont(MINI_MONTH_FONT);
  else panelCanvas.setFont(CAL_FONT);
  
  for (int i = count; i > 0; --i) {
    Point axis = calcAxisOfTheDay(tm_year, tm_month, Holidays.days[i - 1], isMini);

    snprintf(dayText, sizeof(dayText), "%u", Holidays.days[i - 1]);
    uint8_t shift = Holidays.days[i - 1] < 10 ? 2 : 0;          // Shift the text position for single digit days (heck!)
    panelCanvas.drawString(dayText, axis.x + shift, axis.y);    // Draw the holiday number at the calculated position
  }
}

/**
 * @brief Draws a circle around the specified day in the calendar.
 * @param tm_mday The day of the month (1-31) to draw the circle around.
 * @param tm_wday The day of the week (0 = Sunday, 1 = Monday, ..., 6 = Saturday) for the specified day.
 * @param firstWday The day of the week (0 = Sunday, 1 = Monday, ..., 6 = Saturday) for the first day of the month.
 */
void drawCircleAroundTheDay(int tm_mday, int tm_wday, int firstWday) {
  uint8_t lMargin = 50, tMargin = 32, cellUnit = 60;
  
  uint16_t col = !isWkStartsMon ? tm_wday : (tm_wday + 6) % 7;   // Adjust x position based on week start day
  uint16_t row = !isWkStartsMon ? (tm_mday + firstWday - 1) / 7 : (tm_mday + (firstWday + 6) % 7 - 1) / 7; // Adjust y position based on week start day
  int16_t x = lMargin + col * cellUnit;
  int16_t y = tMargin + row * cellUnit + 16;    // Adjust y position for the circle
  for (uint8_t r = 30; r > 25; r--)
    panelCanvas.drawCircle(x, y, r, TFT_RED);
}

/**
 * @brief Draws a calendar for a given year and month on the panelCanvas.
 * @param tm_year The year in tm struct format (years since 1900).
 * @param tm_month The month in tm struct format (0-11).
 * @param isMini Whether to draw the mini version of the calendar.
 */
void drawCalendar(int tm_year, int tm_month, bool isMini) {
  uint8_t firstWday = getWdayOfTheDay(tm_year, tm_month, 1);   // 0 (Sunday) to 6 (Saturday)
  uint8_t lastDay = getLastDayOfTheMonth(tm_year, tm_month);
  constexpr const char* fullWDays[] = {"  Sun  ", "  Mon  ", "  Tue  ", "  Wed  ", "  Thu  ", "   Fri    ", "  Sat    "};
  constexpr const char* miniWDays[] = {"S", "M", "T", "W", "T", "F", "S"};
  const char* const* wDays = isMini ? miniWDays : fullWDays;    // For memory saving, use different array for the mini calendar display

  uint8_t lMargin = 50, tMargin = 32, hMargin = 0, cellUnit = 60;
  if (isMini) { lMargin = 15; tMargin = 40; hMargin = 20; cellUnit = 16; }

  panelCanvas.fillSprite(TFT_WHITE);
  panelCanvas.setTextDatum(textdatum_t::top_center);

  // Column index of day 1 in the first row
  uint8_t startCol = isWkStartsMon ? (firstWday + 6) % 7 : firstWday;
  uint8_t day = 1;
  for (uint8_t x = 0; x < 7; x++) {     // Draw the weekday headers and the first row of days 0=Sunday to 6=Saturday
    int _x = x;
    if (isWkStartsMon) _x = (x + 1) % 7;                              // from 1=Monday to 7=0=Sunday
    if (_x == 0) panelCanvas.setTextColor(TFT_WHITE, TFT_RED);        // Sunday,   WeekStart = Sunday then _x = 0, WeekStart = Monday then _x = 6
    else if (_x == 6) panelCanvas.setTextColor(TFT_WHITE, TFT_BLUE);  // Saturday, WeekStart = Sunday then _x = 6, WeekStart = Monday then _x = 5
    else panelCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
    panelCanvas.setTextSize(1);
    if (isMini) panelCanvas.setFont(MINI_MONTH_FONT);
    else panelCanvas.setFont(MONTH_FONT_SMALL);
    panelCanvas.drawString(wDays[_x], lMargin + x * cellUnit, hMargin);   // Write the weekday header text
    
    // Write the first row of days, considering the week start day
    if (x >= startCol) {
      if (_x == 0) panelCanvas.setTextColor(TFT_RED, TFT_WHITE);
      else if (_x == 6) panelCanvas.setTextColor(TFT_BLUE, TFT_WHITE);
      else panelCanvas.setTextColor(TFT_BLACK, TFT_WHITE);
      panelCanvas.setTextSize(0.9);
      if (isMini) panelCanvas.setFont(MINI_MONTH_FONT);
      else panelCanvas.setFont(CAL_FONT);
      char dayText[4];
      snprintf(dayText, sizeof(dayText), "%u", day);
      panelCanvas.drawString(dayText, lMargin + x * cellUnit + 1, tMargin);   // Write the first row of days
      day++;
    }
  }

  // Then, write the remaining rows of days
  for (uint16_t y = tMargin + cellUnit; y < (tMargin + 6 * cellUnit); y += cellUnit/* - 1*/) {
    for (uint16_t x = lMargin; x < (lMargin + 7 * cellUnit); x += cellUnit) {
      if ((day + firstWday - 1) % 7 == 0) panelCanvas.setTextColor(TFT_RED, TFT_WHITE);
      else if ((day + firstWday - 1) % 7 == 6) panelCanvas.setTextColor(TFT_BLUE, TFT_WHITE);
      else panelCanvas.setTextColor(TFT_BLACK, TFT_WHITE);
      char dayText[4];    // For memory saving, use C style small buffer instead of String for the day text
      snprintf(dayText, sizeof(dayText), "%u", day);      // for memory saving, use C style small buffer instead of String for the day text
      uint8_t shift = day < 10 ? 2 : 0;                   // Shift the text position for single digit days (heck!)
      panelCanvas.drawString(dayText, x + shift, y);      // Write days in 2nd row and beyond 
      day++;
      if (day > lastDay) break; // Stop if the last day of the month
    }
    if (day > lastDay) break;   // Stop outer loop if the last day of the month
  }

  if (localCalendarEnabled) {
    Holidays holidays = getHolidaysOfTheMonth(tm_year, tm_month);
    drawHolidays(holidays, tm_year, tm_month, isMini);
  }

  // Draw a circle around today's date if not in mini mode and the option is enabled
  if (!isMini && drawCircleToday) {
    time_t current;
    time(&current);
    struct tm local_tm;
    localtime_r(&current, &local_tm);
    
    if (local_tm.tm_year == tm_year && local_tm.tm_mon == tm_month) {
      drawCircleAroundTheDay(local_tm.tm_mday, local_tm.tm_wday, firstWday);
      
      /* for DEBUG, check the aixs of each day in the month with circle
      for (int i = 1; i <= lastDay; i++) 
        drawCircleAroundTheDay(i, (i + firstWday - 1) % 7, firstWday);
      */
    }
  }
}

/**
 * @brief Task function to redraw the screen with the current moon age and time information.
 * @param pvParameters Pointer to the task parameters (not used).
 */
void reDrawScreen(void *pvParameters) {
  int8_t currentDisplayMonthShift = static_cast<int8_t>(reinterpret_cast<intptr_t>(pvParameters));

  bool isNtpSuccess = false;
  if (currentDisplayMonthShift == 0) isNtpSuccess = getNtpTime();

  screenCanvas.fillSprite(TFT_WHITE);

  time_t current, target;
  time(&current);
  struct tm currentLocal_tm;
  localtime_r(&current, &currentLocal_tm);

  // Normalize the shifted month and year based on the current local time
  int totalMonths = currentLocal_tm.tm_year * 12 + currentLocal_tm.tm_mon + currentDisplayMonthShift;
  int targetYear = totalMonths / 12;
  int targetMonth = totalMonths % 12;
  if (targetMonth < 0) {
    targetMonth += 12;
    --targetYear;
  }

  // Create a new tm structure for the target date, keeping the same day of the month
  struct tm targetLocal_tm = currentLocal_tm;
  uint8_t lastDay = getLastDayOfTheMonth(targetYear, targetMonth);
  targetLocal_tm.tm_year = targetYear;
  targetLocal_tm.tm_mon = targetMonth;
  targetLocal_tm.tm_mday = currentLocal_tm.tm_mday; // Keep the same day of the month
  targetLocal_tm.tm_isdst = -1;

  if (targetLocal_tm.tm_mday > lastDay)     // Clamp to the target month's last day to avoid impossible dates like 31/Jan -> 28/Feb.
    targetLocal_tm.tm_mday = lastDay;

  target = mktime(&targetLocal_tm);
  localtime_r(&target, &targetLocal_tm);
  
  // Month panel drawing
  drawMonthPanel(targetLocal_tm.tm_year, targetLocal_tm.tm_mon);
  screenCanvas.setClipRect(0, 0, PANEL_SIZE, PANEL_SIZE);
  panelCanvas.pushSprite(&screenCanvas, 0, 0);
  
  // Moon phase drawing
  double _moonAge = getMoonAge(target);
  drawMoonPanel(_moonAge);
  screenCanvas.setClipRect(0, PANEL_SIZE, PANEL_SIZE, PANEL_SIZE);
  panelCanvas.pushSprite(&screenCanvas, 0, PANEL_SIZE);
  
  // Calendar(This month) drawing
  drawCalendar(targetLocal_tm.tm_year, targetLocal_tm.tm_mon , false);
  screenCanvas.setClipRect(PANEL_SIZE, 0, screenCanvas.width() - PANEL_SIZE, screenCanvas.height());
  panelCanvas.pushSprite(&screenCanvas, PANEL_SIZE, 0);

  // Calendar(Next month) drawing
  drawCalendar(targetLocal_tm.tm_year, targetLocal_tm.tm_mon + 1, true);
  panelCanvas.setTextColor(TFT_BLACK, TFT_WHITE);
  panelCanvas.setTextSize(1.2);
  panelCanvas.setFont(&fonts::Font0);
  panelCanvas.setTextDatum(textdatum_t::top_center);
  panelCanvas.drawString(monthString[(targetLocal_tm.tm_mon + 1) % 12], PANEL_SIZE / 2, 6);
  screenCanvas.setClipRect(0, PANEL_SIZE * 2 + 10, PANEL_SIZE, PANEL_SIZE + 10);  // Added 10 pixels for moon age(y) and month name(h) area
  panelCanvas.pushSprite(&screenCanvas, 0, PANEL_SIZE * 2 + 10);

  // Text information drawing
  screenCanvas.setClipRect(0, 0, screenCanvas.width(), screenCanvas.height());
  screenCanvas.setTextSize(1);
  screenCanvas.setTextColor(TFT_BLACK, TFT_WHITE);
  screenCanvas.setCursor(22, 242);
  screenCanvas.printf("Moon Age:%.1f", _moonAge);
  screenCanvas.setCursor(45, 380);
  screenCanvas.printf("Battery:%d%% at ", M5.Power.getBatteryLevel());
  screenCanvas.printf("%04d-%02d-%02d %02d:%02d:%02d, time sync %s. ",
    targetLocal_tm.tm_year + 1900, 
    targetLocal_tm.tm_mon + 1, 
    targetLocal_tm.tm_mday, 
    targetLocal_tm.tm_hour, 
    targetLocal_tm.tm_min, 
    targetLocal_tm.tm_sec,
    isNtpSuccess ? "was successful." : "was skipped or failed."
  );
  screenCanvas.printf("IP:%s  ", WiFi.localIP().toString().c_str());

  // Low battery icon drawing
  if (batteryLow) 
    screenCanvas.drawPng(l_battery_png, sizeof(l_battery_png), screenCanvas.width() - 32, screenCanvas.height() - 32);
  

  // Fiinally, push the composed screenCanvas to the actual display
  screenCanvas.pushSprite(&M5.Display, BEZEL_SIZE, BEZEL_SIZE);

  if (!isPortalRunning) isLedRightWorking = false;  // Make LED stop here, because I don't want to use other flags to make task handling safe.

  drawTaskHandle = NULL;
  vTaskDelete(NULL);
}

/**
 * @brief Returns true when the device is running from external power.
 * For M5PaperColor, the PM1 power-source register is the reliable signal we have.
 * I intentionally avoid M5.Power.isCharging() because it is not reliable on this board, as of now.
 * https://github.com/m5stack/M5Unified/issues/307
 */
bool isExternalPowerConnected() {
#ifdef ARDUINO_M5STACK_PAPERCOLOR
  const auto source = M5.Power.M5pm1.getPowerSource();
  if (source == m5::M5PM1_Class::vin || source == m5::M5PM1_Class::vinout) {
    return true;
  }
  if (source == m5::M5PM1_Class::battery) {
    return false;
  }

  const int16_t vbusMv = M5.Power.getVBUSVoltage();
  if (vbusMv > 4000) {
    return true;
  }

  return false;
#else
  return M5.Power.getVBUSVoltage() > 4000;
#endif
}

#ifdef DEBUG_SLEEP_LOG
/**
 * @brief Returns the last power-source decision in a human-readable form for debug logging.
 */
const char* getPowerSourceDebugLabel() {
#ifdef ARDUINO_M5STACK_PAPERCOLOR
  const auto source = M5.Power.M5pm1.getPowerSource();
  switch (source) {
    case m5::M5PM1_Class::vin:
      return "vin";
    case m5::M5PM1_Class::vinout:
      return "vinout";
    case m5::M5PM1_Class::battery:
      return "battery";
    case m5::M5PM1_Class::unknown:
      return "unknown";
    default:
      return "unknown";
  }
#else
  return "n/a";
#endif
}
#endif

/**
 * @brief Creates a task pinned to core1 to redraw the screen asynchronously.
 */
void asyncRedrawScreen(int8_t displayMonthShift) {
  if (drawTaskHandle == NULL) {
    xTaskCreatePinnedToCore(
      reDrawScreen,     // Task function
      "RedrawScreen",   // Name of the task
      8192,             // Stack size in words
      reinterpret_cast<void*>(static_cast<intptr_t>(displayMonthShift)),
                        // Task input parameter
      1,                // Priority of the task
      &drawTaskHandle,  // Task handle
      1                 // Core where the task should run
    );
  }
}

/**
 * @brief (Helper function) Updates the HTML content for the local holidays editor based on the current local holidays data.
 */
void updateLocalHolidaysEditorHtml() {
  String escapedValue;
  escapedValue.reserve(strlen(localHolidays) * 2 + 1);

  for (size_t index = 0; localHolidays[index] != '\0'; index++) {
    char current = localHolidays[index];

    if (current == '\\')
      escapedValue += "\\\\";
    else if (current == '\'')
      escapedValue += "\\'";
    else if (current == '\n')
      escapedValue += "\\n";
    else if (current == '\r')
      escapedValue += "\\r";
    else if (current == '<')
      escapedValue += "\\x3C";
    else
      escapedValue += current;
  }

  String html =
    "<label for='localHolidaysEditor'>Holidays (Local) YAML format Max. 3KB</label><br/>"
    "<textarea id='localHolidaysEditor' rows='10' cols='40' style='overflow-y:scroll; width:95%; height:200px'></textarea>"
    "<script>(function(){var hidden=document.getElementsByName('localHolidays')[0];var editor=document.getElementById('localHolidaysEditor');"
    "if(hidden&&editor){editor.value='";
  html += escapedValue;
  html += "';hidden.value=editor.value;editor.oninput=function(){hidden.value=this.value;};}})();</script>";

  strlcpy(localHolidaysEditorHtml, html.c_str(), sizeof(localHolidaysEditorHtml));
}

/**
 * @brief Callback function that is called when the WiFiManager enters configuration mode.
 * @param myWiFiManager Pointer to the WiFiManager instance.
 */
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

/**
 * @brief Callback function to indicate that the WiFiManager should save the WiFi configuration.
 */
void saveWiFiConfigCallback() {
  Serial.println("Should save WiFi config");
  shouldSaveWiFiConfig = true;
}

/**
 * @brief Callback function to indicate that the WiFiManager should save the parameter configuration.
 */
void saveParamConfigCallback() {
  Serial.println("Should save parameter config");
  shouldSaveParamConfig = true;
  
  // Save the parameters to Preferences (NVS)
  if (param_ntp_server) {
    String newValue = param_ntp_server->getValue();
    newValue.trim();
    strcpy(ntp_server, newValue.c_str());
  }
  if (param_user_tz) {
    String newValue = param_user_tz->getValue();
    newValue.trim();
    strcpy(user_tz, newValue.c_str());
  }
  if (param_localCalendarEnabled) {
    String newValue = param_localCalendarEnabled->getValue();
    localCalendarEnabled = newValue.length() > 0 ? true : false;
  }
  if (param_refreshEvery) {
    String newValue = param_refreshEvery->getValue();
    refreshEvery = newValue.toInt();
    if (refreshEvery < 0) refreshEvery = 0;
    if (refreshEvery > 23) refreshEvery = 0;
  }
  if (param_drawCircleToday) {
    String newValue = param_drawCircleToday->getValue();
    drawCircleToday = newValue.length() > 0 ? true : false;
  }
  snprintf(
    drawCircleTodayHtml,
    sizeof(drawCircleTodayHtml),
    CHECKBOX_HTML_FORMAT,
    "drawCircleTodayEditor",
    drawCircleToday ? " checked" : "",
    "drawCircleTodayEditor",
    "Draw circle around today's date",
    "<br/>",
    "drawCircleToday",
    "drawCircleTodayEditor"
  );
  if (param_isWkStartsMon) {
    String newValue = param_isWkStartsMon->getValue();
    isWkStartsMon = newValue.length() > 0 ? true : false;
  }
  snprintf(
    isWkStartsMonHtml,
    sizeof(isWkStartsMonHtml),
    CHECKBOX_HTML_FORMAT,
    "isWkStartsMonEditor",
    isWkStartsMon ? " checked" : "",
    "isWkStartsMonEditor",
    "Week starts on Monday",
    "",
    "isWkStartsMon",
    "isWkStartsMonEditor"
  );
  if (param_localCalendarEnabled) {
    String newValue = param_localCalendarEnabled->getValue();
    localCalendarEnabled = newValue.length() > 0 ? true : false;
  }
  snprintf(
    localCalendarEnabledHtml,
    sizeof(localCalendarEnabledHtml),
    CHECKBOX_HTML_FORMAT,
    "localCalendarEnabledEditor",
    localCalendarEnabled ? " checked" : "",
    "localCalendarEnabledEditor",
    "Enable local calendar",
    "<br/>",
    "localCalendar",
    "localCalendarEnabledEditor"
  );
  if (param_localEraName) {
    String newValue = param_localEraName->getValue();
    newValue.trim();
    strcpy(localEraName, newValue.c_str());
  }
  if (param_localEraStartYear) {
    String newValue = param_localEraStartYear->getValue();
    localEraStartYear = newValue.toInt();
  }
  if (param_localHolidays) {
    String newValue = param_localHolidays->getValue();
    newValue.trim();
    strcpy(localHolidays, newValue.c_str());
  }
  updateLocalHolidaysEditorHtml();
  /*** add params to save here ***/
  
  prefs.begin("p_c_cal_cfg", false);
  prefs.putString("ntp_server", ntp_server);
  prefs.putString("user_tz", user_tz);
  prefs.putInt("refreshEvery", refreshEvery);
  prefs.putBool("drawCircleToday", drawCircleToday);
  prefs.putBool("isWkStartsMon", isWkStartsMon);
  prefs.putBool("localCalendar", localCalendarEnabled);
  prefs.putString("localEraName", localEraName);
  prefs.putInt("localEraStartYear", localEraStartYear);
  prefs.putString("localHolidays", localHolidays);
  prefs.end();

  setenv("TZ", user_tz, 1);
  tzset();
  if (strlen(localHolidays) == 0) {
    strlcpy(localHolidays, jp_holidays_yaml, sizeof(localHolidays));
  }
  holidayRoot = YAMLNode::loadString(localHolidays);
  holidayRulesLoaded = holidayRoot.isMap();
}

void setup() {
  // put your setup code here, to run once:
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  M5.Power.begin();
  Serial.println("Start setup");

  M5.Speaker.setVolume(128);
  delay(200);

  if (!skipBootToneOnWake) {
    // Since the first note is dropped on the actual device, a dummy note has been inserted.
    M5.Speaker.tone(NOTE_A7, 200);  // Dummy note
    delay(260);
    M5.Speaker.tone(NOTE_C5, 200);  // Play a tone for feedback
    delay(260);
    M5.Speaker.tone(NOTE_E5, 200);  // Play a tone for feedback
    delay(260);
    M5.Speaker.tone(NOTE_G5, 200);  // Play a tone for feedback
    delay(260);
  }
  skipBootToneOnWake = false;

#ifdef ARDUINO_M5STACK_PAPERCOLOR
  M5.Display.setRotation(3);    // Landscape mode for M5PaperColor
#endif
  scn_x = M5.Display.width();
  scn_y = M5.Display.height();
  M5.Display.setBrightness(128); // 0-255
  M5.Display.setTextSize(1.5);
  M5.Display.setEpdMode(::epd_quality);
  screenCanvas.setColorDepth(16);
  screenCanvas.createSprite(scn_x - BEZEL_SIZE * 2, scn_y - BEZEL_SIZE * 2);
  panelCanvas.setColorDepth(16);
  panelCanvas.createSprite(scn_x - BEZEL_SIZE * 2 - PANEL_SIZE, scn_y - BEZEL_SIZE * 2 - 10/*Text area*/);  // 470, 380
  M5.BtnA.setHoldThresh(3000); // Set the hold threshold for button A to 3000 milliseconds
  M5.BtnB.setHoldThresh(3000); // Set the hold threshold for button B to 3000 milliseconds
  M5.BtnC.setHoldThresh(3000); // Set the hold threshold for button C to 3000 milliseconds


#ifdef ARDUINO_M5STACK_PAPERCOLOR
  screenCanvas.setTextSize(2);
  pixels.begin(); // Initialize the NeoPixel library
  pixels.setBrightness(40); // Set brightness (0-255)
  pixels.clear(); // Clear the pixel
  pixels.show(); // Update the pixel to turn it off
#endif

  // Load the default holidays(Japanese) YAML from the included file Max 4096 bytes
  strlcpy(localHolidays, jp_holidays_yaml, sizeof(localHolidays));
  holidayRoot = YAMLNode::loadString(localHolidays);
  holidayRulesLoaded = holidayRoot.isMap();
  if (!holidayRulesLoaded)
    Serial.println("Failed to parse holidays from header.");
  else
    Serial.println("Holidays parsed successfully.");
  
  // Read the saved network configuration from NVS.
  prefs.begin("wifi_ip_cfg", true);
  const bool hasSavedWifiSsid = prefs.getString("wifi_ssid", "").length() > 0;

  staticIpEnabled = prefs.getString("static_ip", "").length() > 0; // Check if static IP is set
  if (staticIpEnabled) {
    prefs.getString("static_ip", "").toCharArray(static_ip, 24);
    prefs.getString("gateway", "").toCharArray(gateway, 24);
    prefs.getString("subnetMask", "255.255.255.0").toCharArray(subnetMask, 24);
    prefs.getString("pri_dns", "").toCharArray(pri_dns, 24);
    prefs.getString("sec_dns", "").toCharArray(sec_dns, 24);
  }
  prefs.end();

  // WiFi Manager setup for initial configuration portal
  WiFiManager wm_init;
  std::vector<const char*> initMenu = {"wifi", "info", "exit"};   // Menu items for the initial configuration portal
  wm_init.setMenu(initMenu);
  wm_init.setSaveConfigCallback(saveWiFiConfigCallback);
  wm_init.setAPCallback(configModeCallback);
  wm_init.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  // If saved static IP is valid, set it before connecting
  if (staticIpEnabled && strlen(static_ip) > 0) {
    if (ip.fromString(static_ip) && gw.fromString(gateway) && sn.fromString(subnetMask)) {
      if (strlen(pri_dns) != 0)
        dns1.fromString(pri_dns);
      if (strlen(sec_dns) != 0)
        dns2.fromString(sec_dns);

      if (dns1 != IPAddress(0, 0, 0, 0)) {
        wm_init.setSTAStaticIPConfig(ip, gw, sn, dns1);
      }
      else {
        wm_init.setSTAStaticIPConfig(ip, gw, sn);
        screenCanvas.println("DNS not configured");
      }
    }
    else {
      screenCanvas.println("Static IP not found");
    }
  }
  
  // Add custom parameters for static IP configuration UI in AP mode
  // This web UI used once, so the parameters are local
  WiFiManagerParameter p_ip("ip", "Static IP (Leave these blank for DHCP)", static_ip, 24 - 1);
  WiFiManagerParameter p_gw("gw", "Gateway", gateway, 24 - 1);
  WiFiManagerParameter p_sn("sn", "Subnet Mask", subnetMask, 24 - 1);
  WiFiManagerParameter p_dns1("dns1", "Primary DNS", pri_dns, 24 - 1);
  WiFiManagerParameter p_dns2("dns2", "Secondary DNS", sec_dns, 24 - 1);
  wm_init.addParameter(&p_ip);
  wm_init.addParameter(&p_gw);
  wm_init.addParameter(&p_sn);
  wm_init.addParameter(&p_dns1);
  wm_init.addParameter(&p_dns2);
  
  // Read the saved configuration from NVS
  prefs.begin("p_c_cal_cfg", true);
  prefs.getString("ntp_server", "ntp.nict.jp").toCharArray(ntp_server, 64);
  prefs.getString("user_tz", "JST-9").toCharArray(user_tz, 32);
  prefs.getInt("refreshEvery", 0) ? refreshEvery = prefs.getInt("refreshEvery", 0) : refreshEvery = 0;
  
  prefs.getBool("drawCircleToday", true) ? drawCircleToday = true : drawCircleToday = false;
  snprintf(
    drawCircleTodayHtml,
    sizeof(drawCircleTodayHtml),
    CHECKBOX_HTML_FORMAT,
    "drawCircleTodayEditor",
    drawCircleToday ? " checked" : "",
    "drawCircleTodayEditor",
    "Draw circle around today's date",
    "<br/>",
    "drawCircleToday",
    "drawCircleTodayEditor"
  );
  
  prefs.getBool("isWkStartsMon", false) ? isWkStartsMon = true : isWkStartsMon = false;
  snprintf(
    isWkStartsMonHtml,
    sizeof(isWkStartsMonHtml),
    CHECKBOX_HTML_FORMAT,
    "isWkStartsMonEditor",
    isWkStartsMon ? " checked" : "",
    "isWkStartsMonEditor",
    "Week starts on Monday",
    "",
    "isWkStartsMon",
    "isWkStartsMonEditor"
  );
  
  prefs.getBool("localCalendar", false) ? localCalendarEnabled = true : localCalendarEnabled = false;
  snprintf(
    localCalendarEnabledHtml,
    sizeof(localCalendarEnabledHtml),
    CHECKBOX_HTML_FORMAT,
    "localCalendarEnabledEditor",
    localCalendarEnabled ? " checked" : "",
    "localCalendarEnabledEditor",
    "Enable local calendar",
    "<br/>",
    "localCalendar",
    "localCalendarEnabledEditor"
  );
  
  prefs.getString("localEraName", "R.").toCharArray(localEraName, 8);
  prefs.getInt("localEraStartYear", 2019) ? localEraStartYear = prefs.getInt("localEraStartYear", 2019) : localEraStartYear = 2019;
  
  prefs.getString("localHolidays", jp_holidays_yaml).toCharArray(localHolidays, sizeof(localHolidays));
  updateLocalHolidaysEditorHtml();
  setenv("TZ", user_tz, 1);
  tzset();
  if (strlen(localHolidays) == 0) {
    strlcpy(localHolidays, jp_holidays_yaml, sizeof(localHolidays));
  }
  holidayRoot = YAMLNode::loadString(localHolidays);
  holidayRulesLoaded = holidayRoot.isMap();
  
  /*** add prefs to read here ***/
  prefs.end();

  // Add custom parameters configuration UI in AP mode
  // This web UI used multiple times, so the parameters are global
  param_ntp_server = new WiFiManagerParameter("ntp_server", "NTP Server", ntp_server, 64 - 1);
  param_user_tz = new WiFiManagerParameter("user_tz", "Timezone", user_tz, 32 - 1);
  param_isWkStartsMon = new WiFiManagerParameter("isWkStartsMon", "", isWkStartsMon ? "T" : "", 2, "type='hidden'", WFM_NO_LABEL);
  param_isWkStartsMonEditor = new WiFiManagerParameter(isWkStartsMonHtml);
  param_drawCircleToday = new WiFiManagerParameter("drawCircleToday", "", drawCircleToday ? "T" : "", 2, "type='hidden'", WFM_NO_LABEL);
  param_drawCircleTodayEditor = new WiFiManagerParameter(drawCircleTodayHtml);
  param_refreshEvery = new WiFiManagerParameter("refreshEvery", "Refresh every(o'clock)<br />(Refresh +5 min later, -1 is no auto refresh)", String(refreshEvery).c_str(), 4);
  param_localCalendarEnabled = new WiFiManagerParameter("localCalendar", "", localCalendarEnabled ? "" : "", 2, "type='hidden'", WFM_NO_LABEL);
  param_localCalendarEnabledEditor = new WiFiManagerParameter(localCalendarEnabledHtml);
  param_localEraName = new WiFiManagerParameter("localEraName", "Local era name (1-4 chars. ANK only)", localEraName, 8 - 1);
  param_localEraStartYear = new WiFiManagerParameter("localEraStartYear", "Local era start year (4 chars. Numbers)", String(localEraStartYear).c_str(), 4);
  param_localHolidays = new WiFiManagerParameter("localHolidays", "", "", sizeof(localHolidays) - 1, "type='hidden'", WFM_NO_LABEL);
  param_localHolidaysEditor = new WiFiManagerParameter(localHolidaysEditorHtml);
  /*** add params in web UI here ***/
  
  wm_config.addParameter(param_ntp_server);
  wm_config.addParameter(param_user_tz);
  wm_config.addParameter(param_isWkStartsMon);
  wm_config.addParameter(param_isWkStartsMonEditor);
  wm_config.addParameter(param_drawCircleToday);
  wm_config.addParameter(param_drawCircleTodayEditor);
  wm_config.addParameter(param_refreshEvery);
  wm_config.addParameter(param_localCalendarEnabled);
  wm_config.addParameter(param_localCalendarEnabledEditor);
  wm_config.addParameter(param_localEraName);
  wm_config.addParameter(param_localEraStartYear);
  wm_config.addParameter(param_localHolidays);
  wm_config.addParameter(param_localHolidaysEditor);
  /*** add params in web UI here ***/


  wm_config.setSaveParamsCallback(saveParamConfigCallback);
  wm_config.setAPCallback(configModeCallback);
  wm_config.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  std::vector<const char*> configMenu = {"param", "info", "exit"};  // Menu items for the configuration portal after initial setup
  wm_config.setMenu(configMenu);
  wm_config.setConfigPortalBlocking(false);

  // Auto-connect start
  Serial.println("Connecting to WiFi...");
  Serial.println();

  if (!hasSavedWifiSsid) {    // First time setup, show the initial WiFi setup instructions on the screen
    screenCanvas.fillSprite(TFT_WHITE);
    screenCanvas.setTextColor(TFT_BLACK);
    screenCanvas.setFont(MONTH_FONT_SMALL);
    screenCanvas.setTextSize(1.5);
    screenCanvas.setCursor(0, 10);
    screenCanvas.println("PaperColor Calendar Initial WiFi setup");
    screenCanvas.setTextSize(1);
    screenCanvas.println("Connect to WiFi(bellow) and configure your own WiFi settings.");
    screenCanvas.println("");
    screenCanvas.println("SSID: M5 PaperColor Init mode");
    screenCanvas.printf("PASS: %s\n", AP_PASS);
    screenCanvas.println("(Web portal adress:  192.168.4.1)");
    screenCanvas.println("");
    screenCanvas.println("When you exit the web setup, this device will restart automatically.");
    screenCanvas.pushSprite(&M5.Display, 10, 5);
  }

#ifdef ARDUINO_M5STACK_PAPERCOLOR
  pixels.setPixelColor(LED_R, pixels.Color(0, 0, 255));
  pixels.show();
#endif

  if (!wm_init.autoConnect("M5 PaperColor Init mode", AP_PASS)) { // Arguments only used at AP mode
    Serial.println("Failed to connect and hit timeout");
    ESP.restart();
  }
  else {
    Serial.println("WiFi connected");
    Serial.printf("Success: %s\n\n", ip == IPAddress(0, 0, 0, 0) ? "DHCP" : ip.toString().c_str());
  }

  if (staticIpEnabled && dns2 != IPAddress(0, 0, 0, 0)) {   // WiFi Manager does not support secondary DNS configuration, so we need to set it manually if it's configured.
    WiFi.config(ip, gw, sn, dns1, dns2);
    Serial.println("Secondary DNS configured");
  }
  else if (staticIpEnabled)
    screenCanvas.println("Secondary DNS not configured");

#ifdef ARDUINO_M5STACK_PAPERCOLOR
  pixels.setPixelColor(LED_R, pixels.Color(0, 0, 0));
  pixels.show();
#endif
  
  // If new configuration was send, save it to NVS
  if (shouldSaveWiFiConfig) {
    String input_ip = p_ip.getValue();
    input_ip.trim();
    //screenCanvas.printf("Input Static IP: %s\n", input_ip.c_str());
    Serial.printf("Input Static IP: %s\n", input_ip.c_str());

    if (input_ip.length() > 0) {
      staticIpEnabled = true;
      Serial.println("Static IP enabled");
      strcpy(static_ip, input_ip.c_str());
      strcpy(gateway, p_gw.getValue());
      strcpy(subnetMask, p_sn.getValue());
      strcpy(pri_dns, p_dns1.getValue());
      strcpy(sec_dns, p_dns2.getValue());
    }
    else {
      staticIpEnabled = false;
      Serial.println("Dynamic IP enabled");
      strcpy(static_ip, "");
    }

    prefs.begin("wifi_ip_cfg", false);
    prefs.putString("wifi_ssid", WiFi.SSID());
    prefs.putString("static_ip", static_ip);
    prefs.putString("gateway", gateway);
    prefs.putString("subnetMask", subnetMask);
    prefs.putString("pri_dns", pri_dns);
    prefs.putString("sec_dns", sec_dns);
    prefs.end();

    Serial.println("Settings saved. Restarting...");
    delay(2000);
    ESP.restart();
  }

  // Timezone setting
  setenv("TZ", user_tz, 1);
  tzset();

#ifdef ARDUINO_M5STACK_PAPERCOLOR
  pixels.setPixelColor(LED_R, pixels.Color(0, 0, 255));
  pixels.show();
#endif
  
  asyncRedrawScreen(0);    // NTP sync was moved to the reDrawScreen to avoid blocking the main loop.
  
#ifdef ARDUINO_M5STACK_PAPERCOLOR
  pixels.setPixelColor(LED_R, pixels.Color(0, 0, 0));
  pixels.show();
#endif

  startTimeMillis = millis();   // Start the timer for deep sleep

#ifdef DEBUG_SLEEP_LOG
  debugSleepStartMillis = millis();
  debugSleepLogged = false;

  prefs.begin("sleep_dbg", true);
  const uint32_t dbg_elapsed = prefs.getUInt("elapsed", 0);
  const int dbg_refresh = prefs.getInt("refresh", 0);
  const bool dbg_drawNull = prefs.getBool("drawNull", false);
  const bool dbg_portal = prefs.getBool("portal", false);
  const bool dbg_charging = prefs.getBool("charging", false);
  const int dbg_shift = prefs.getInt("shift", 0);
  const int dbg_psrc = prefs.getInt("psrc", -1);
  const int dbg_vbus = prefs.getInt("vbus", -1);
  prefs.end();

  Serial.printf("sleep_dbg elapsed=%lu refresh=%d drawNull=%s portal=%s charging=%s psrc=%d vbus=%d shift=%d\n",
    static_cast<unsigned long>(dbg_elapsed),
    dbg_refresh,
    dbg_drawNull ? "true" : "false",
    dbg_portal ? "true" : "false",
    dbg_charging ? "true" : "false",
    dbg_psrc,
    dbg_vbus,
    dbg_shift);
#endif
}

void loop() {
  // put your main code here, to run repeatedly:
  M5.update();
  if (isPortalRunning) wm_config.process();

#ifdef DEBUG_SLEEP_LOG
  if (!debugSleepLogged && millis() - debugSleepStartMillis >= 5UL * 60UL * 1000UL) {
    debugSleepLogged = true;
    const auto powerSource = M5.Power.M5pm1.getPowerSource();
    const int16_t vbusMv = M5.Power.getVBUSVoltage();

    prefs.begin("sleep_dbg", false);
    prefs.putUInt("elapsed", static_cast<uint32_t>(millis() - debugSleepStartMillis));
    prefs.putInt("refresh", refreshEvery);
    prefs.putBool("drawNull", drawTaskHandle == NULL);
    prefs.putBool("portal", isPortalRunning);
    prefs.putBool("charging", isExternalPowerConnected());
    prefs.putInt("psrc", static_cast<int>(powerSource));
    prefs.putInt("vbus", static_cast<int>(vbusMv));
    prefs.putInt("shift", currentDisplayMonthShift);
    prefs.end();
  }
#endif

#ifdef ARDUINO_M5STACK_PAPERCOLOR
  // LED blinking logic
  ulong currentLedMillis = millis();
  uint8_t r = 0, g = 128;
  if (ledLeftOn) {
    if (currentLedMillis - previousLedMillisLeft >= 500) {
      ledLeftOn = false;
      previousLedMillisLeft = currentLedMillis;
    }
  }
  else {
    if (currentLedMillis - previousLedMillisLeft >= 4000) {
      ledLeftOn = true;
      uint8_t batteryLevel = M5.Power.getBatteryLevel();
      if (batteryLow)
        batteryLow = (batteryLevel < BATTERY_LOW_EXIT);
      else
        batteryLow = (batteryLevel < BATTERY_LOW_ENTER);
      previousLedMillisLeft = currentLedMillis;
    }
  }

  if (batteryLow) { r = 255; g = 0; }

  if (isLedRightWorking) {
    if (currentLedMillis - previousLedMillisRight >= 1000) {
      ledRightOn = !ledRightOn;
      previousLedMillisRight = currentLedMillis;
    }
  }
  else {
    ledRightOn = false;
  }

  if (ledRightOn) {
    pixels.setPixelColor(LED_R, pixels.Color(0, 0, 255)); // Blue for right LED
  }
  else
    pixels.setPixelColor(LED_R, pixels.Color(0, 0, 0));
  
  if (ledLeftOn)
    pixels.setPixelColor(LED_L, pixels.Color(r, g, 0)); // Green for left LED
  else
    pixels.setPixelColor(LED_L, pixels.Color(0, 0, 0));
  
  pixels.show(); // Update the NeoPixel LEDs
#endif

  // Threshold for all button hold detection is set to 3000 milliseconds in setup()
  if (M5.BtnA.wasReleasedAfterHold()) {   // (CONFIG) Top right button in landscape
    /*
    * Toggle the WiFiManager configuration portal. It will start the portal in AP mode
    * with the specified SSID and password. If the portal is already running, it will stop
    * the portal and switch back to STA mode. 
    */
    M5.Speaker.tone(NOTE_C5, 10);   // Dummy tone
    delay(20);
    M5.Speaker.tone(NOTE_A7, 100);  // Play a tone for feedback
    delay(200);
    M5.Speaker.tone(NOTE_A7, 100);  // Play a tone for feedback

    if (!isPortalRunning) {
      snprintf(
        isWkStartsMonHtml,
        sizeof(isWkStartsMonHtml),
        CHECKBOX_HTML_FORMAT,
        "isWkStartsMonEditor",
        isWkStartsMon ? " checked" : "",
        "isWkStartsMonEditor",
        "Week starts on Monday",
        "",
        "isWkStartsMon",
        "isWkStartsMonEditor"
      );
      snprintf(
        localCalendarEnabledHtml,
        sizeof(localCalendarEnabledHtml),
        CHECKBOX_HTML_FORMAT,
        "localCalendarEnabledEditor",
        localCalendarEnabled ? " checked" : "",
        "localCalendarEnabledEditor",
        "Enable local calendar",
        "<br/>",
        "localCalendar",
        "localCalendarEnabledEditor"
      );
      snprintf(
        drawCircleTodayHtml,
        sizeof(drawCircleTodayHtml),
        CHECKBOX_HTML_FORMAT,
        "drawCircleTodayEditor",
        drawCircleToday ? " checked" : "",
        "drawCircleTodayEditor",
        "Draw circle around today's date",
        "<br/>",
        "drawCircleToday",
        "drawCircleTodayEditor"
      );
      updateLocalHolidaysEditorHtml();
      WiFi.mode(WIFI_AP_STA);
      Serial.printf("Config Portal SSID: %s\n", "M5 PaperColor Config mode");
      wm_config.startConfigPortal("M5 PaperColor Config mode", AP_PASS);
      isPortalRunning = true;
      isLedRightWorking = true;
    }
    else {
      Serial.println("Stopping WiFiManager Config Portal");
      wm_config.stopConfigPortal();
      WiFi.mode(WIFI_STA);
      isPortalRunning = false;
      isLedRightWorking = false;

      if (shouldSaveParamConfig) {
        shouldSaveParamConfig = false;
        currentDisplayMonthShift = 0;
        asyncRedrawScreen(0);
      }
    }

    startTimeMillis = millis();   // Reset the timer for deep sleep
  }
  else if (M5.BtnA.wasReleased()) {       // (Forward)
    /*
    * Shift forward the displayed month and redraw the screen.
    */
    isLedRightWorking = true;   // false is in the end of reDrawScreen.

    // NTP sync will be skipped when shifting months.
    M5.Speaker.tone(NOTE_A7, 100); // Play a tone for feedback
    currentDisplayMonthShift++; // Shift to next month
    asyncRedrawScreen(currentDisplayMonthShift);

    startTimeMillis = millis();   // Reset the timer for deep sleep
  }

  if (M5.BtnB.wasReleasedAfterHold()) {   // (RESET) Top left(center) button in landscape
    /*
     * Reset WiFi and IP configuration. This will clear the saved WiFi and IP settings from NVS and restart the device.
     */
    M5.Speaker.tone(NOTE_C5, 10);   // Dummy tone
    delay(20);
    M5.Speaker.tone(NOTE_A7, 100);  // Play a tone for feedback
    delay(200);
    M5.Speaker.tone(NOTE_A7, 100);
    delay(200);
    M5.Speaker.tone(NOTE_A7, 100);
    
    Serial.println("!!! RESET WIFI & IP CONFIG !!!");
    Serial.println("Please wait...");

    WiFiManager wm_init;
    wm_init.resetSettings();

    Preferences prefs;
    prefs.begin("wifi_ip_cfg", false);
    prefs.remove("static_ip");
    prefs.remove("gateway");
    prefs.remove("subnetMask");
    prefs.remove("pri_dns");
    prefs.remove("sec_dns");
    prefs.remove("wifi_ssid");
    prefs.end();

    Serial.println("Settings cleared. Restarting...");
    delay(2000);
    ESP.restart();
  }
  else if (M5.BtnB.wasReleased()) {       // (Backward)
    /*
    * Shift backward the displayed month and redraw the screen.
    */
    isLedRightWorking = true;   // false is in the end of reDrawScreen.

    
    // NTP sync will be skipped when shifting months.
    M5.Speaker.tone(NOTE_A7, 100); // Play a tone for feedback
    currentDisplayMonthShift--; // Shift to previous month
    asyncRedrawScreen(currentDisplayMonthShift);

    startTimeMillis = millis();   // Reset the timer for deep sleep
  }

  if (M5.BtnC.wasReleased()) {            // (Current month) Side large button in landscape
    /*
     * Redraw the screen. This will trigger the reDrawScreen task to update the display
     * with the current moon age and time information.
     */
     isLedRightWorking = true;   // false is in the end of reDrawScreen.

    // NTP sync was moved to the reDrawScreen to avoid blocking the main loop.
    M5.Speaker.tone(NOTE_A7, 100); // Play a tone for feedback
    currentDisplayMonthShift = 0; // Reset to current month
    asyncRedrawScreen(0);

    startTimeMillis = millis();   // Reset the timer for deep sleep
  }

  // Dispose of the draw task handle if the task has been deleted
  if (drawTaskHandle != NULL && eTaskGetState(drawTaskHandle) == eDeleted)
    drawTaskHandle = NULL;

  // After 5min. of inactivity, the device will enter deep sleep mode if refreshEvery is set to a valid hour (0-23).
  if (millis() - startTimeMillis >= RUNTIME_MS && refreshEvery != -1 && drawTaskHandle == NULL && !isPortalRunning) {
    if (currentDisplayMonthShift != 0) {  // If the displayed month is not the current month, force a redraw to the current month before sleeping
      currentDisplayMonthShift = 0;       // Reset to current month before sleeping
      asyncRedrawScreen(0);
      startTimeMillis = millis();         // Re-arm the idle timer after the forced redraw
      return;
    }

    if (isExternalPowerConnected()) {
      startTimeMillis = millis(); // Devices fed from external power must not be put to sleep by timerSleep
      return;
    }

    m5::rtc_time_t alarmTime;
    alarmTime.hours = refreshEvery;
    if (alarmTime.hours < 0) alarmTime.hours = 0;   // Ensure hours 0-23
    if (alarmTime.hours > 23) alarmTime.hours = 0;
    alarmTime.minutes = 5;    // Wait 5 minutes after the hour to avoid the device's clock drift
    alarmTime.seconds = 0;

    skipBootToneOnWake = true;
    M5.Display.waitDisplay();
  #ifdef ARDUINO_M5STACK_PAPERCOLOR
    pixels.setPixelColor(LED_R, pixels.Color(0, 0, 0));
    pixels.setPixelColor(LED_L, pixels.Color(0, 0, 0));
    pixels.show(); // Update the NeoPixel LEDs
  #endif
    M5.Speaker.tone(NOTE_A7, 100); // Play a tone for feedback
    delay(200);
    M5.Power.timerSleep(alarmTime); // Enter deep sleep mode with timer wakeup
  }
  
  delay(10);
}

// put function definitions here:
