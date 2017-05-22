 /*************************************************** 
    Copyright (C) 2016  Steffen Ochs, Phantomias2006

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    HISTORY: Please refer Github History
    
 ****************************************************/

#include <Wire.h>                 // I2C
#include <SPI.h>                  // SPI
#include <ESP8266WiFi.h>          // WIFI
#include <ESP8266WiFiMulti.h>     // WIFI
#include <WiFiClientSecure.h>     // HTTPS
#include <WiFiUdp.h>              // NTP
#include <TimeLib.h>              // TIME
#include <EEPROM.h>               // EEPROM
#include <FS.h>                   // FILESYSTEM
#include <ArduinoJson.h>          // JSON
#include <ESP8266mDNS.h>        
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>    // https://github.com/me-no-dev/ESPAsyncWebServer/issues/60
#include "AsyncJson.h"

extern "C" {
#include "user_interface.h"
#include "spi_flash.h"
#include "core_esp8266_si2c.c"
}

extern "C" uint32_t _SPIFFS_start;      // START ADRESS FS
extern "C" uint32_t _SPIFFS_end;        // FIRST ADRESS AFTER FS

// ++++++++++++++++++++++++++++++++++++++++++++++++++
// SETTINGS

// HARDWARE
#define FIRMWAREVERSION "V0.3"

// CHANNELS
#define CHANNELS 8                     // UPDATE AUF HARDWARE 4.05
#define INACTIVEVALUE  999             // NO NTC CONNECTED
#define SENSORTYPEN    8               // NUMBER OF SENSORS
#define LIMITUNTERGRENZE -20           // MINIMUM LIMIT
#define LIMITOBERGRENZE 999            // MAXIMUM LIMIT
#define MAX1161x_ADDRESS 0x33          // MAX11615
#define ULIMITMIN 10.0
#define ULIMITMAX 150.0
#define OLIMITMIN 35.0
#define OLIMITMAX 200.0
#define ULIMITMINF 50.0
#define ULIMITMAXF 302.0
#define OLIMITMINF 95.0
#define OLIMITMAXF 392.0

// BATTERY
#define BATTMIN 3600                  // MINIMUM BATTERY VOLTAGE in mV
#define BATTMAX 4185                  // MAXIMUM BATTERY VOLTAGE in mV 
#define ANALOGREADBATTPIN 0           // INTERNAL ADC PIN
#define BATTDIV 5.9F
#define CHARGEDETECTION 16              // LOAD DETECTION PIN

// OLED
#define OLED_ADRESS 0x3C              // OLED I2C ADRESS
#define MAXBATTERYBAR 13

// TIMER
#define INTERVALBATTERYMODE 1000
#define INTERVALSENSOR 1000
#define INTERVALCOMMUNICATION 30000
#define FLASHINWORK 500

// BUS
#define SDA 0
#define SCL 2
#define THERMOCOUPLE_CS 12          // 12

// BUTTONS
#define btn_r  4                    // Pullup vorhanden
#define btn_l  5                    // Pullup vorhanden
#define INPUTMODE INPUT_PULLUP      // INPUT oder INPUT_PULLUP
#define PRELLZEIT 5                 // Prellzeit in Millisekunden   
#define DOUBLECLICKTIME 400         // Längste Zeit für den zweiten Klick beim DOUBLECLICK
#define LONGCLICKTIME 600           // Mindestzeit für einen LONGCLICK
#define MINCOUNTER 0
#define MAXCOUNTER CHANNELS-1

// WIFI
#define APNAME "NANO-AP"
#define APPASSWORD "12345678"
#define HOSTNAME "NANO-"
#define NTP_PACKET_SIZE 48          // NTP time stamp is in the first 48 bytes of the message

// FILESYSTEM
#define CHANNELJSONVERSION 4        // FS VERSION
#define EEPROM_SIZE 1792            // EEPROM SIZE

// PITMASTER
#define PITMASTER1 15               // PITMASTER PIN
//#define PITMASTER2 14             // ab Platine V7.2
#define PITMIN 0                    // LOWER LIMIT SET
#define PITMAX 100                  // UPPER LIMIT SET
#define PITMASTERSIZE 5             // PITMASTER SETTINGS LIMIT

// ++++++++++++++++++++++++++++++++++++++++++++++++++++


// ++++++++++++++++++++++++++++++++++++++++++++++++++++
// GLOBAL VARIABLES

// CHANNELS
struct ChannelData {
   String name;             // CHANNEL NAME
   float temp;              // TEMPERATURE
   int   match;             // Anzeige im Temperatursymbol
   float max;               // MAXIMUM TEMPERATURE
   float min;               // MINIMUM TEMPERATURE
   byte  typ;               // TEMPERATURE SENSOR
   bool  alarm;             // SET CHANNEL ALARM
   bool  isalarm;           // Limits überschritten
   bool  show;              // Anzeigen am OLED       
   bool  showalarm;         // Alarm nicht weiter anzeigen
   String color;            // COLOR
};

ChannelData ch[CHANNELS];

String  ttypname[SENSORTYPEN] = {"Maverick",
                      "Fantast-Neu",
                      "100K6A1B",
                      "100K",
                      "SMD NTC",
                      "5K3A1B",
                      "MOUSER47K",
                      "Typ K"};


String  temp_unit = "C";
String colors[8] = {"#6495ED", "#CD2626", "#66CDAA", "#F4A460", "#D02090", "#FFEC8B", "#BA55D3", "#008B8B"};

// PITMASTER
struct Pitmaster {
   byte typ;           // PITMASTER NAME/TYP
   float set;            // SET-TEMPERATUR
   bool  active;           // IS PITMASTER ACTIVE
   byte  channel;         // PITMASTER CHANNEL
   float value;           // PITMASTER VALUE IN %
   int manuel;            // MANUEL PITMASTER VALUE IN %
   bool event;
   int16_t msec;          // PITMASTER VALUE IN MILLISEC
   unsigned long last;
};

Pitmaster pitmaster;
int pidsize;

// DATALOGGER
struct datalogger {
 uint16_t tem[8];
 long timestamp;
};

#define MAXLOGCOUNT 204             // SPI_FLASH_SEC_SIZE/ sizeof(datalogger)
datalogger mylog[MAXLOGCOUNT];
datalogger archivlog[MAXLOGCOUNT];
int log_count = 0;
uint32_t log_sector;                // erster Sector von APP2
uint32_t freeSpaceStart;            // First Sector of OTA
uint32_t freeSpaceEnd;              // Last Sector+1 of OTA

// SYSTEM
bool stby = false;                // USB POWER SUPPLY?
bool doAlarm = false;             // HARDWARE ALARM           
byte pulsalarm = 1;
String language;

struct Battery {
  int voltage;                    // CURRENT VOLTAGE
  bool charge;                    // CHARGE DETECTION
  int percentage;                 // BATTERY CHARGE STATE in %
  bool setreference;              // LOAD COMPLETE SAVE VOLTAGE
  int max;                        // MAX VOLTAGE
  int min;
};

Battery battery;
uint32_t vol_sum = 0;
int vol_count = 0;

// OLED
int current_ch = 0;               // CURRENTLY DISPLAYED CHANNEL       
bool LADENSHOW = false;           // LOADING INFORMATION?
bool INACTIVESHOW = true;         // SHOW INACTIVE CHANNELS
bool displayblocked = false;                     // No OLED Update
enum {NO, CONFIGRESET, CHANGEUNIT, OTAUPDATE, HARDWAREALARM};

struct MyQuestion {
   int typ;    
   int con;            
};

MyQuestion question;

// FILESYSTEM
enum {eCHANNEL, eWIFI, eTHING, ePIT, eSYSTEM, ePRESET};

// WIFI
byte isAP = 2;                    // WIFI MODE  (0 = STA, 1 = AP, 2 = NO)
String wifissid[5];
String wifipass[5];
int lenwifi = 0;
long rssi = 0;                   // Buffer rssi
String THINGSPEAK_KEY;
long scantime;
bool disconnectAP;
int timeZone;                     // Central European Time
String host;
struct HoldSSID {
   unsigned long connect;
   bool hold;             
   String ssid;
   String pass;
};
HoldSSID holdssid;

// NTP
byte packetBuffer[ NTP_PACKET_SIZE];    //buffer to hold incoming and outgoing packets

// BUTTONS
byte buttonPins[]={btn_r,btn_l};          // Pins
#define NUMBUTTONS sizeof(buttonPins)
byte buttonState[NUMBUTTONS];     // Aktueller Status des Buttons HIGH/LOW
enum {NONE, FIRSTDOWN, FIRSTUP, SHORTCLICK, DOUBLECLICK, LONGCLICK};
byte buttonResult[NUMBUTTONS];    // Aktueller Klickstatus der Buttons NONE/SHORTCLICK/LONGCLICK
unsigned long buttonDownTime[NUMBUTTONS]; // Zeitpunkt FIRSTDOWN
byte menu_count = 0;                      // Counter for Menu
byte inMenu = 0;
enum {TEMPSUB, PITSUB, SYSTEMSUB, MAINMENU, TEMPKONTEXT, BACK};
bool inWork = 0;
bool isback = 0;
int framepos[3] = {0, 6, 11};
int frameCount = 19;
bool flashinwork = true;
float tempor;                       // Zwischenspeichervariable


// ++++++++++++++++++++++++++++++++++++++++++++++++++++


// ++++++++++++++++++++++++++++++++++++++++++++++++++++
// PRE-DECLARATION

// INIT
void set_serial();                                // Initialize Serial
void set_button();                                // Initialize Buttons
static inline boolean button_input();             // Dectect Button Input
static inline void button_event();                // Response Button Status
void controlAlarm(bool action);                              // Control Hardware Alarm
void set_piepser();
void piepserOFF();
void piepserON();      

// SENSORS
byte set_sensor();                                // Initialize Sensors
int  get_adc_average (byte ch);                   // Reading ADC-Channel Average
double get_thermocouple(void);                    // Reading Temperature KTYPE
void get_Vbat();                                   // Reading Battery Voltage
void cal_soc();

// TEMPERATURE (TEMP)
float calcT(int r, byte typ);                     // Calculate Temperature from ADC-Bytes
void get_Temperature();                           // Reading Temperature ADC
void set_Channels();                              // Initialize Temperature Channels
void transform_limits();                          // Transform Channel Limits

// OLED
#include <SSD1306.h>              
#include <OLEDDisplayUi.h>        
SSD1306 display(OLED_ADRESS, SDA, SCL);
OLEDDisplayUi ui     ( &display );

// FRAMES
void drawConnect();                       // Frane while System Start
void drawLoading();                               // Frame while Loading
void drawQuestion(int counter);                    // Frame while Question
void drawMenu();
void set_OLED();                                  // Configuration OLEDDisplay

// FILESYSTEM (FS)
bool loadfile(const char* filename, File& configFile);
bool savefile(const char* filename, File& configFile);
bool checkjson(JsonVariant json, const char* filename);
bool loadconfig(byte count);
bool setconfig(byte count, const char* data[2]);
bool modifyconfig(byte count, const char* data[12]);
void start_fs();                                  // Initialize FileSystem
void read_serial(char *buffer);                   // React to Serial Input 
int readline(int readch, char *buffer, int len);  // Put together Serial Input

// MEDIAN
void median_add(int value);                       // add Value to Buffer
void median_sort();                               // sort Buffer
double median_get();                              // get Median from Buffer

// OTA
void set_ota();                                   // Configuration OTA

// WIFI
void set_wifi();                                  // Connect WiFi
void get_rssi();
void reconnect_wifi();
void stop_wifi();
void check_wifi();

// SERVER
void handleSettings(AsyncWebServerRequest *request, bool www);
void handleData(AsyncWebServerRequest *request, bool www);
void handleWifiResult(AsyncWebServerRequest *request, bool www);
void handleWifiScan(AsyncWebServerRequest *request, bool www);

// EEPROM
void setEE();
void writeEE(const char* json, int len, int startP);
void readEE(char *buffer, int len, int startP);
void clearEE(int startP, int endP);

// PITMASTER
void startautotunePID(float temp, int maxCycles, bool storeValues);

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initialize Serial
void set_serial() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initialize Buttons
void set_button() {
  
  for (int i=0;i<NUMBUTTONS;i++) pinMode(buttonPins[i],INPUTMODE);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Dedect Button Input
static inline boolean button_input() {
// Rückgabewert false ==> Prellzeit läuft, Taster wurden nicht abgefragt
// Rückgabewert true ==> Taster wurden abgefragt und Status gesetzt

  static unsigned long lastRunTime;
  //static unsigned long buttonDownTime[NUMBUTTONS];
  unsigned long now=millis();
  
  if (now-lastRunTime<PRELLZEIT) return false; // Prellzeit läuft noch
  
  lastRunTime=now;
  
  for (int i=0;i<NUMBUTTONS;i++)
  {
    byte curState=digitalRead(buttonPins[i]);
    if (INPUTMODE==INPUT_PULLUP) curState=!curState; // Vertauschte Logik bei INPUT_PULLUP
    if (buttonResult[i]>=SHORTCLICK) buttonResult[i]=NONE; // Letztes buttonResult löschen
    if (curState!=buttonState[i]) // Flankenwechsel am Button festgestellt
    {
      if (curState)   // Taster wird gedrückt, Zeit merken
      {
        if (buttonResult[i]==FIRSTUP && now-buttonDownTime[i]<DOUBLECLICKTIME)
          buttonResult[i]=DOUBLECLICK;
        else
        { 
          buttonDownTime[i]=now;
          buttonResult[i]=FIRSTDOWN;
        }
      }
      else  // Taster wird losgelassen
      {
        if (buttonResult[i]==FIRSTDOWN) buttonResult[i]=FIRSTUP;
        if (now-buttonDownTime[i]>=LONGCLICKTIME) buttonResult[i]=LONGCLICK;
      }
    }
    else // kein Flankenwechsel, Up/Down Status ist unverändert
    {
      if (buttonResult[i]==FIRSTUP && now-buttonDownTime[i]>DOUBLECLICKTIME)
        buttonResult[i]=SHORTCLICK;
    }
    buttonState[i]=curState;
  } // for
  return true;
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Response Button Status
static inline void button_event() {

  static unsigned long lastMupiTime;    // Zeitpunkt letztes schnelles Zeppen
  int mupi = 0;
  bool event[3] = {0, 0, 0};
  int b_counter;
  
  /*
  // Reset der Config erwuenscht
  if (buttonResult[1]==DOUBLECLICK && inMenu == TEMPSUB) {
      displayblocked = true;
      question = CONFIGRESET;
      drawQuestion();
      return;
  }

  // Anzeigemodus wechseln
  if (buttonResult[0]==DOUBLECLICK && inMenu == TEMPSUB) {
    INACTIVESHOW = !INACTIVESHOW;

    #ifdef DEBUG
      Serial.println("[INFO]\tAnzeigewechsel");
    #endif
    return;
  }
  */

  // Bearbeitungsmodus aktivieren/deaktivieren
  if (buttonResult[0]==DOUBLECLICK) {
    
    if (inWork) {
      inWork = false;                     // Bearbeitung verlassen
      flashinwork = true;                 // Dauerhafte Symbolanzeige
      event[0] = 1;
      event[1] = 0;
      event[2] = 1;                       // Endwert setzen
    } else {
      switch (inMenu) {
      
        case TEMPKONTEXT:                 // Temperaturwerte bearbeiten
          inWork = true;
          break;

        case PITSUB:                      // Pitmasterwerte bearbeiten
          inWork = true;
          break;

        case SYSTEMSUB:                   // Systemeinstellungen bearbeiten
          inWork = true;
          break;

      }
      event[0] = 1;
      event[1] = 1;         // Startwert setzen
      event[2] = 0;
      
    }
  }

  // Tiefer im Menü
  if (buttonResult[1]==DOUBLECLICK) {}
  


  // Bei LONGCLICK rechts großer Zahlensprung jedoch gebremst
  if (buttonResult[0] == FIRSTDOWN && (millis()-buttonDownTime[0]>400)) {
    
    if (inWork) {
      mupi = 10;
      if (millis()-lastMupiTime > 200) {
        event[0] = 1;
        lastMupiTime = millis();
      }
    } else {
      buttonResult[0] = NONE;     // Manuelles Zurücksetzen um Überspringen der Menus zu verhindern
      switch (inMenu) {

        case MAINMENU:                    // Menu aufrufen
          inMenu = menu_count;
          displayblocked = false;
          b_counter = framepos[menu_count];
          ui.switchToFrame(b_counter);
          return;
      
        case TEMPSUB:                     // Main aufrufen
          displayblocked = true;
          drawMenu();
          inMenu = MAINMENU;
          return;

        case PITSUB:                      // Main aufrufen
          if (isback) {
            displayblocked = true;
            drawMenu();
            inMenu = MAINMENU;
            //modifyconfig(ePIT,{});
            isback = 0;
          }
          return;

        case SYSTEMSUB:                   // Main aufrufen
          if (isback) {
            displayblocked = true;
            drawMenu();
            inMenu = MAINMENU;
            modifyconfig(eSYSTEM,{});
            isback = 0;
          }
          return;

        case TEMPKONTEXT:                 // Temperaturmenu aufrufen
          if (isback) {
            b_counter = 0;
            modifyconfig(eCHANNEL,{});      // Am Ende des Kontextmenu Config speichern
            inMenu = TEMPSUB;
            ui.switchToFrame(b_counter);
            isback = 0;
          }
          return;
          

      }
    }
  }


  // Bei LONGCLICK links großer negativer Zahlensprung jedoch gebremst
  if (buttonResult[1] == FIRSTDOWN && (millis()-buttonDownTime[1]>400)) {

    if (inWork) {
      mupi = -10;
      if (millis()-lastMupiTime > 200) {
        event[0] = 1;
        lastMupiTime = millis();
      }
    } else {
      
      buttonResult[1] = NONE;     // Manuelles Zurücksetzen um Überspringen der Menus zu verhindern
      switch (inMenu) {
        case TEMPSUB:                     // Temperaturkontextmenu aufgerufen
          inMenu = TEMPKONTEXT;
          ui.switchToFrame(framepos[0]+1);
          return;

        case MAINMENU:                    // Menu aufrufen
          b_counter = 0;
          displayblocked = false;
          inMenu = TEMPSUB;
          ui.switchToFrame(b_counter);
          return;

        case PITSUB:                      // Main aufrufen
          displayblocked = true;
          drawMenu();
          inMenu = MAINMENU;
          //modifyconfig(ePIT,{});
          return;

        case SYSTEMSUB:                   // Main aufrufen
          displayblocked = true;
          drawMenu();
          inMenu = MAINMENU;
          modifyconfig(eSYSTEM,{});
          return;

        case TEMPKONTEXT:                 // Temperaturmenu aufrufen
          b_counter = 0;
          modifyconfig(eCHANNEL,{});      // Am Ende des Kontextmenu Config speichern
          inMenu = TEMPSUB;
          ui.switchToFrame(b_counter);
          return;

      }
    }
  }


  // Button rechts Shortclick: Vorwärts / hochzählen / Frage mit Ja beantwortet
  if (buttonResult[0] == SHORTCLICK) {

    if (question.typ > 0) {
      // Frage wurde mit YES bestätigt
      switch (question.typ) {
        case CONFIGRESET:
          setconfig(eCHANNEL,{});
          loadconfig(eCHANNEL);
          set_Channels();
          break;

        case HARDWAREALARM:
          ch[question.con].showalarm = false;
          break;

      }
      question.typ = NO;
      displayblocked = false;
      return;
    }

    if (inWork) {

      // Bei SHORTCLICK kleiner Zahlensprung
      mupi = 1;
      event[0] = 1;
      
    } else {

      b_counter = ui.getCurrentFrameCount();
      int i = 0;
    
      switch (inMenu) {
      
        case MAINMENU:                     // Menu durchwandern
          if (menu_count < 2) menu_count++;
          else menu_count = 0;
          drawMenu();
          break;

        case TEMPSUB:                     // Temperaturen durchwandern
          if (INACTIVESHOW) {
            current_ch++;
            if (current_ch > MAXCOUNTER) current_ch = MINCOUNTER;
          }
          else {
            do {
              current_ch++;
              i++;
              if (current_ch > MAXCOUNTER) current_ch = MINCOUNTER;
            } while ((ch[current_ch].temp==INACTIVEVALUE) && (i<CHANNELS)); 
          }
          ui.setFrameAnimation(SLIDE_LEFT);
          ui.transitionToFrame(0);      // Refresh
          break;

        case TEMPKONTEXT:
          if (b_counter < framepos[1]-1) b_counter++;
          else b_counter = framepos[0]+1;
          if (b_counter == framepos[1]-1) isback = 1;
          else isback = 0;
          ui.switchToFrame(b_counter);
          break;

        case PITSUB:
          if (b_counter < framepos[2]-1) b_counter++;
          else  b_counter = framepos[1];
          if (b_counter == framepos[2]-1) isback = 1;
          else isback = 0;
          ui.switchToFrame(b_counter);
          break;

        case SYSTEMSUB:
          if (b_counter < frameCount-1) b_counter++;
          else b_counter = framepos[2];
          if (b_counter == frameCount-1) isback = 1;
          else isback = 0;
          ui.switchToFrame(b_counter);
          break;
      }
      return;
    }
  }
  
  
  
  // Button links gedrückt: Rückwärts / runterzählen / Frage mit Nein beantwortet
  if (buttonResult[1] == SHORTCLICK) {

    // Frage wurde verneint -> alles bleibt beim Alten
    if (question.typ > 0) {

      switch (question.typ) {

        case HARDWAREALARM:
          return;

      }
      
      question.typ = NO;
      displayblocked = false;
      return;
    }

    if (inWork) {

      mupi = -1;
      event[0] = 1;
    
    } else {
    
      b_counter = ui.getCurrentFrameCount();
      int j = CHANNELS;
    
      switch (inMenu) {

        case MAINMENU:                     
          if (menu_count > 0) menu_count--;
          else menu_count = 2;
          drawMenu();
          break;
        
        case TEMPSUB: 
          if (INACTIVESHOW) {
            current_ch--;
            if (current_ch < MINCOUNTER) current_ch = MAXCOUNTER;
          } else {
            do {
              current_ch--;
              j--;
              if (current_ch < MINCOUNTER) current_ch = MAXCOUNTER;
            } while ((ch[current_ch].temp==INACTIVEVALUE) && (j > 0)); 
          }
          ui.setFrameAnimation(SLIDE_RIGHT);
          ui.transitionToFrame(0);      // Refresh
          break;

        case TEMPKONTEXT:
          if (b_counter > framepos[0]+1) {
            b_counter--;
            isback = 0;
          }
          else {
            b_counter = framepos[1]-1;
            isback = 1;
          }
          ui.switchToFrame(b_counter);
          break;

        case PITSUB:
          if (b_counter > framepos[1]) {
            b_counter--;
            isback = 0;
          } else {
            b_counter = framepos[2]-1;
            isback = 1;
          }
          ui.switchToFrame(b_counter);
          break;

        case SYSTEMSUB:
          if (b_counter > framepos[2]) {
            b_counter--;
            isback = 0;
          } else {
            b_counter = frameCount-1;
            isback = 1;
          }
          ui.switchToFrame(b_counter);
          break;
      }
      return;
    }
  }

  
    
  if (event[0]) {  
    switch (ui.getCurrentFrameCount()) {
        
      case 1:  // Upper Limit
        if (event[1]) tempor = ch[current_ch].max;
        tempor += (0.1*mupi);
        if (temp_unit == "C") {
          if (tempor > OLIMITMAX) tempor = OLIMITMIN;
          else if (tempor < OLIMITMIN) tempor = OLIMITMAX;
        } else {
          if (tempor > OLIMITMAXF) tempor = OLIMITMINF;
          else if (tempor < OLIMITMINF) tempor = OLIMITMAXF;
        }
        if (event[2]) ch[current_ch].max = tempor;
        break;
          
      case 2:  // Lower Limit
        if (event[1]) tempor = ch[current_ch].min;
        tempor += (0.1*mupi);
        if (temp_unit == "C") {
          if (tempor > ULIMITMAX) tempor = ULIMITMIN;
          else if (tempor < ULIMITMIN) tempor = ULIMITMAX;
        } else {
          if (tempor > ULIMITMAXF) tempor = ULIMITMINF;
          else if (tempor < ULIMITMINF) tempor = ULIMITMAXF;
        }
        if (event[2]) ch[current_ch].min = tempor;
        break;
          
      case 3:  // Typ
        if (mupi == 10) mupi = 1;
        if (event[1]) tempor = ch[current_ch].typ;
        tempor += mupi;
        if (tempor > (SENSORTYPEN-2)) tempor = 0;
        else if (tempor < 0) tempor = SENSORTYPEN-2;
        if (event[2]) ch[current_ch].typ = tempor;
        break;
        
      case 4:  // Alarm
        if (event[1]) tempor = ch[current_ch].alarm;
        if (mupi) tempor = !tempor; 
        if (event[2]) ch[current_ch].alarm = tempor;
        break;
        
      case 6:  // Pitmaster Typ
        if (mupi == 10) mupi = 1;
        if (event[1]) tempor = pitmaster.typ; 
        tempor += mupi;
        if (tempor > pidsize-1) tempor = 0;
        else if (tempor < 0) tempor = pidsize-1;
        if (event[2]) pitmaster.typ = tempor;
        break;
        
      case 7:  // Pitmaster Channel
        if (mupi == 10) mupi = 1;
        if (event[1]) tempor = pitmaster.channel;
        tempor += mupi;
        if (tempor > CHANNELS-1) tempor = 0;
        else if (tempor < 0) tempor = CHANNELS-1;
        if (event[2]) pitmaster.channel = tempor;
        break;
        
      case 8:  // Pitmaster Set
        if (event[1]) tempor = pitmaster.set;
        tempor += (0.1*mupi);
        if (tempor > ch[pitmaster.channel].max) tempor = ch[pitmaster.channel].min;
        else if (tempor < ch[pitmaster.channel].min) tempor = ch[pitmaster.channel].max;
        if (event[2]) pitmaster.set = tempor;
        break;
        
      case 9:  // Pitmaster Active
        if (event[1]) tempor = pitmaster.active;
        if (mupi) tempor = !tempor;
        if (event[2]) pitmaster.active = tempor;
        break;
        
      case 14:  // Unit Change
        if (event[1]) {
          if (temp_unit == "F") tempor = 1;
        }
        if (mupi) tempor = !tempor;
        if (event[2]) {
          String unit;
          if (tempor) unit = "F";
          else unit = "C";
          if (unit != temp_unit) {
            temp_unit = unit;
            transform_limits();                             // Transform Limits
            modifyconfig(eCHANNEL,{});                      // Save Config
            get_Temperature();                              // Update Temperature
            #ifdef DEBUG
              Serial.println("[INFO]\tEinheitenwechsel");
            #endif
          }
        }
        break;
        
      case 15:  // Hardware Alarm
        if (event[1]) tempor = doAlarm;
        if (mupi) tempor = !tempor;
        if (event[2]) doAlarm = tempor;
        break;
      
      case 17:  // Fastmode
        if (event[1]) tempor = INACTIVESHOW;
        if (mupi) tempor = !tempor;
        if (event[2]) INACTIVESHOW = tempor;
        break;
     
    }
  }
  
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++
//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}






