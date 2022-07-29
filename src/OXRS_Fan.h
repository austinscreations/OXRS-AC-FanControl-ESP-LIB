#ifndef OXRS_FAN_H
#define OXRS_FAN_H

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Adafruit_EMC2101.h>

// I2C addresses for the TCA9548 I2C muxes
const byte    TCA_I2C_ADDRESS[]               = { 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77 };
const uint8_t TCA_COUNT                       = sizeof(TCA_I2C_ADDRESS);

// I2C address for the EMC2101 fan driver
#define       EMC_I2C_ADDRESS                   0x4C
#define       EMC_COUNT                         8

// How often to publish telemetry data (default to 60s)
#define       DEFAULT_PUBLISH_TELEMETRY_MS      60000L

// How long before reverting to onboard temp sensor after last external temp report (defaults to 90s)
#define       DEFAULT_EXTERNAL_TEMP_TIMEOUT_MS  90000L

class OXRS_Fan
{
public:
  void begin();
  void loop();
  
  void getTelemetry(JsonVariant json);

  void setConfigSchema(JsonVariant json);
  void setCommandSchema(JsonVariant json);

  void onConfig(JsonVariant json);
  void onCommand(JsonVariant json);

private:
  uint8_t _tcasFound = 0;
  uint8_t _emcsFound[TCA_COUNT];
  uint8_t _fansFound = 0;

  uint32_t _publishTelemetry_ms = DEFAULT_PUBLISH_TELEMETRY_MS;
  uint32_t _lastPublishTelemetry;

  uint32_t _externalTempTimeout_ms[TCA_COUNT * EMC_COUNT];
  uint32_t _lastExternalTemp[TCA_COUNT * EMC_COUNT];

  bool selectEMC(uint8_t tca, uint8_t emc);
  void scanI2CBus();

  void jsonFanConfig(JsonVariant json);
  void jsonFanCommand(JsonVariant json);

  uint8_t getFan(JsonVariant json);
};

#endif
