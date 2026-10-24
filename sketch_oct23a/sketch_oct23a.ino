#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>
#include <Preferences.h>
#include <time.h>

// =================== CẤU HÌNH WIFI ===================
const char* ssid     = "TuanTrinh";
const char* password = "mangdanghong";

// =================== CẤU HÌNH CHÂN (S - V - G) ===================
#define PIN_OLED_SDA      21   // D21 (Cột S)
#define PIN_OLED_SCL      22   // D22 (Cột S)
#define PIN_SOIL_ADC      34   // D34 (Cột S - Cảm biến độ ẩm đất)
#define PIN_WATER_ADC     35   // D35 (Cột S - Cảm biến đo mực nước bình chứa)
#define PIN_RELAY         23   // D23 (Cột S - Relay máy bơm)
#define PIN_TOUCH         4    // D4  (Cột S - Nút cảm ứng TTP223)
#define PIN_DHT           15   // D15 (Cột S - Cảm biến DHT11)

#define DHTTYPE           DHT11

// Relay Active LOW
#define RELAY_ON          LOW
#define RELAY_OFF         HIGH

#define MAX_PUMP_SECONDS  60 
#define WATER_EMPTY_THRESHOLD 15 // Dưới 15% nước là báo động cạn

// =================== KHỞI TẠO ĐỐI TƯỢNG ===================
Adafruit_SH1106G display(128, 64, &Wire, -1);
DHT dht(PIN_DHT, DHTTYPE);
WebServer server(80);
Preferences prefs;

// =================== BIẾN TOÀN CỤC ===================
bool isAutoMode = true;
bool isPumpOn = false;
bool oledFound = false;
bool isWaterAvailable = true;
bool isPumpTimeoutAlert = false;

int soilDryThreshold = 40;
int soilWetThreshold = 65;

float airTemp = 0.0;
float airHumidity = 0.0;
int soilPercent = 0;
int soilRaw = 0;

int waterPercent = 100;
int waterRaw = 0;

bool touchState = LOW;
unsigned long pumpStartTime = 0;
unsigned long pumpDurationSeconds = 0;

unsigned long lastSensorRead = 0;
unsigned long lastOledUpdate = 0;
const unsigned long SENSOR_INTERVAL = 1500;
const unsigned long OLED_INTERVAL   = 300;

bool lastTouchState = LOW;

// =================== BẬT / TẮT BƠM AN TOÀN ===================
void setPump(bool state) {
  // Nếu cạn nước -> Ngăn chặn bật bơm
  if (state && !isWaterAvailable) {
    Serial.println(F("[CANH BAO] Khong the bat bom: Binh chua het nuoc!"));
    isPumpOn = false;
    digitalWrite(PIN_RELAY, RELAY_OFF);
    return;
  }

  isPumpOn = state;
  digitalWrite(PIN_RELAY, isPumpOn ? RELAY_ON : RELAY_OFF);

  if (isPumpOn) {
    pumpStartTime = millis();
    isPumpTimeoutAlert = false;
  } else {
    pumpDurationSeconds = 0;
  }

  Serial.print(F("[RELAY] Trang thai bom: "));
  Serial.println(isPumpOn ? F("DANG BAT") : F("DANG TAT"));
}

// =================== LẤY THỜI GIAN NTP ===================
String getCurrentTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "--:--:--";
  }
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  return String(timeStr);
}

// =================== ĐỌC CẢM BIẾN & ĐO MỰC NƯỚC ===================
void readSensors() {
  // 1. Đọc cảm biến đo mực nước bình chứa (GPIO 35)
  waterRaw = analogRead(PIN_WATER_ADC);
  // Hiệu chuẩn: khô ngoài không khí ~ 200, nhúng ngập vạch ~ 2600
  waterPercent = map(waterRaw, 200, 2600, 0, 100);
  waterPercent = constrain(waterPercent, 0, 100);

  // Điều kiện cạn nước: Mực nước < 15%
  isWaterAvailable = (waterPercent >= WATER_EMPTY_THRESHOLD);

  // Khóa bơm lập tức nếu cạn nước
  if (isPumpOn && !isWaterAvailable) {
    setPump(false);
    Serial.println(F("[AN TOAN] Ngat bom khan cap: Binh chua het nuoc!"));
  }

  // 2. Đọc DHT11
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    airHumidity = h;
    airTemp = t;
  }

  // 3. Đọc độ ẩm đất (GPIO 34)
  soilRaw = analogRead(PIN_SOIL_ADC);
  const int DRY_RAW = 3500;
  const int WET_RAW = 1400;
  soilPercent = map(soilRaw, DRY_RAW, WET_RAW, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  // 4. Đọc nút chạm
  touchState = digitalRead(PIN_TOUCH);

  // 5. Kiểm tra thời gian bơm liên tục
  if (isPumpOn) {
    pumpDurationSeconds = (millis() - pumpStartTime) / 1000;
    if (pumpDurationSeconds >= MAX_PUMP_SECONDS) {
      setPump(false);
      isPumpTimeoutAlert = true;
      Serial.println(F("[AN TOAN] Ngat bom tu dong vi da bom qua 60 giay!"));
    }
  }

  Serial.print(F("[DATA] Dat: ")); Serial.print(soilPercent);
  Serial.print(F("% | Nuoc: ")); Serial.print(waterPercent);
  Serial.print(F("% | Nhiet: ")); Serial.print(airTemp);
  Serial.println(F("C"));
}

// =================== HIỂN THỊ OLED ===================
void updateOLED() {
  if (!oledFound) return;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // Dòng 1: Tiêu đề & Giờ
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(isAutoMode ? F("AUTO") : F("MANU"));
  display.setCursor(40, 0);
  display.print(F("GARDEN"));
  display.setCursor(80, 0);
  display.print(getCurrentTimeString());
  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Cảnh báo cạn nước ưu tiên
  if (!isWaterAvailable) {
    display.fillRect(0, 12, 127, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK, SH110X_WHITE);
    display.setCursor(6, 14);
    display.print(F("CANH BAO: HET NUOC!"));
    display.setTextColor(SH110X_WHITE);
  } else {
    // Dòng 2: Mực nước bình chứa (%)
    display.setCursor(0, 13);
    display.print(F("Binh nuoc: "));
    display.print(waterPercent);
    display.print(F("% ["));
    display.print(waterPercent > 50 ? F("DAY") : F("TB"));
    display.print(F("]"));
  }

  // Dòng 3: Độ ẩm đất
  display.setCursor(0, 27);
  display.print(F("Do am dat: "));
  display.setTextSize(2);
  display.setCursor(68, 25);
  display.print(soilPercent);
  display.print(F("%"));

  // Dòng 4: Khí hậu & Máy bơm
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print((int)airTemp);
  display.print(F("C|"));
  display.print((int)airHumidity);
  display.print(F("% "));

  if (isPumpOn) {
    display.fillRect(48, 46, 52, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK, SH110X_WHITE);
    display.setCursor(52, 49);
    display.print(F("DANG BAT"));
    display.setTextColor(SH110X_WHITE);
    display.setCursor(102, 49);
    display.print(pumpDurationSeconds);
    display.print(F("s"));
  } else {
    display.setCursor(48, 48);
    display.print(isPumpTimeoutAlert ? F("NGAT(60s)") : F("DANG TAT"));
  }

  display.display();
}

// =================== REST API CHO WEB ===================
void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "ESP32 Water Monitoring Ready");
}

void handleData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");

  String json = "{";
  json += "\"temp\":" + String(airTemp, 1) + ",";
  json += "\"hum\":" + String(airHumidity, 1) + ",";
  json += "\"soil\":" + String(soilPercent) + ",";
  json += "\"raw\":" + String(soilRaw) + ",";
  json += "\"water_pct\":" + String(waterPercent) + ",";
  json += "\"water_raw\":" + String(waterRaw) + ",";
  json += "\"touch\":" + String(touchState ? "true" : "false") + ",";
  json += "\"pump\":" + String(isPumpOn ? "true" : "false") + ",";
  json += "\"mode\":\"" + String(isAutoMode ? "AUTO" : "MANU") + "\",";
  json += "\"water\":" + String(isWaterAvailable ? "true" : "false") + ",";
  json += "\"timeout_alert\":" + String(isPumpTimeoutAlert ? "true" : "false") + ",";
  json += "\"pump_time\":" + String(pumpDurationSeconds) + ",";
  json += "\"dry_th\":" + String(soilDryThreshold) + ",";
  json += "\"wet_th\":" + String(soilWetThreshold) + ",";
  json += "\"time\":\"" + getCurrentTimeString() + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleTogglePump() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");

  isAutoMode = false;
  setPump(!isPumpOn);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSetMode() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");

  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    isAutoMode = (m == "auto");
  } else {
    isAutoMode = !isAutoMode;
  }
  server.send(200, "application/json", "{\"mode\":\"" + String(isAutoMode ? "AUTO" : "MANU") + "\"}");
}

void handleSetThresholds() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");

  if (server.hasArg("dry") && server.hasArg("wet")) {
    soilDryThreshold = server.arg("dry").toInt();
    soilWetThreshold = server.arg("wet").toInt();

    prefs.putInt("dry", soilDryThreshold);
    prefs.putInt("wet", soilWetThreshold);
  }

  server.send(200, "application/json", "{\"status\":\"saved\"}");
}

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=========================================="));
  Serial.println(F("   HE THONG TUOI & GIAM SAT MUC NUOC PRO  "));
  Serial.println(F("=========================================="));

  // 1. Đọc cấu hình Flash
  prefs.begin("garden", false);
  soilDryThreshold = prefs.getInt("dry", 40);
  soilWetThreshold = prefs.getInt("wet", 65);

  // 2. Chân phần cứng
  pinMode(PIN_RELAY, OUTPUT);
  setPump(false);
  pinMode(PIN_TOUCH, INPUT_PULLDOWN);
  pinMode(PIN_SOIL_ADC, INPUT);
  pinMode(PIN_WATER_ADC, INPUT);

  // 3. Khởi động cảm biến & OLED
  dht.begin();
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setTimeOut(50);

  if (display.begin(0x3C, true) || display.begin(0x3D, true)) {
    oledFound = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(15, 20);
    display.println(F("SMART GARDEN"));
    display.setCursor(20, 36);
    display.println(F("GIAM SAT NUOC"));
    display.display();
    delay(1000);
  }

  // 4. Kết nối WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 12) {
    delay(400);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WiFi] Da ket noi WiFi thanh cong!"));
    Serial.print(F(">>> IP ESP32: "));
    Serial.println(WiFi.localIP());
    MDNS.begin("smartgarden");
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  } else {
    Serial.println(F("\n[WiFi] Phat WiFi du phong: ESP32_SmartGarden"));
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32_SmartGarden", "12345678");
    Serial.print(F(">>> IP: http://"));
    Serial.println(WiFi.softAPIP());
  }

  // 5. REST API
  server.on("/", handleRoot);
  server.on("/api/data", handleData);
  server.on("/api/toggle", handleTogglePump);
  server.on("/api/mode", handleSetMode);
  server.on("/api/settings", handleSetThresholds);
  server.begin();

  readSensors();
}

// =================== LOOP ===================
void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();

  // 1. Nút chạm TTP223
  bool touch = digitalRead(PIN_TOUCH);
  if (lastTouchState == LOW && touch == HIGH) {
    isAutoMode = false;
    setPump(!isPumpOn);
    delay(50);
  }
  lastTouchState = touch;

  // 2. Đo cảm biến định kỳ
  if (currentMillis - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = currentMillis;
    readSensors();

    if (isAutoMode && isWaterAvailable) {
      if (soilPercent < soilDryThreshold && !isPumpOn) {
        setPump(true);
      } else if (soilPercent >= soilWetThreshold && isPumpOn) {
        setPump(false);
      }
    }
  }

  // 3. Cập nhật OLED
  if (currentMillis - lastOledUpdate >= OLED_INTERVAL) {
    lastOledUpdate = currentMillis;
    updateOLED();
  }
}
