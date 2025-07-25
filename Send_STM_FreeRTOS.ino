#include <SPI.h>
#include <mcp2515.h>
#include <DHT.h>
#include <MapleFreeRTOS900.h>

// CAN
//#define SPI_SCK  PA5
//#define SPI_MOSI PA7
//#define SPI_MISO PA6
#define SPI_CS PA4

// DHT11 và KY026
#define DHTPIN PB10
#define DHTTYPE DHT11
#define KYPIN PA0

MCP2515* mcp2515;
DHT dht(DHTPIN, DHTTYPE);

struct can_frame canMsg;

struct SensorData {
  int temperature;
  int humidity;
  int flame;
};
SensorData sdata;

// Mutex bảo vệ các task
SemaphoreHandle_t dataMutex;

void taskReadDHT(void* parameters) {
  for (;;) {
    int t = dht.readTemperature();
    int h = dht.readHumidity();

    // if (!isnan(t) && !isnan(h)) {
    // xSemaphoreTake(dataMutex, portMAX_DELAY);
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
      sdata.temperature = t;
      sdata.humidity    = h;
    xSemaphoreGive(dataMutex);
    
    vTaskDelay(pdMS_TO_TICKS(1500));
    }
  }
}

void taskReadFlame(void* parameters) {
  for (;;) {
    int f = analogRead(KYPIN);
    
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    // if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
      sdata.flame = f;
    xSemaphoreGive(dataMutex);
    
    vTaskDelay(pdMS_TO_TICKS(1000)); 
    // }
  }
}

void taskSendCAN(void* parameters) {
  SensorData senddata;
  for (;;) {
      // xSemaphoreTake(dataMutex, portMAX_DELAY);
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        senddata = sdata;  
        xSemaphoreGive(dataMutex);
      
        canMsg.can_id = 0x036;
        canMsg.can_dlc = 8;
        canMsg.data[0] = senddata.temperature;
        canMsg.data[1] = senddata.humidity;
        canMsg.data[2] = (senddata.flame >> 8) & 0xFF;
        canMsg.data[3] = senddata.flame & 0xFF;
        canMsg.data[4] = 0x00;
        canMsg.data[5] = 0x00;
        canMsg.data[6] = 0x00;
        canMsg.data[7] = 0x00;

        mcp2515->sendMessage(&canMsg);
        vTaskDelay(pdMS_TO_TICKS(1000)); 
      }
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000);

  SPI.begin();
  dht.begin();
  mcp2515 = new MCP2515(SPI_CS);
  mcp2515->reset();
  mcp2515->setBitrate(CAN_125KBPS, MCP_8MHZ);
  mcp2515->setNormalMode();

  // Tạo mutex
  dataMutex = xSemaphoreCreateMutex();

  // Tạo task
  xTaskCreate(taskReadFlame, "KY026", 128, NULL, 2, NULL);
  xTaskCreate(taskReadDHT, "DHT11", 128, NULL, 2, NULL);
  xTaskCreate(taskSendCAN, "CAN", 128, NULL, 2, NULL);

  // Bắt đầu FreeRTOS
  vTaskStartScheduler();
}

// Không làm gì ở loop, FreeRTOS chạy task
void loop() {}
