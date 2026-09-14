#include "logger.h"
#include <SD.h>
#include <time.h>

// Static member definitions
String Logger::ring[LOG_RING_SIZE];
int    Logger::ring_head      = 0;
int    Logger::ring_count     = 0;
bool   Logger::sd_log_enabled = false;

void Logger::enableSDLog(bool enable) {
  sd_log_enabled = enable;
  if (enable)
    Serial.println("[-] [DBG] SD debug logging enabled -> " + String(DEBUG_LOG_FILE));
  else
    Serial.println("[-] [DBG] SD debug logging disabled");
}

// Build a timestamp prefix for each log line.
// After GPS sets the system clock (settimeofday in GpsInterface::setGPSInfo)
// this returns a real wall-clock time: "[2026-09-03 20:02:14] "
// Before that, falls back to elapsed time since boot: "[T+00:01:23] "
// so the log is never timestamp-free.
static String buildTimestamp() {
  char buf[28];
  time_t now = time(nullptr);
  if (now > 1000000000L) {
    // System clock has been set from GPS — use wall-clock time
    struct tm* t = gmtime(&now);
    snprintf(buf, sizeof(buf), "[%04d-%02d-%02d %02d:%02d:%02d] ",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
  } else {
    // No GPS fix yet — use elapsed millis since boot
    uint32_t ms  = millis();
    uint32_t sec = ms / 1000;
    uint32_t h   = sec / 3600;
    uint32_t m   = (sec % 3600) / 60;
    uint32_t s   = sec % 60;
    snprintf(buf, sizeof(buf), "[T+%02u:%02u:%02u] ", h, m, s);
  }
  return String(buf);
}

void Logger::log(uint8_t type, String msg) {
  String prefix = "";

  if (type == WARN_MSG)
    prefix = "[!] ";
  else if (type == GUD_MSG)
    prefix = "[+] ";
  else
    prefix = "[-] ";

  String line = prefix + msg;

  // Truncate the message body FIRST, at the original LOG_LINE_MAX, so
  // adding the timestamp below doesn't eat into message content. Log
  // lines therefore keep exactly the same body as before timestamps
  // were introduced; the timestamp is purely additive.
  if (line.length() > LOG_LINE_MAX)
    line = line.substring(0, LOG_LINE_MAX - 3) + "...";

  // Prepend the timestamp after truncation
  line = buildTimestamp() + line;

  // Always write to Serial
  Serial.println(line);

  ring[ring_head] = line;
  ring_head = (ring_head + 1) % LOG_RING_SIZE;
  if (ring_count < LOG_RING_SIZE)
    ring_count++;

  // Write to SD debug log if enabled
  #ifdef HAS_SD
  if (sd_log_enabled) {
    File f = SD.open(DEBUG_LOG_FILE, FILE_APPEND);
    if (f) {
      f.println(line);
      f.close();
    }
  }
  #endif
}
