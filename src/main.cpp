// display headers:
#include"TFT_eSPI.h"
#include"Free_Fonts.h" //include the header file
// Sensor comm header:
#include <Seeed_HM330X.h>
#include "UNIT_ENV.h"
#include "Adafruit_Sensor.h"
#include <Adafruit_BMP280.h>
// Storage headers:
#include <Seeed_FS.h>
#include <WiFiClientSecure.h>
#include "wifi_credentials.h"
#include <PubSubClient.h>



// Use SD card instead of internal SPI flash mem (2mb only)
#undef USESPIFLASH

#ifdef USESPIFLASH
#define DEV SPIFLASH
#include "SFUD/Seeed_SFUD.h"
#else
#define DEV SD
#include "SD/Seeed_SD.h"
#endif

TFT_eSPI tft;

// Use WiFiClient class to create TCP connections
WiFiClientSecure wifiClient;
PubSubClient client(wifiClient);

const char* logFilePath = "data.csv"; //log path on file system

#ifdef  ARDUINO_SAMD_VARIANT_COMPLIANCE
    #define SERIAL_OUTPUT SerialUSB
#else
    #define SERIAL_OUTPUT Serial
#endif

HM330X sensor;
SHT3X sht30;
uint8_t buf[30];

char temperature[10];
char humidity[10];
char pressure[10];
Adafruit_BMP280 bme;

unsigned long start_time = 0;
unsigned long last_mqtt_send = 0;
unsigned long mqtt_interval_seconds = 60;
unsigned long last_sensor_reading = 0;
unsigned long last_display = 0;
unsigned long buttonIndex[] = {WIO_KEY_A, WIO_KEY_B, WIO_KEY_C};
int lastButtonState[] = {HIGH, HIGH, HIGH};
int screenState = HIGH;

boolean writeFile(fs::FS& fs, const char* path, const char* message) {
    boolean success = true;
    SERIAL_OUTPUT.print("Writing file: ");
    SERIAL_OUTPUT.println(path);
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        SERIAL_OUTPUT.println("Failed to open file for writing");
        return false;
    }
    
    if (file.print(message)) {
        SERIAL_OUTPUT.println("File written");
    } else {
        SERIAL_OUTPUT.println("Write failed");
        success = false;
    }

    file.close();
    return success;
}

boolean appendFile(fs::FS& fs, const char* path, const char* message) {
    boolean success = true;
    char buf[100];
    sprintf(buf, "Appending \"%s\" to file: %s", message, path);
    SERIAL_OUTPUT.println(buf);

    File file = fs.open(path, FILE_APPEND);
    if (!file) {
        SERIAL_OUTPUT.println("Failed to open file for appending");
        return false;
    }
    if (!file.print(message)) {
        SERIAL_OUTPUT.println("Append failed");
        success = false;
    }
    file.close();
    return success;
}

const char* titles[] = {"sensor num: ", "PM1.0",
                     "PM2.5",
                     "PM10"
                    };

HM330XErrorCode print_result(const char* str, uint16_t value) {
    if (NULL == str) {
        return ERROR_PARAM;
    }
    SERIAL_OUTPUT.print(str);
    SERIAL_OUTPUT.println(value);
    return NO_ERROR;
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("stickc", IO_USERNAME, IO_KEY))
    {
      Serial.println("connected");
      delay(1000);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


/*parse buf with 29 uint8_t-data*/
HM330XErrorCode parse_result(uint8_t* data) {
    uint16_t value = 0;
    if (NULL == data) {
        return ERROR_PARAM;
    }
    tft.fillScreen(TFT_BLACK); //Black background   
    
   
    char buf[100];
    char filebuf[100];
    memset(filebuf, 0, sizeof(filebuf));
    int written = 0;
    int start  = 0;
    // display available size
    uint32_t usedBytes = DEV.usedBytes();
    sprintf(buf, "Used space: %ld KB", usedBytes / 1024);
    tft.drawString(buf,0,start);
    start += 30;
    written += sprintf(filebuf + written, "%ld", last_sensor_reading);
    char pm1[20], pm2[20], pm10[20];
    for (int i = 2; i < 5; i++) {
        value = (uint16_t) data[i * 2] << 8 | data[i * 2 + 1];
        if(i==2) {
            sprintf(pm1, "%d", value);
        } else if(i==3) {
            sprintf(pm2, "%d", value);
        } else {
            sprintf(pm10, "%d", value);
        }
        sprintf(buf, "%s: %u ug/m3", titles[i - 1], value);
        tft.drawString(buf,0,start);
        written += sprintf(filebuf + written, ",%u", value);
        start += 30;
    }
    written += sprintf(filebuf + written, "\n");
    // temperature, pressure, humidity
    sprintf(pressure, "%.2f", bme.readPressure());
    sht30.get();  //Obtain the data of shT30.
    sprintf(temperature, "%.2f", sht30.cTemp); //Store the temperature obtained from shT30.
    sprintf(humidity, "%.1f", sht30.humidity); //Store the humidity obtained from the SHT30.
    appendFile(DEV, logFilePath, filebuf);    
    // m5mqtt.publish(str('QRTone/feeds/temperature'),str(("%.1f"%(temp))))
    // m5mqtt.publish(str('QRTone/feeds/pressure'),str(("%.1f"%(baro))))
    // m5mqtt.publish(str('QRTone/feeds/humidity'),str(int(hum)))
    
    if(millis() - last_mqtt_send > mqtt_interval_seconds * 1000L) {
        last_mqtt_send = millis();
        if (!client.connected())
        {
           reconnect();
        }
        client.publish("QRTone/feeds/pm1",pm1);
        client.publish("QRTone/feeds/pm2",pm2);
        client.publish("QRTone/feeds/pm10",pm10);
        client.publish("QRTone/feeds/temperature",temperature);
        client.publish("QRTone/feeds/pressure",pressure);
        client.publish("QRTone/feeds/humidity",humidity);
        SERIAL_OUTPUT.println("MQTT sent");
    }
    return NO_ERROR;
}

HM330XErrorCode parse_result_value(uint8_t* data) {
    if (NULL == data) {
        return ERROR_PARAM;
    }
    uint8_t sum = 0;
    for (int i = 0; i < 28; i++) {
        sum += data[i];
    }
    if (sum != data[28]) {
        SERIAL_OUTPUT.println("wrong checkSum!!!!");
    }
    SERIAL_OUTPUT.println("");
    return NO_ERROR;
}

void writeHeader(File& logFile) {
    logFile.write("time");
    for (int i = 2; i < 5; i++) {
      logFile.write(",");
      logFile.write(titles[i - 1]);
    }
    logFile.write("\n");
    logFile.flush();
}

/*30s*/
void setup() {
    SERIAL_OUTPUT.begin(115200);

    delay(100);

    // Init buttons
    pinMode(WIO_KEY_A, INPUT_PULLUP);
    pinMode(WIO_KEY_B, INPUT_PULLUP);
    pinMode(WIO_KEY_C, INPUT_PULLUP);
    // Init screen
    tft.begin();  
    tft.setRotation(3);      
    tft.setFreeFont(FF10); //select Free, Mono, Oblique, 12pt.  
    tft.fillScreen(TFT_BLACK); //Black background   
    // init storage
    SERIAL_OUTPUT.print("Initializing SD card...");

    #ifdef SFUD_USING_QSPI
        if (!DEV.begin(104000000UL)) {
            SERIAL_OUTPUT.println("Card Mount Failed");
            while (1);
        }
    #else
        if (!DEV.begin(SDCARD_SS_PIN,SDCARD_SPI,4000000UL)) {
            SERIAL_OUTPUT.println("Card Mount Failed");
            while (1);
        }
    #endif 

    Serial.println("initialization done.");
    // check if file exists
    if(!DEV.exists(logFilePath)) {
      File logFile = DEV.open("data.csv", FILE_WRITE); //Writing Mode
      if (!logFile) {
          SERIAL_OUTPUT.println("Failed to open file for writing");
          while (1);
      }
      writeHeader(logFile);    
    }


    WiFi.mode(WIFI_STA); //set WiFi to station mode 
    WiFi.disconnect(); //disconnect from an AP if it was previously connected
        
    tft.fillScreen(TFT_BLACK);
    tft.setCursor((320 - tft.textWidth("Connecting to WiFi..")) / 2, 120);
    tft.print("Connecting to WiFi..");

    WiFi.begin(ssid, password); //connect to Wi-Fi network
    
    // attempt to connect to Wi-Fi network:
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        // wait 1 second for re-trying
        delay(1000);
    }
        
    Serial.print("Connected to ");
    Serial.println(ssid); //print Wi-Fi name 
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP()); //print Wio Terminal's IP address
    Serial.println(""); //print empty line

    tft.fillScreen(TFT_BLACK);
    tft.setCursor((320 - tft.textWidth("Connected!")) / 2, 120);
    tft.print("Connected!");

    wifiClient.setCACert(test_root_ca);

    client.setServer(IO_SERVER, 8883);

    delay(500);

    if (sensor.init()) {
        SERIAL_OUTPUT.println("HM330X init failed!!!");
        while (1);
    }
    start_time = millis();
}


void loop() {
    // Use press button on the left
    if(lastButtonState[2] == HIGH && digitalRead(buttonIndex[2]) == LOW) {
        if(screenState == HIGH) {
            // Turning off the LCD backlight
            digitalWrite(LCD_BACKLIGHT, LOW);
            screenState = LOW;
        } else {
            // Turning on the LCD backlight
            digitalWrite(LCD_BACKLIGHT, HIGH);
            screenState = HIGH;
        }
    }
    for(int i=0; i < 3; i++) {
        lastButtonState[i] = digitalRead(buttonIndex[i]);
    }
    if(millis() - start_time < 30000) {
        if(millis() - last_display > 1000) {
            char buf[100];
            sprintf(buf, "Warmup %ld ..   ", 30 - (millis() - start_time) / 1000);
            tft.drawString(buf,0,80);
            last_display = millis();
        }
    } else if(millis() - last_sensor_reading > 5000) {
        if (sensor.read_sensor_value(buf, 29)) {
            SERIAL_OUTPUT.println("HM330X read result failed!!!");
        } else {
            last_sensor_reading = millis();
            parse_result_value(buf);
            parse_result(buf);
        }
    }
    delay(125);
}