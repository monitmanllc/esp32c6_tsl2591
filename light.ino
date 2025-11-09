#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2591.h>

// I2C pins for ESP32-C6
#define I2C_SDA 6
#define I2C_SCL 7

// WiFi credentials
const char* ssid = "SomeWifiNetwork";
const char* password = "ILovePonies";

// Server endpoint
const char* serverUrl = "https://app.monitman.com/dashboard/receive.php?sensor=lux";

// Create TSL2591 instance
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);

// Timing
unsigned long lastUpload = 0;
const unsigned long uploadInterval = 60000; // Upload every 60 seconds

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("TSL2591 WiFi Data Logger");
  
  // Initialize I2C with custom pins
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(100); // Give I2C time to initialize
  
  Serial.println("I2C initialized on SDA=GPIO6, SCL=GPIO7");
  
  // Scan for I2C devices
  scanI2C();
  
  // Initialize TSL2591
  if (!tsl.begin()) {
    Serial.println("Failed to find TSL2591 sensor!");
    while (1) delay(1000);
  }
  
  Serial.println("TSL2591 sensor found!");
  
  // Configure sensor
  configureSensor();
  
  // Connect to WiFi
  connectWiFi();
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    connectWiFi();
  }
  
  // Check if it's time to upload
  if (millis() - lastUpload >= uploadInterval) {
    // Read sensor
    uint16_t visible, ir, full;
    float lux;
    
    if (readSensor(visible, ir, full, lux)) {
      // Upload data
      uploadData(lux, visible, ir, full);
    } else {
      Serial.println("Failed to read sensor");
    }
    
    lastUpload = millis();
  }
  
  delay(100);
}

void configureSensor() {
  // Set gain: TSL2591_GAIN_LOW (1x), TSL2591_GAIN_MED (25x), 
  //           TSL2591_GAIN_HIGH (428x), TSL2591_GAIN_MAX (9876x)
  tsl.setGain(TSL2591_GAIN_MED);
  
  // Set integration time:
  // TSL2591_INTEGRATIONTIME_100MS, TSL2591_INTEGRATIONTIME_200MS,
  // TSL2591_INTEGRATIONTIME_300MS, TSL2591_INTEGRATIONTIME_400MS,
  // TSL2591_INTEGRATIONTIME_500MS, TSL2591_INTEGRATIONTIME_600MS
  tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);
  
  Serial.println("Sensor configured:");
  Serial.print("Gain: ");
  tsl2591Gain_t gain = tsl.getGain();
  switch(gain) {
    case TSL2591_GAIN_LOW:
      Serial.println("1x (Low)");
      break;
    case TSL2591_GAIN_MED:
      Serial.println("25x (Medium)");
      break;
    case TSL2591_GAIN_HIGH:
      Serial.println("428x (High)");
      break;
    case TSL2591_GAIN_MAX:
      Serial.println("9876x (Max)");
      break;
  }
  
  Serial.print("Integration time: ");
  Serial.print((tsl.getTiming() + 1) * 100);
  Serial.println(" ms");
}

void scanI2C() {
  Serial.println("\nScanning I2C bus...");
  byte count = 0;
  
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (i < 16) Serial.print("0");
      Serial.print(i, HEX);
      Serial.println();
      count++;
      
      // TSL2591 default address is 0x29
      if (i == 0x29) {
        Serial.println("  ^ This is the TSL2591!");
      }
    }
  }
  
  if (count == 0) {
    Serial.println("No I2C devices found!");
    Serial.println("Check wiring:");
    Serial.println("  - SDA should be on GPIO 6");
    Serial.println("  - SCL should be on GPIO 7");
    Serial.println("  - VCC connected to 3.3V");
    Serial.println("  - GND connected to GND");
  } else {
    Serial.print("Found ");
    Serial.print(count);
    Serial.println(" device(s)");
  }
  Serial.println();
}

bool readSensor(uint16_t &visible, uint16_t &ir, uint16_t &full, float &lux) {
  // Read the sensor
  uint32_t lumFull = tsl.getFullLuminosity();
  ir = lumFull >> 16;
  full = lumFull & 0xFFFF;
  visible = full - ir;
  lux = tsl.calculateLux(full, ir);
  
  // Check for valid reading
  if (lux < 0) {
    return false;
  }
  
  Serial.println("\n--- Sensor Reading ---");
  Serial.print("IR: "); Serial.println(ir);
  Serial.print("Visible: "); Serial.println(visible);
  Serial.print("Full: "); Serial.println(full);
  Serial.print("Lux: "); Serial.println(lux, 2);
  
  return true;
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

void uploadData(float lux, uint16_t visible, uint16_t ir, uint16_t full) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping upload");
    return;
  }
  
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  
  // Manually format JSON string
  String jsonPayload = "{";
  jsonPayload += "\"sensor\":\"TSL2591\",";
  jsonPayload += "\"timestamp\":" + String(millis()) + ",";
  jsonPayload += "\"lux\":" + String(lux, 2) + ",";
  jsonPayload += "\"visible\":" + String(visible) + ",";
  jsonPayload += "\"ir\":" + String(ir) + ",";
  jsonPayload += "\"full\":" + String(full);
  jsonPayload += "}";
  
  Serial.println("\n--- Uploading Data ---");
  Serial.println("JSON: " + jsonPayload);
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println("Response: " + response);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
    Serial.println(http.errorToString(httpResponseCode));
  }
  
  http.end();
}
