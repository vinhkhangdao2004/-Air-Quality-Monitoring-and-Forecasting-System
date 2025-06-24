#include <WiFi.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <DHT.h>

// Cấu hình thông tin mạng WiFi và InfluxDB
#define WIFI_SSID "Redmi Note 10S"
#define WIFI_PASSWORD "dvkhang2004"
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "rNKmS3Z7-_pqMbloCDVLeWJBaQq5AnflUidBT17ahV5kKePZbstxNxjLlr6kxegIdv0-HP6PSUG0N5QQ5_d0iA=="
#define INFLUXDB_ORG "f4ea8e890f77d114"
#define INFLUXDB_BUCKET "ESP32"
#define TZ_INFO "ICT-7"

// Cấu hình cảm biến DHT11
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Cấu hình cảm biến khí gas MQ135
#define MQ135PIN 34
#define RL_VALUE 10.0
#define VCC 5.0

// Cấu hình cảm biến bụi GP2Y
int measurePin = 32;
int ledPower = 16;
int samplingTime = 280;
int deltaTime = 40;
int sleepTime = 9680;

// Giá trị hiệu chuẩn cảm biến MQ135
float R0 = 2;

// Tạo client kết nối đến InfluxDB và điểm dữ liệu
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point sensorData("environment_data");

// Biến thời gian để điều khiển tần suất gửi và in dữ liệu
unsigned long lastSendTime = 0;
unsigned long lastPrintTime = 0;
const unsigned long sendInterval = 60000;
const unsigned long printInterval = 3000;

// Hàm tính điện trở Rs từ giá trị ADC cảm biến MQ2
float getResistance(int analogValue) {
  float voltage = analogValue * (VCC / 4095.0);
  float Rs = ((VCC / voltage) - 1) * RL_VALUE;
  return Rs;
}

// Hàm tính toán nồng độ khí CO ppm từ Rs
float calculatePPM(float Rs) {
  float ratio = Rs / R0;
  float ppm = pow(ratio, -1.518);
  return ppm;
}

// Hàm đọc dữ liệu từ cảm biến bụi
float readDust() {
  digitalWrite(ledPower, LOW);
  delayMicroseconds(samplingTime);
  float voMeasured = analogRead(measurePin);
  delayMicroseconds(deltaTime);
  digitalWrite(ledPower, HIGH);
  delayMicroseconds(sleepTime);
  float calcVoltage = voMeasured * (5.0 / 4095.0);
  return calcVoltage;
}

void setup() {
  // Thiết lập ban đầu: khởi tạo Serial, kết nối WiFi, cảm biến, đồng bộ thời gian
  Serial.begin(115200);
  pinMode(MQ135PIN, INPUT);
  pinMode(ledPower, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  dht.begin();
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Kiểm tra kết nối đến InfluxDB
  if (client.validateConnection()) {
    Serial.println("InfluxDB connected");
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  // Gắn thẻ định danh thiết bị và vị trí
  sensorData.addTag("device", "ESP32");
  sensorData.addTag("location", "SPKT");
}

void loop() {
  // Đọc dữ liệu từ các cảm biến
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();

  // Kiểm tra lỗi khi đọc DHT11
  if (isnan(temp) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    delay(1000);
    return;
  }

  int analogValue = analogRead(MQ135PIN);
  float Rs = getResistance(analogValue);
  float ppm = calculatePPM(Rs);
  float dust = readDust();

  unsigned long currentTime = millis();

  // Hiển thị dữ liệu lên Serial mỗi 3 giây
  if (currentTime - lastPrintTime >= printInterval) {
    Serial.print("Temp: "); Serial.print(temp);
    Serial.print(" °C, Humidity: "); Serial.print(humidity);
    Serial.print(" %, CO: "); Serial.print(ppm);
    Serial.print(" ppm, Dust: "); Serial.print(dust);
    Serial.println("");
    lastPrintTime = currentTime;
  }

  // Gửi dữ liệu lên InfluxDB mỗi 60 giây
  if (currentTime - lastSendTime >= sendInterval) {
    sensorData.clearFields();
    sensorData.addField("temperature", temp);
    sensorData.addField("humidity", humidity);
    sensorData.addField("co_ppm", ppm);
    sensorData.addField("dust_density", dust);

    Serial.println(client.pointToLineProtocol(sensorData));

    if (!client.writePoint(sensorData)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    } else {
      Serial.println("Data sent to InfluxDB.");
    }
    lastSendTime = currentTime;
  }

  delay(500);  // Nghỉ để giảm tải CPU
}
