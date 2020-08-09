// display headers:
#include"TFT_eSPI.h"
#include"Free_Fonts.h" //include the header file
// Sensor comm header:
#include <Seeed_HM330X.h>
// Storage headers:
#include <Seeed_FS.h>

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

const char* logFilePath = "data.csv"; //log path on file system
File logFile;

#ifdef  ARDUINO_SAMD_VARIANT_COMPLIANCE
    #define SERIAL_OUTPUT SerialUSB
#else
    #define SERIAL_OUTPUT Serial
#endif

HM330X sensor;
uint8_t buf[30];

unsigned long start_time = 0;

const char* str[] = {"sensor num: ", "PM1.0",
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

/*parse buf with 29 uint8_t-data*/
HM330XErrorCode parse_result(uint8_t* data) {
    uint16_t value = 0;
    if (NULL == data) {
        return ERROR_PARAM;
    }
    tft.fillScreen(TFT_BLACK); //Black background   
    
   
    char buf[100];
    int start  = 0;
    // display available size
    uint32_t usedBytes = DEV.usedBytes();
    sprintf(buf, "Used space: %d MB", usedBytes / (1024 * 1024));
    tft.drawString(buf,0,start);
    start += 30;
    int written = sprintf(buf, "%ld", millis());
    logFile.write(buf, written);
    for (int i = 2; i < 5; i++) {
        value = (uint16_t) data[i * 2] << 8 | data[i * 2 + 1];
        sprintf(buf, "%s: %u ug/m3", str[i - 1], value);
        tft.drawString(buf,0,start);
        written = sprintf(buf, ",%u", value);
        logFile.write(buf, written);
        start += 30;
    }
    logFile.write("\n");
    logFile.flush();
    return NO_ERROR;
}

HM330XErrorCode parse_result_value(uint8_t* data) {
    if (NULL == data) {
        return ERROR_PARAM;
    }
    for (int i = 0; i < 28; i++) {
        SERIAL_OUTPUT.print(data[i], HEX);
        SERIAL_OUTPUT.print("  ");
        if ((0 == (i) % 5) || (0 == i)) {
            SERIAL_OUTPUT.println("");
        }
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

void writeHeader() {
    logFile.write("time");
    for (int i = 2; i < 5; i++) {
      logFile.write(",");
      logFile.write(str[i - 1]);
    }
    logFile.write("\n");
    logFile.flush();
}

/*30s*/
void setup() {
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
      logFile = SD.open("data.csv", FILE_WRITE); //Writing Mode
      writeHeader();
    } else {
      logFile = SD.open("data.csv", FILE_APPEND); //Writing Mode
    }



    SERIAL_OUTPUT.begin(115200);
    delay(100);
    SERIAL_OUTPUT.println("Serial start");
    if (sensor.init()) {
        SERIAL_OUTPUT.println("HM330X init failed!!!");
        while (1);
    }
    start_time = millis();
}


void loop() {
    if (sensor.read_sensor_value(buf, 29)) {
        SERIAL_OUTPUT.println("HM330X read result failed!!!");
    } else {
      if(millis() - start_time > 30000) {
        parse_result_value(buf);
        parse_result(buf);
        SERIAL_OUTPUT.println("");
        delay(5000);  
      } else {
          char buf[100];
          sprintf(buf, "Warmup %d ..", 30 - (millis() - start_time) / 1000);
          tft.drawString(buf,0,80);
          delay(1000);      
      }
    }
}