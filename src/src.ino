#include "configs.h"
#include "BatteryInterface.h"
#include "Buffer.h"
#include "settings.h"
#include "display.h"
#include "GpsInterface.h"
#include "SDInterface.h"
#include "Switches.h"
#include "WiFiOps.h"
#include "utils.h"
#include "ui.h"
#include "logger.h"

Buffer buffer;
Settings settings;
GpsInterface gps;
BatteryInterface battery;
WiFiOps wifi_ops;
Utils utils;
UI ui_obj;
bool g_force_display_redraw = false;

SPIClass sharedSPI(SPI);
Display display = Display(&sharedSPI, TFT_CS, TFT_DC, TFT_RST);
SDInterface sd_obj = SDInterface(&sharedSPI, SD_CS);

Switches u_btn = Switches(U_BTN, 1000, U_PULL);
Switches d_btn = Switches(D_BTN, 1000, D_PULL);
Switches c_btn = Switches(C_BTN, 1000, C_PULL);

void setup() {
  Serial.begin(115200);

  while (!Serial)
    delay(10);

  // Do SPI stuff first
  sharedSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

  // Give SPI some time I guess
  delay(100);

  // Init the display before SD
  display.begin();

  // Give SD some time
  delay(100);

  // Show us IDF information
  Logger::log(STD_MSG, "ESP-IDF version is: " + String(esp_get_idf_version()));

  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  // Load settings
  settings.begin();

  if (settings.getSettingType(SETTING_SANITY) == "") {
    Logger::log(WARN_MSG, "Current settings format not supported. Installing new default settings...");
    settings.createDefaultSettings(SPIFFS);
  }
  else {
    Logger::log(GUD_MSG, "Current settings format supported");
  }

  // Init our buffer for writing logs
  buffer = Buffer();

  // Init SD Card
  if(!sd_obj.initSD())
    Logger::log(WARN_MSG, "SD Card NOT Supported");

  // Check for firmware updates now
  Logger::log(STD_MSG, "Checking for firmware updates...");
  sd_obj.runUpdate();

  // Enable SD debug logging if setting is on
  Logger::enableSDLog(settings.loadSetting<bool>(DEBUG_LOG_NAME));

  // Log the reset reason immediately after SD logging is enabled so it
  // lands in debug.log. Critical for diagnosing mid-wardrive crashes —
  // watchdog vs panic vs brownout each point to different root causes.
  {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reason_str = "UNKNOWN";
    switch (reason) {
      case ESP_RST_POWERON:   reason_str = "POWERON";   break;
      case ESP_RST_EXT:       reason_str = "EXT_PIN";   break;
      case ESP_RST_SW:        reason_str = "SOFTWARE";  break;
      case ESP_RST_PANIC:     reason_str = "PANIC";     break;
      case ESP_RST_INT_WDT:   reason_str = "INT_WDT";   break;
      case ESP_RST_TASK_WDT:  reason_str = "TASK_WDT";  break;
      case ESP_RST_WDT:       reason_str = "WDT";       break;
      case ESP_RST_DEEPSLEEP: reason_str = "DEEPSLEEP"; break;
      case ESP_RST_BROWNOUT:  reason_str = "BROWNOUT";  break;
      case ESP_RST_SDIO:      reason_str = "SDIO";      break;
      default:                reason_str = "UNKNOWN";   break;
    }
    Logger::log(WARN_MSG, String("[BOOT] Reset reason: ") + reason_str);
  }

  // Init battery
  battery.RunSetup();
  battery.battery_level = battery.getBatteryLevel();

  // Init GPS
  gps.begin();

  ui_obj.begin();

  // Init wifi and bluetooth
  wifi_ops.begin(c_btn.justPressed());

  // Init UI
  ui_obj.begin();

  settings.printJsonSettings(settings.getSettingsString());

  Logger::log(GUD_MSG, "Initialization complete!");
}

void loop() {
  // Take current time of this loop for functions
  uint32_t currentTime = millis();

  // Refresh all functions
  wifi_ops.main(currentTime);
  settings.main(currentTime);
  battery.main(currentTime);
  gps.main();
  sd_obj.main();
  buffer.save();
  ui_obj.main(currentTime);

  // Solo or Core modes
  if ((gps.getFixStatus()) && (sd_obj.supported) && (ui_obj.stat_display_mode != SD_FILES))
    wifi_ops.setCurrentScanMode(WIFI_WARDRIVING);
  // Nodes
  else if ((wifi_ops.run_mode == NODE_MODE) && (wifi_ops.getNodeReady())) {
    wifi_ops.setCurrentScanMode(WIFI_WARDRIVING);
    digitalWrite(LED_PIN, HIGH);
  }
  else {
    wifi_ops.setCurrentScanMode(WIFI_STANDBY);
    if (wifi_ops.run_mode == NODE_MODE)
      digitalWrite(LED_PIN, LOW);
  }
}
