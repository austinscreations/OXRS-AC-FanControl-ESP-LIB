#include "OXRS_Fan.h"

// Fan driver (accessed on same address via I2C mux)
Adafruit_EMC2101 emc2101[TCA_COUNT];

/*
 *
 *  Main program
 *
 */
void OXRS_Fan::begin()
{
  // Scan the I2C bus for any TCA9548s (I2C muxes) and check each one
  // to see if any EMC2101 fan controllers are attached
  scanI2CBus();
}

void OXRS_Fan::loop()
{
  // Ignore if no fan controllers found
  if (_fansFound == 0) { return; }
  
  // Iterate through each of the TCA9548s found on the I2C bus
  for (uint8_t tca = 0; tca < TCA_COUNT; tca++)
  {
    if (bitRead(_tcasFound, tca) == 0)
      continue;

    // Iterate through each of the EMC2101s found on the TCA I2C mux
    for (uint8_t emc = 0; emc < EMC_COUNT; emc++)
    {
      if (bitRead(_emcsFound[tca], emc) == 0)
        continue;

      // Calculate the unique fan index
      uint8_t fan = (tca * EMC_COUNT) + emc + 1;

      // Ignore if there hasn't been an external temp report recently
      if (_lastExternalTemp[fan] == 0L)
        continue;

      // Ignore if external temp timeouts have been disabled
      if (_externalTempTimeout_ms[fan] == 0L)
        continue;
      
      // Check if we have been waiting too long
      if ((millis() - _lastExternalTemp[fan]) > _externalTempTimeout_ms[fan])
      {
        // Select the EMC2101 on our I2C mux
        if (!selectEMC(tca, emc))
          continue;

        // Disable external temperature handling, reverting to onboard sensor
        emc2101[tca].enableForcedTemperature(false);
        _lastExternalTemp[fan] = 0L;
      }
    }
  }
}

void OXRS_Fan::getTelemetry(JsonVariant json)
{
  // Ignore if no fan controllers found
  if (_fansFound == 0) { return; }
  
  // Ignore if publishing has been disabled
  if (_publishTelemetry_ms == 0) { return; }

  // Check if we are ready to publish
  if ((millis() - _lastPublishTelemetry) > _publishTelemetry_ms)
  {
    JsonArray telemetry = json.to<JsonArray>();
    
    // Iterate through each of the TCA9548s found on the I2C bus
    for (uint8_t tca = 0; tca < TCA_COUNT; tca++)
    {
      if (bitRead(_tcasFound, tca) == 0)
        continue;
  
      // Iterate through each of the EMC2101s found on the TCA I2C mux
      for (uint8_t emc = 0; emc < EMC_COUNT; emc++)
      {
        if (bitRead(_emcsFound[tca], emc) == 0)
          continue;

        // Select the EMC2101 on our I2C mux
        if (!selectEMC(tca, emc))
          continue;

        uint8_t fan       = (tca * EMC_COUNT) + emc + 1;
        float temperature = emc2101[tca].getExternalTemperature();
        uint8_t dutyCycle = emc2101[tca].getDutyCycle();
        uint16_t rpm      = emc2101[tca].getFanRPM();

        // EMC2101 returns a temp of 127 if nothing connected
        if (temperature == 127L)
          continue;

        // Add the telemetry data for this fan
        JsonObject item = telemetry.createNestedObject();
        item["fan"] = fan;
        item["running"] = rpm > 0;
        item["rpm"] = rpm;
        item["dutyCycle"] = dutyCycle;
        item["temperature"] = temperature;
      }
    }

    // Reset our timer
    _lastPublishTelemetry = millis();
  }
}

bool OXRS_Fan::selectEMC(uint8_t tca, uint8_t emc)
{
  if (tca >= TCA_COUNT) 
    return false;

  if (emc >= EMC_COUNT) 
    return false;

  Wire.beginTransmission(TCA_I2C_ADDRESS[tca]);
  Wire.write(1 << emc);
  Wire.endTransmission();

  return true;
}

void OXRS_Fan::scanI2CBus()
{
  Serial.println(F("[fan ] scanning for fan controllers..."));
  _fansFound = 0;

  for (uint8_t tca = 0; tca < TCA_COUNT; tca++) 
  {
    _emcsFound[tca] = 0;
    
    Serial.print(F(" - 0x"));
    Serial.print(TCA_I2C_ADDRESS[tca], HEX);
    Serial.print(F("..."));
  
    Wire.beginTransmission(TCA_I2C_ADDRESS[tca]);
    if (Wire.endTransmission() == 0)
    {
      bitWrite(_tcasFound, tca, 1);
      Serial.println(F("TCA9548"));

      for (uint8_t emc = 0; emc < EMC_COUNT; emc++)
      {
        // Select the EMC2101 on our I2C mux
        if (!selectEMC(tca, emc))
          continue;
    
        Serial.print(F("   - MUX port #")); 
        Serial.print(emc);
        Serial.print(F("..."));

        // The EMC2101 library prints a serial message if .begin() fails
        if (emc2101[tca].begin(EMC_I2C_ADDRESS))
        {
          uint8_t fan = (tca * EMC_COUNT) + emc + 1;
          
          bitWrite(_emcsFound[tca], emc, 1);
          Serial.print(F("EMC2101 (fan #"));
          Serial.print(fan);
          Serial.println(F(")"));

          // Set the default external temperature timeout
          _externalTempTimeout_ms[fan] = DEFAULT_EXTERNAL_TEMP_TIMEOUT_MS;
          _lastExternalTemp[fan] = 0L;
          
          // Keep track of how many fans we detected
          ++_fansFound;

          // Enable the lookup table and set hysteresis to 5 degrees
          emc2101[tca].LUTEnabled(true);
          emc2101[tca].setLUTHysteresis(5);

          // Set the default fan speed thresholds
          emc2101[tca].setLUT(0, 30, 25);
          emc2101[tca].setLUT(1, 40, 50);
          emc2101[tca].setLUT(2, 50, 100);
        }    
      }
    }
    else
    {
      Serial.println(F("empty"));
    }
  }
}

void OXRS_Fan::setConfigSchema(JsonVariant json)
{
  // Ignore if no fan controllers found
  if (_fansFound == 0) { return; }

  JsonObject publishFanTelemetrySeconds = json.createNestedObject("publishFanTelemetrySeconds");
  publishFanTelemetrySeconds["title"] = "Publish Fan Telemetry (seconds)";
  publishFanTelemetrySeconds["description"] = "How often to publish telemetry data from the fan controllers attached to your device (defaults to 60 seconds, setting to 0 disables telemetry reports). Must be a number between 0 and 86400 (i.e. 1 day).";
  publishFanTelemetrySeconds["type"] = "integer";
  publishFanTelemetrySeconds["minimum"] = 0;
  publishFanTelemetrySeconds["maximum"] = 86400;

  JsonObject fans = json.createNestedObject("fans");
  fans["title"] = "Fan Configuration";
  fans["description"] = "Add configuration for each fan attached to your device. The 1-based index specifies which fan you wish to configure. The external temperature sensor timeout defines how long after the last temperature update before reverting to the onboard temperature sensor for determining fan speed (defaults to 60 seconds, 0 means it will never revert, must be a number between 0 and 86400).";
  fans["type"] = "array";
  
  JsonObject items = fans.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject fan = properties.createNestedObject("fan");
  fan["title"] = "Fan";
  fan["type"] = "integer";
  fan["minimum"] = 1;
  fan["maximum"] = (TCA_COUNT * EMC_COUNT);

  JsonObject externalTemperatureTimeoutSeconds = properties.createNestedObject("externalTemperatureTimeoutSeconds");
  externalTemperatureTimeoutSeconds["title"] = "External Temperature Timeout (seconds)";
  externalTemperatureTimeoutSeconds["type"] = "integer";
  externalTemperatureTimeoutSeconds["minimum"] = 0;
  externalTemperatureTimeoutSeconds["maximum"] = 86400;

  JsonObject fanSpeedThresholds = properties.createNestedObject("fanSpeedThresholds");
  fanSpeedThresholds["title"] = "Fan Speed Thresholds";
  fanSpeedThresholds["description"] = "Add a series of temperature thresholds and required fan speeds. This allows you to configure your fan to ramp up as the temperature increases.";
  fanSpeedThresholds["type"] = "array";
  fanSpeedThresholds["minItems"] = 1;
  fanSpeedThresholds["maxItems"] = 8;

  JsonObject fanSpeedThresholdsItems = fanSpeedThresholds.createNestedObject("items");
  fanSpeedThresholdsItems["type"] = "object";
  
  JsonObject fanSpeedThresholdProperties = fanSpeedThresholdsItems.createNestedObject("properties");
  
  JsonObject fanSpeedThresholdTemp = fanSpeedThresholdProperties.createNestedObject("temperature");
  fanSpeedThresholdTemp["title"] = "Temperature (°C)";
  fanSpeedThresholdTemp["type"] = "integer";
  fanSpeedThresholdTemp["minimum"] = 0;
  fanSpeedThresholdTemp["maximum"] = 126;
  
  JsonObject fanSpeedThresholdDutyCycle = fanSpeedThresholdProperties.createNestedObject("dutyCycle");
  fanSpeedThresholdDutyCycle["title"] = "Duty Cycle (%)";
  fanSpeedThresholdDutyCycle["type"] = "integer";
  fanSpeedThresholdDutyCycle["minimum"] = 0;
  fanSpeedThresholdDutyCycle["maximum"] = 100;

  JsonArray fanSpeedThresholdsRequired = fanSpeedThresholdsItems.createNestedArray("required");
  fanSpeedThresholdsRequired.add("temperature");
  fanSpeedThresholdsRequired.add("dutyCycle");
  
  JsonArray required = items.createNestedArray("required");
  required.add("fan");
}

void OXRS_Fan::onConfig(JsonVariant json)
{
  // Ignore if no fan controllers found
  if (_fansFound == 0) { return; }

  if (json.containsKey("publishFanTelemetrySeconds"))
  {
    _publishTelemetry_ms = json["publishFanTelemetrySeconds"].as<uint32_t>() * 1000L;
  }

  if (json.containsKey("fans"))
  {
    for (JsonVariant fan : json["fans"].as<JsonArray>())
    {
      jsonFanConfig(fan);
    }
  }
}

void OXRS_Fan::jsonFanConfig(JsonVariant json)
{
  uint8_t fan = getFan(json);
  if (fan == 0) return;

  uint8_t tca = (fan - 1) / EMC_COUNT;
  uint8_t emc = (fan - 1) % EMC_COUNT;

  // Select the EMC2101 on our I2C mux
  if (!selectEMC(tca, emc))
    return;

  if (json.containsKey("externalTemperatureTimeoutSeconds"))
  {
    _externalTempTimeout_ms[fan] = json["externalTemperatureTimeoutSeconds"].as<uint8_t>();
  }
  
  if (json.containsKey("fanSpeedThresholds"))
  {
    JsonArray array = json["fanSpeedThresholds"].as<JsonArray>();
    uint8_t index = 0;
    
    for (JsonVariant v : array)
    {
      emc2101[tca].setLUT(index++, v["temperature"].as<uint8_t>(), v["dutyCycle"].as<uint8_t>());
    }
  }
}

void OXRS_Fan::setCommandSchema(JsonVariant json)
{
  // Ignore if no fan controllers found
  if (_fansFound == 0) { return; }

  JsonObject fans = json.createNestedObject("fans");
  fans["title"] = "Fan Commands";
  fans["description"] = "Send commands to one or more fans attached to your device. The 1-based index specifies which fan you wish to command. The duty cycle is used to manually control the fan speed (from 0 - 100%, setting to 0 will revert to automatic control based on temperature). External temperature reports (in °C) will be used in preference to the onboard temperature sensor. If no external temperature report is received after a while (configurable period) the fan will revert to using the onboard temperature sensor.";
  fans["type"] = "array";
  
  JsonObject items = fans.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject fan = properties.createNestedObject("fan");
  fan["title"] = "Fan";
  fan["type"] = "integer";
  fan["minimum"] = 1;
  fan["maximum"] = (TCA_COUNT * EMC_COUNT);

  JsonObject dutyCycle = properties.createNestedObject("dutyCycle");
  dutyCycle["title"] = "Duty Cycle (%)";
  dutyCycle["type"] = "integer";
  dutyCycle["minimum"] = 0;
  dutyCycle["maximum"] = 100;

  JsonObject externalTemperature = properties.createNestedObject("externalTemperature");
  externalTemperature["title"] = "External Temperature (°C)";
  externalTemperature["type"] = "integer";
  externalTemperature["minimum"] = 0;
  externalTemperature["maximum"] = 126;

  JsonArray required = items.createNestedArray("required");
  required.add("fan");
}

void OXRS_Fan::onCommand(JsonVariant json)
{
  // Ignore if no fan controllers found
  if (_fansFound == 0) { return; }

  if (json.containsKey("fans"))
  {
    for (JsonVariant fan : json["fans"].as<JsonArray>())
    {
      jsonFanCommand(fan);
    }
  }
}

void OXRS_Fan::jsonFanCommand(JsonVariant json)
{
  uint8_t fan = getFan(json);
  if (fan == 0) return;

  uint8_t tca = (fan - 1) / EMC_COUNT;
  uint8_t emc = (fan - 1) % EMC_COUNT;

  // Select the EMC2101 on our I2C mux
  if (!selectEMC(tca, emc))
    return;

  if (json.containsKey("dutyCycle"))
  {
    uint8_t dutyCycle = json["dutyCycle"].as<uint8_t>();
    
    // Revert to automatic control if duty cycle is 0%
    emc2101[tca].LUTEnabled(dutyCycle == 0);
    emc2101[tca].setDutyCycle(dutyCycle);
  }

  if (json.containsKey("externalTemperature"))
  {
    uint8_t externalTemperature = json["externalTemperature"].as<uint8_t>();
    
    // Enable forced temp control if valid report
    emc2101[tca].enableForcedTemperature(externalTemperature > 0);
    emc2101[tca].setForcedTemperature(externalTemperature);

    // Keep track of when we received the last external temp report
    _lastExternalTemp[fan] = millis();
  }
}

uint8_t OXRS_Fan::getFan(JsonVariant json)
{
  if (!json.containsKey("fan"))
  {
    Serial.println(F("[fan ] missing fan"));
    return 0;
  }
  
  uint8_t fan = json["fan"].as<uint8_t>();
  
  // Check the fan is valid for this device
  if (fan <= 0 || fan > (TCA_COUNT * EMC_COUNT))
  {
    Serial.println(F("[fan ] invalid fan"));
    return 0;
  }

  // Check the fan corresponds to an existing TCA/EMC (index is 1-based)
  uint8_t tca = (fan - 1) / EMC_COUNT;
  uint8_t emc = (fan - 1) % EMC_COUNT;
  
  if (bitRead(_tcasFound, tca) == 0)
  {
    Serial.println(F("[fan ] invalid fan, no TCA9548 found"));
    return 0;
  }

  if (bitRead(_emcsFound[tca], emc) == 0)
  {
    Serial.println(F("[fan ] invalid fan, no EMC2101 found"));
    return 0;
  }
  
  return fan;
}
