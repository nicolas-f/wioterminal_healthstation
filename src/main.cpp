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

#ifdef  ARDUINO_SAMD_VARIANT_COMPLIANCE
    #define SERIAL_OUTPUT SerialUSB
#else
    #define SERIAL_OUTPUT Serial
#endif

HM330X sensor;
uint8_t buf[30];

unsigned long start_time = 0;
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
    for (int i = 2; i < 5; i++) {
        value = (uint16_t) data[i * 2] << 8 | data[i * 2 + 1];
        sprintf(buf, "%s: %u ug/m3", str[i - 1], value);
        tft.drawString(buf,0,start);
        written += sprintf(filebuf + written, ",%u", value);
        start += 30;
    }
    written += sprintf(filebuf + written, "\n");
    appendFile(DEV, logFilePath, filebuf);
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
      logFile.write(str[i - 1]);
    }
    logFile.write("\n");
    logFile.flush();
}

/*30s*/
void setup() {
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