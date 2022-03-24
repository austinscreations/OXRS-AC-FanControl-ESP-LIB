#include <OXRS_Fan.h>

// Serial
#define       SERIAL_BAUD_RATE      115200

// Fan control
OXRS_Fan oxrsFan;

void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("OXRS_Fan: SimpleExample"));
  Serial.println(F("========================================"));
  Serial.println();

  // Start the I2C bus
  Wire.begin();
  
  // Scan for and initialise any fan controllers found on the I2C bus
  oxrsFan.begin();

  Serial.println();
  Serial.println(F("Display telemetry data every 60 seconds..."));
}

void loop() 
{
  // Publish fan telemetry to serial
  DynamicJsonDocument telemetry(4096);
  oxrsFan.getTelemetry(telemetry.as<JsonVariant>());

  if (telemetry.size() > 0)
  {
    serializeJson(telemetry, Serial);
    Serial.println();
  }
}