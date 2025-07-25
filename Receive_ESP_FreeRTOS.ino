#include <SPI.h>
#include <mcp2515.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// WiFi
WiFiMulti wifiMulti;
#define WIFI_SSID "PhongTro"
#define WIFI_PASSWORD "08031978"

// InfluxDB
#define DEVICE "ESP32-C3"
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "nH40j9Zv7RN1wvK8cVj_EDQRqZw95pZPdrZ54uv8rV4lAe8gqX3pB9mnbvJcZ02V7wtalO_Pb3W6q70a8QeT2Q=="
#define INFLUXDB_ORG "RTOS"
#define INFLUXDB_BUCKET "sensor_data"
#define TZ_INFO "UTC7"

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// CAN
#define SPI_SCK  6
#define SPI_MOSI 7
#define SPI_MISO 2
#define SPI_CS   10

// LCD
#define SDA_PIN  0
#define SCL_PIN  1

// L298N
// Quạt
#define EnA 18
#define In1 19
#define In2 3
// Bơm
#define In3 8
#define In4 9

LiquidCrystal_I2C lcd(0x27, 16, 2);

SPIClass *spiCAN = nullptr;
#undef SPI
#define SPI (*spiCAN)

MCP2515 mcp2515(SPI_CS);

const int freq = 2000; 
const int resolution = 8;

struct can_frame canMsg;
struct SensorData {
  int temp;
  int hum;
  int flame;
};
// SensorData rdata;

QueueHandle_t serverQueue;      // Hàng đợi gửi dữ liệu lên server
QueueHandle_t dataQueue;      // Hàng đợi truyền dữ liệu
SemaphoreHandle_t dataMutex;  // Mutex bảo vệ các task

// ---------- TASK: Đọc CAN ----------
void taskCAN(void *parameters) {
  SensorData rdata;
  for (;;) {
    if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
      rdata.temp  = canMsg.data[0];
      rdata.hum   = canMsg.data[1];
      rdata.flame = (canMsg.data[2] << 8) | canMsg.data[3];
      
      xQueueOverwrite(dataQueue, &rdata);   // ghi các giá trị cảm biến mới nhất
      xQueueOverwrite(serverQueue, &rdata);
      
      Serial.print("Temperature: ");
      Serial.println(rdata.temp);
      Serial.print("Humidity: ");
      Serial.println(rdata.hum);
      Serial.print("Flame: ");
      Serial.println(rdata.flame);
    }
    // vTaskDelay(10 / portTICK_PERIOD_MS);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------- TASK: Xử lý – điều khiển ----------
void taskControl(void *parameters) {
  SensorData rdata;
  for (;;) {
   // if (xQueuePeek(dataQueue, &rdata, portMAX_DELAY)) {
   if (xQueuePeek(dataQueue, &rdata, pdMS_TO_TICKS(200)) == pdTRUE) {
      // Điều khiển BƠM 
      if (rdata.flame < 500) {           
        digitalWrite(In3, HIGH);
        digitalWrite(In4, LOW);

        digitalWrite(In1, LOW);
        digitalWrite(In2, LOW);
        ledcWrite(EnA, 0);
      } else {
        digitalWrite(In3, LOW);
        digitalWrite(In4, LOW);
        
        // Điều khiển QUẠT theo nhiệt độ
        if (rdata.temp > 40) {
          digitalWrite(In1, HIGH); 
          digitalWrite(In2, LOW);
          ledcWrite(EnA, 255);
        } else if (rdata.temp > 33) {
          digitalWrite(In1, HIGH); 
          digitalWrite(In2, LOW);
          ledcWrite(EnA, 100);
        } else {
          digitalWrite(In1, LOW);  
          digitalWrite(In2, LOW);
          ledcWrite(EnA, 0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ---------- TASK: Hiển thị LCD ----------
void taskLCD(void *parameters) {
  SensorData rdata;
  for (;;) {
    if (xQueuePeek(dataQueue, &rdata, pdMS_TO_TICKS(200)) == pdTRUE) {
    // if (xQueueReceive(dataQueue, &rdata, pdMS_TO_TICKS(200)) == pdTRUE) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      lcd.clear();
      if (rdata.flame < 500) {
        lcd.setCursor(0,0); 
        lcd.print("Fire: Yes");
        lcd.setCursor(0,1); 
        lcd.print("Turn on Pump");
      } else {
        lcd.setCursor(0, 0);
        lcd.print("T: ");
        lcd.print(rdata.temp);
        lcd.setCursor(8, 0);
        lcd.print("H: ");
        lcd.print(rdata.hum);
        lcd.setCursor(0,1); 
        lcd.print("Fire: No ");
      }
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ---------- TASK: Tích hợp Grafana ----------
void taskGrafana(void *parameters) {
    client.setHTTPOptions(HTTPOptions().connectionReuse(true));
    client.setWriteOptions(
      WriteOptions()
        .batchSize(10)        // gửi 10 điểm một lần
        .flushInterval(5000)  // hoặc gửi sau 5 giây, tùy cái nào tới trước
    );
    Point point("sensor");
    point.addTag("device", DEVICE);
    
    SensorData rdata;
    for (;;) {
      if (xQueueReceive(serverQueue, &rdata, portMAX_DELAY)) {
        // Xóa field cũ
        point.clearFields(); 
        // Gửi lên InfluxDB
        point.addField("temp", rdata.temp);
        point.addField("hum", rdata.hum);
        point.addField("flame", rdata.flame);
        // point.setTimeToNow();
        client.writePoint(point);
      }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  Serial.begin(9600);

  // Khởi tạo MCP2515
  spiCAN = new SPIClass(FSPI);
  spiCAN->begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
  mcp2515.reset();
  mcp2515.setBitrate(CAN_125KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  // Khởi tạo LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init(); 
  // lcd.clear();
  lcd.backlight();

  // GPIO
  pinMode(In1, OUTPUT); 
  pinMode(In2, OUTPUT);
  ledcAttach(EnA, freq, resolution);
  pinMode(In3, OUTPUT); 
  pinMode(In4, OUTPUT);

  // WiFi cho ESP
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");

  if (client.validateConnection()) {
    Serial.println("Connected to InfluxDB");
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  // Tạo queue (1 phần tử – các giá trị cảm biến mới nhất)
  dataQueue = xQueueCreate(1, sizeof(SensorData));
  serverQueue = xQueueCreate(1, sizeof(SensorData));

  // Mutex cho LCD
  dataMutex = xSemaphoreCreateMutex();

  // Tạo task
  xTaskCreate(taskCAN, "CAN", 4096, NULL, 3, NULL);
  xTaskCreate(taskControl, "CONTROL", 4096, NULL, 2, NULL);
  xTaskCreate(taskLCD, "LCD", 4096, NULL, 1, NULL);
  xTaskCreate(taskGrafana, "Grafana", 5120, NULL, 1, NULL);

  // Không dùng loop(), nên treo vô hạn
  vTaskDelete(NULL);
}

void loop() {}
