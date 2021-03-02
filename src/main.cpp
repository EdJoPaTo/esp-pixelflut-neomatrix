// NEOPIXEL BEST PRACTICES for most reliable operation:
// - Add 1000 uF CAPACITOR between NeoPixel strip's + and - connections.
// - MINIMIZE WIRING LENGTH between microcontroller board and first pixel.
// - NeoPixel strip's DATA-IN should pass through a 300-500 OHM RESISTOR.
// - AVOID connecting NeoPixels on a LIVE CIRCUIT. If you must, ALWAYS
//   connect GROUND (-) first, then +, then data.
// - When using a 3.3V microcontroller with a 5V-powered NeoPixel strip,
//   a LOGIC-LEVEL CONVERTER on the data line is STRONGLY RECOMMENDED.
// (Skipping these may work OK on your workbench but can fail in the field)

#include <Adafruit_NeoMatrix.h>
#include <ArduinoOTA.h>
#include <credentials.h>
#include <EspMQTTClient.h>
#include <LinkedList.h>
#include <MqttKalmanPublish.h>

#define CLIENT_NAME "espPixelflut"

EspMQTTClient client(
  WIFI_SSID,
  WIFI_PASSWORD,
  MQTT_SERVER,  // MQTT Broker server ip
  CLIENT_NAME,     // Client name that uniquely identify your device
  1883              // The MQTT port, default to 1883. this line can be omitted
);

const bool MQTT_RETAINED = true;

#define BASIC_TOPIC CLIENT_NAME "/"
#define BASIC_TOPIC_SET BASIC_TOPIC "set/"
#define BASIC_TOPIC_STATUS BASIC_TOPIC "status/"


const int PIN_MATRIX = 13; // D7
const int PIN_ON = 5; // D1

// MATRIX DECLARATION:
// Parameter 1 = width of NeoPixel matrix
// Parameter 2 = height of matrix
// Parameter 3 = pin number (most are valid)
// Parameter 4 = matrix layout flags, add together as needed:
//   NEO_MATRIX_TOP, NEO_MATRIX_BOTTOM, NEO_MATRIX_LEFT, NEO_MATRIX_RIGHT:
//     Position of the FIRST LED in the matrix; pick two, e.g.
//     NEO_MATRIX_TOP + NEO_MATRIX_LEFT for the top-left corner.
//   NEO_MATRIX_ROWS, NEO_MATRIX_COLUMNS: LEDs are arranged in horizontal
//     rows or in vertical columns, respectively; pick one or the other.
//   NEO_MATRIX_PROGRESSIVE, NEO_MATRIX_ZIGZAG: all rows/columns proceed
//     in the same order, or alternate lines reverse direction; pick one.
//   See example below for these values in action.
// Parameter 5 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)


// Example for NeoPixel Shield.  In this application we'd like to use it
// as a 5x8 tall matrix, with the USB port positioned at the top of the
// Arduino.  When held that way, the first pixel is at the top right, and
// lines are arranged in columns, progressive order.  The shield uses
// 800 KHz (v2) pixels that expect GRB color data.
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(32, 8, PIN_MATRIX,
  NEO_MATRIX_BOTTOM     + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB            + NEO_KHZ800);


WiFiServer pixelflutServer(1337);
LinkedList<WiFiClient> pixelflutClients;

MQTTKalmanPublish mkRssi(client, BASIC_TOPIC_STATUS "rssi", MQTT_RETAINED, 12 * 5 /* every 5 min */, 10);

boolean on = true;
uint8_t mqttBri = 5;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  pinMode(PIN_ON, OUTPUT);
  Serial.begin(115200);

  digitalWrite(PIN_ON, on ? HIGH : LOW);

  matrix.begin();
  matrix.setBrightness(mqttBri);
  matrix.show();

  ArduinoOTA.setHostname(CLIENT_NAME);

  // Optional functionnalities of EspMQTTClient
  client.enableDebuggingMessages();
  client.enableHTTPWebUpdater();
  client.enableLastWillMessage(BASIC_TOPIC "connected", "0", MQTT_RETAINED);
}

void onConnectionEstablished() {
  client.subscribe(BASIC_TOPIC_SET "bri", [](const String & payload) {
    int value = strtol(payload.c_str(), 0, 10);
    mqttBri = max(1, min(255, value));
    matrix.setBrightness(mqttBri * on);
    client.publish(BASIC_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  });

  client.subscribe(BASIC_TOPIC_SET "on", [](const String & payload) {
    boolean value = payload != "0";
    on = value;
    digitalWrite(PIN_ON, on ? HIGH : LOW);
    matrix.setBrightness(mqttBri * on);
    client.publish(BASIC_TOPIC_STATUS "on", String(on), MQTT_RETAINED);
  });

  client.publish(BASIC_TOPIC "connected", "2", MQTT_RETAINED);
  client.publish(BASIC_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  client.publish(BASIC_TOPIC_STATUS "on", String(on), MQTT_RETAINED);

  ArduinoOTA.begin();

  pixelflutServer.begin();
  pixelflutServer.setNoDelay(true);
}

void handlePixelflutInput(WiFiClient &client, String str) {
  Serial.print("Pixelflut Command: ");
  Serial.println(str);

  str.toLowerCase();

  if (str == "help") {
    client.println("there is no help yet");
  } else if (str == "size") {
    char buffer[20];
    if (snprintf(buffer, sizeof(buffer), "SIZE %d %d", matrix.width(), matrix.height()) >= 0) {
      client.println(buffer);
    }
  } else if (str.startsWith("px")) {
    auto s = str.c_str();
    s += 3;

    char *ptr;

    int16_t x = strtol(s, &ptr, 10);
    int16_t y = strtol(ptr, &ptr, 10);

    auto length = strnlen(ptr, 20);

    if (length == 0) {
      char buffer[20];
      if (snprintf(buffer, sizeof(buffer), "PX %d %d unknown", x, y) >= 0) {
        client.println(buffer);
      }
    } else if (length == 6 + 1) {
      // normal hex color
      char buffer[20];

      strncpy(buffer, ptr + 1, 2);
      auto red = strtoul(buffer, 0, 16);
      strncpy(buffer, ptr + 3, 2);
      auto green = strtoul(buffer, 0, 16);
      strncpy(buffer, ptr + 5, 2);
      auto blue = strtoul(buffer, 0, 16);

      matrix.drawPixel(x, y, matrix.Color(red, green, blue));
    } else {
      client.println("colorcode not implemented. Use 6 digit hex color.");
    }
  } else {
    client.println("unknown command. try help");
  }
}

void pixelflutLoop() {
  for (auto i = pixelflutClients.size(); i > 0; i--) {
    auto client = pixelflutClients.get(i - 1);

    if (client.status() == CLOSED) {
      pixelflutClients.remove(i - 1);

      Serial.print("Client left. Remaining: ");
      Serial.println(pixelflutClients.size());
    }
  }

  while (pixelflutServer.hasClient()) {
    auto client = pixelflutServer.available();
    client.setNoDelay(true);
    client.flush();

    Serial.print("Client new: ");
    Serial.print(client.remoteIP().toString());
    Serial.print(":");
    Serial.println(client.remotePort());

    pixelflutClients.add(client);
  }

  for (auto i = 0; i < pixelflutClients.size(); i++) {
    auto client = pixelflutClients.get(i);

    String input;
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        handlePixelflutInput(client, input);
        input = "";
      } else if (c >= 32) {
        input += c;
      }
    }
  }
}

unsigned long nextMeasure = 0;
unsigned long nextMatrixUpdate = 0;

void loop() {
  client.loop();
  ArduinoOTA.handle();
  pixelflutLoop();
  digitalWrite(LED_BUILTIN, client.isConnected() ? HIGH : LOW);

  auto now = millis();

  if (now > nextMatrixUpdate) {
    nextMatrixUpdate = now + 25;
    matrix.show();
  }

  if (client.isConnected()) {
    if (now > nextMeasure) {
      nextMeasure = now + 5000;
      long rssi = WiFi.RSSI();
      float avgRssi = mkRssi.addMeasurement(rssi);
      Serial.print("RSSI        in dBm:     ");
      Serial.print(String(rssi).c_str());
      Serial.print("   Average: ");
      Serial.println(String(avgRssi).c_str());
    }
  }
}
