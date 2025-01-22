#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_3C.h>
#include "hu16pt7b.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include "config.h"

#define PIN_SPI_SCK 14
#define PIN_SPI_DIN 13
#define CS_PIN 15
#define RST_PIN 2
#define DC_PIN 4
#define BUSY_PIN 5

// 定义墨水屏对象
#define GxEPD2_DISPLAY_CLASS GxEPD2_3C
#define GxEPD2_DRIVER_CLASS GxEPD2_420c
#define MAX_DISPLAY_BUFFER_SIZE (81920ul-34000ul-10000ul)
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8) ? EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8))
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display(GxEPD2_DRIVER_CLASS(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));

bool access_locked = false;
/* Put IP Address details in AP mode */
IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
ESP8266WebServer server(80);


WiFiClient client;

// NTP time update
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp1.aliyun.com", 8 * 3600, 60000);

bool image_updated = false;

// 绘制图片数据
void drawCompressedBitmap(WiFiClient &client, int width, int height) {
    int length = width * height / 4; // 2 bits per pixel, 1 byte for 4 pixels
    image_updated = true;
    unsigned long startTime = millis();
    const unsigned long timeout = 10000; // 10 seconds timeout

    for (int i = 0; i < length; i++) {
        while (!client.available()) {
            if (millis() - startTime > timeout) {
                Serial.println("Data transfer timed out.");
                image_updated = false;
                client.stop();
                return;
            }
            delay(10);
        }
        uint8_t data = client.read();
        for (int j = 0; j < 4; j++) {
            int color = (data >> (6 - 2 * j)) & 0x03;
            if (color == 0) {
                display.drawPixel((i * 4 + j)%width, (i*4+j)/width, GxEPD_WHITE);
            } else if (color == 1) {
                display.drawPixel((i * 4 + j)%width, (i*4+j)/width, GxEPD_BLACK);
            } else if (color == 2) {
                display.drawPixel((i * 4 + j)%width, (i*4+j)/width, GxEPD_RED);
            } else {
                display.drawPixel((i * 4 + j)%width, (i*4+j)/width, GxEPD_WHITE);
            }
        }
    }
    client.stop();
}

void fetch_data() {
    if (!client.connect(host, port)) {
        Serial.println("Connection failed.");
        return;
    }
    Serial.println("Connected to server.");

   
    drawCompressedBitmap(client, 400, 300);
    Serial.println("Data transfer completed successfully.");
}


bool connect_wifi(const char *ssid, const char *passwd) {
    // init network
    Serial.printf("Connecting to %s\n", ssid);
    WiFi.disconnect();
    WiFi.begin(ssid, passwd);
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
        if (--timeout == 0) {
            Serial.printf("Failed to connect to WiFi %s\n", ssid);
            return false;
        }
    }
    Serial.println("Connected to WiFi");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
}

void start_server() {
    server.serveStatic("/", LittleFS, "/index.html");
    server.on("/wifi", HTTP_POST, [](){
        if (access_locked) {
            server.send(403, "text/plain", "Please wait for a while and retry.");
            return;
        }
        access_locked = true;
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        Serial.printf("ssid: %s, password: %s\n", ssid.c_str(), password.c_str());
        File f = LittleFS.open("/wifi.json", "w");
        if (!f) {
            Serial.println("Failed to open wifi.json");
            server.send(500, "text/plain", "Failed to open wifi.json");
            access_locked = false;
            return;
        }
        JsonDocument doc;
        doc["ssid"] = ssid;
        doc["password"] = password;
        serializeJson(doc, f);
        f.close();
        server.send(200, "text/plain", "WiFi configuration saved. Please restart the device.");
        access_locked = false;
    });
    server.begin();
}

void setup() {
    Serial.begin(115200); // 初始化串口
    Serial.println("Initializing...");

    pinMode(PIN_SPI_SCK, OUTPUT);
    pinMode(PIN_SPI_DIN, OUTPUT);
    pinMode(CS_PIN , OUTPUT);
    pinMode(RST_PIN , OUTPUT);
    pinMode(DC_PIN , OUTPUT);
    pinMode(BUSY_PIN, INPUT);

    if (!LittleFS.begin()) {
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }

    start_server();

    bool connected = false;
    WiFi.mode(WIFI_STA);
    if (!connect_wifi(ssid, password)) {
        delay(1000);
        File f = LittleFS.open("/wifi.json", "r");
        if (!f) {
            Serial.println("Failed to open wifi.json");
        } else {
            String json = f.readString();
            Serial.println(json);
            JsonDocument doc;
            deserializeJson(doc, json);
            const char *ssid = doc["ssid"];
            const char *passwd = doc["password"];
            connected = connect_wifi(ssid, passwd);
            if (!connected) {
                Serial.println("Failed to connect to WiFi from wifi.json");
            }
        }
        f.close();

        if (!connected) {
            Serial.println("Try to set up WiFi from AP mode");
            WiFi.mode(WIFI_AP);
            WiFi.softAP("ESP8266", "12345678");
            WiFi.softAPConfig(local_ip, gateway, subnet);
            delay(100);
            return;
        }
    }

    timeClient.begin();
    timeClient.update();

    // 初始化墨水屏
    display.init();
    display.setFont(&hu16pt7b);// 设置字体

    // 清空屏幕并显示内容
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE); // 清空屏幕
        fetch_data();
        Serial.printf("image_updated flag: %d\n", image_updated);
        if (!image_updated) {
            display.fillScreen(GxEPD_WHITE); // 清空屏幕
            display.setCursor(160, 120);
            display.print("No signal!");
        }
    } while (display.nextPage()); // 结束当前页并检查是否需要渲染下一页

    Serial.println("Display updated.");
}

void loop() {
    // 每小时更新一次数据
    server.handleClient();
    delay(1000);
    timeClient.update();
    if (timeClient.getMinutes() == 0) {
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE); // 清空屏幕
            fetch_data();
            Serial.printf("image_updated flag: %d\n", image_updated);
            if (!image_updated) {
                display.fillScreen(GxEPD_WHITE); // 清空屏幕
                display.setCursor(160, 120);
                display.print("No signal!");
            }
        } while (display.nextPage()); // 结束当前页并检查是否需要渲染下一页
        delay(1000*60); // 延时 1 分钟
    }
}