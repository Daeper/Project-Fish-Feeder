#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h> // เพิ่มไลบรารีสำหรับ Servo

// --- ตั้งค่า Wi-Fi ---
const char* ssid = "Nitipat.c";
const char* password = "25198301";

// --- ตั้งค่า MQTT (HiveMQ Cloud) ---
const char* mqtt_server = "42556a1a83004b9f8407776620ae63e7.s1.eu.hivemq.cloud";
const int mqtt_port = 8883; 
const char* mqtt_user = "admin";
const char* mqtt_pass = "12345678";

// --- Topics ---
const char* topicCmd = "feeder/nitipat/cmd";
const char* topicFoodLevel = "feeder/nitipat/food_level";

// --- ตั้งค่า Hardware ---
const int BUZZER_PIN = 3; 
const int SERVO_PIN = 2;  // ขาสัญญาณ (สีส้ม/เหลือง) ของ Servo

WiFiClientSecure espClient;
PubSubClient client(espClient);
Servo feederServo; // สร้างออบเจกต์ Servo

void playConnectMelody() {
  tone(BUZZER_PIN, 523, 100); delay(100);
  tone(BUZZER_PIN, 659, 100); delay(100);
  tone(BUZZER_PIN, 784, 100); delay(100);
  tone(BUZZER_PIN, 1047, 200); delay(200);
}

void playFeedMelody() {
  tone(BUZZER_PIN, 784, 150); delay(150);
  tone(BUZZER_PIN, 659, 250); delay(250);
}

// ฟังก์ชันทำให้ไฟ LED กระพริบตอบสนองสั้นๆ 2 ครั้ง
void blinkAck() {
  digitalWrite(LED_BUILTIN, LOW);  delay(100);
  digitalWrite(LED_BUILTIN, HIGH); delay(100);
  digitalWrite(LED_BUILTIN, LOW);  delay(100);
  digitalWrite(LED_BUILTIN, HIGH); 
}

void setup_wifi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); 
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("ได้รับคำสั่ง: ");
  Serial.println(message);
  
  // กระพริบไฟตอบรับทุกครั้งที่ได้รับข้อความใดๆ
  blinkAck();

  if (message == "FEED_NOW") {
    Serial.println(">> สั่งมอเตอร์หมุน ให้อาหารปลาทันที! <<");
    playFeedMelody();
    
    // สั่ง Servo หมุนเพื่อเทอาหาร
    feederServo.write(90); // หมุนเปิด (ปรับองศาได้ตามโครงสร้างกลไกของคุณ)
    delay(500);            // รอให้อาหารเทลงมาครึ่งวินาที
    feederServo.write(0);  // หมุนกลับไปปิด
  } 
  else if (message.startsWith("SYNC_TIME:")) {
    Serial.println(">> ซิงค์เวลาบอร์ดเป็น: " + message.substring(10) + " <<");
  }
  else if (message.startsWith("SET_SCHEDULE:")) {
    Serial.println(">> ได้รับตารางเวลา: " + message.substring(13) + " <<");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32C6-Feeder-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      
      digitalWrite(LED_BUILTIN, HIGH); 
      playConnectMelody(); 
      client.subscribe(topicCmd);
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      
      for(int i = 0; i < 10; i++) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        delay(500);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); 
  
  // ตั้งค่าเริ่มต้นของ Servo
  feederServo.attach(SERVO_PIN);
  feederServo.write(0); // ตำแหน่งปิดเริ่มต้น
  
  espClient.setInsecure(); 
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 
}