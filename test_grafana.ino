#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>

// Thay thế bằng thông tin Wi-Fi và Telemetry Harbor
const char* ssid = "PhongTro";
const char* password = "08031978";
const char* serverName = "https://telemetryharbor.com/api/v1/ingest/ingest/357";
const char* deviceToken = "Fe8k7wftw_xddH5-6ikTTfrSw54s7FDCHjy2_ssEQT0";

// Khởi tạo DHT11
#define DHTPIN 5 // Chân GPIO kết nối với DHT11 (GPIO5 trên ESP32-C3)
#define DHTTYPE DHT11 // Loại cảm biến
DHT dht(DHTPIN, DHTTYPE);

// NTP Client để lấy thời gian
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void setup() {
  Serial.begin(115200);
  // Kết nối Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Khởi động DHT11
  dht.begin();

  // Khởi động NTP
  timeClient.begin();
  timeClient.setTimeOffset(25200); // GMT+7 cho Việt Nam (7*3600 giây)
}

void loop() {
  timeClient.update();
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Kiểm tra lỗi đọc từ DHT11
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT11 sensor!");
    delay(2000); // Thử lại sau 2 giây
    return;
  }

  // Gửi dữ liệu đến Telemetry Harbor
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(deviceToken));

    String jsonPayload = "{\"temperature\":" + String(temperature) + 
                        ",\"humidity\":" + String(humidity) + 
                        ",\"timestamp\":" + String(timeClient.getEpochTime()) + "}";

    int httpResponseCode = http.POST(jsonPayload);
    if (httpResponseCode > 0) {
      Serial.println("Data sent successfully, HTTP code: " + String(httpResponseCode));
    } else {
      Serial.println("Error sending data: " + String(httpResponseCode));
    }
    http.end();
  }
  delay(60000); // Gửi dữ liệu mỗi 60 giây
}