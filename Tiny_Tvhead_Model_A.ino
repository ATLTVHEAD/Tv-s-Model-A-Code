#include "FS.h"
#include <Adafruit_HX8357.h>
#include <Adafruit_GFX.h>
#include <Bounce2.h>
#include <WiFi.h>
#include <IRCClient.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <SD.h>
#include <SPI.h>
#include "WORDS.h"
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <math.h>

// for time zone calculations and setting up the NTP
int8_t timeZone = -5;
int8_t minutesTimeZone = 0;
bool wifiFirstConnected = true;

boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpEvent; // Last triggered event


//For setting up the tft and sd card 
#define TFT_DC 16
#define TFT_CS 17
// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, -1);
#define BUFFPIXEL 75
#define SD_CS 5

// bool for the word of the day
boolean firstOn = false;

//clock variables and bool for triggering the clock in the main loop - replace with encoder
int i = 0;
unsigned int strLength = 0;
//int yesterday = 0;
//int dailyCounter = 0;
char buf[12];
boolean clockOn = true;

// used for random x,y, values to give the words and time a little motion 
int rx = 0;
int ry = 0;

//setup for the vintage circles 
boolean vcOn = true;
uint16_t v=0;
uint16_t xtrans=240;
uint16_t ytrans=160;
int eq = 2; // -> get's set when encoder triggers



void setup() {

  pinMode(19,INPUT_PULLUP);
  Serial.begin(115200);

  tft.begin(HX8357D);
  tft.fillScreen(HX8357_BLACK);


  tft.setRotation(1);
  tft.setCursor(90, 100);
  tft.setTextColor(HX8357_WHITE);  tft.setTextSize(7);
  tft.print("M");
  delay(100);
  tft.print("O");
  delay(100);
  tft.print("D");
  delay(100);
  tft.print("E");
  delay(100);
  tft.print("L ");
  delay(200);
  tft.print("A");
  tft.setRotation(0);
  delay(750);
  tft.setRotation(1);
  tft.setCursor(260, 260);
  tft.setTextColor(HX8357_WHITE);  tft.setTextSize(3);
  tft.println("LOADING");
  tft.setTextSize(2);
  tft.setCursor(180, 180);
  tft.println("SERIAL A01");



  Serial.print("Initializing SD card...");
  while(!SD.begin(SD_CS)) {
    Serial.print("failed! ");
    delay(50);
    Serial.println(i);
    if(i>10){
      break;
    }
    i++;
  }
  

  WiFiManager wifiManager;
  wifiManager.setTimeout(180);
  if(!wifiManager.autoConnect("AutoConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  } 


  tft.setRotation(0); 
  tft.fillScreen(HX8357_YELLOW);


  NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
      ntpEvent = event;
      syncEventTriggered = true;
  });

  
  loading();
}

void loop() {
    static int i = 0;
    static int last = 0;

    if (wifiFirstConnected) {
        wifiFirstConnected = false;
        NTP.begin ("pool.ntp.org", timeZone, true, minutesTimeZone);
        NTP.setInterval (3600);
    }

    if (syncEventTriggered) {
        processSyncEvent (ntpEvent);
        syncEventTriggered = false;
    }

    if(firstOn){
      firstOn = false;
      daily();
      delay(5000);
      clockOn = true;
      vcOn = true;
      }
    else{
      //Clock();
      vintageCircles();
      
    }
      

  yield();
}






void bmpDraw(char *filename, uint8_t x, uint16_t y) {

  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  goodBmp = false;       // Set to true on valid header parse
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h, row, col;
  uint8_t  r, g, b;
  uint32_t pos = 0, startTime = millis();

  if((x >= tft.width()) || (y >= tft.height())) return;

  Serial.println();
  Serial.print(F("Loading image '"));
  Serial.print(filename);
  Serial.println('\'');

  // Open requested file on SD card
  if ((bmpFile = SD.open(filename)) == NULL) {
    Serial.print(F("File not found"));
    return;
  }

  // Parse BMP header
  if(read16(bmpFile) == 0x4D42) { // BMP signature
    Serial.print(F("File size: ")); Serial.println(read32(bmpFile));
    (void)read32(bmpFile); // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile); // Start of image data
    Serial.print(F("Image Offset: ")); Serial.println(bmpImageoffset, DEC);
    // Read DIB header
    Serial.print(F("Header size: ")); Serial.println(read32(bmpFile));
    bmpWidth  = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if(read16(bmpFile) == 1) { // # planes -- must be '1'
      bmpDepth = read16(bmpFile); // bits per pixel
      Serial.print(F("Bit Depth: ")); Serial.println(bmpDepth);
      if((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed

        goodBmp = true; // Supported BMP format -- proceed!
        Serial.print(F("Image size: "));
        Serial.print(bmpWidth);
        Serial.print('x');
        Serial.println(bmpHeight);

        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;

        // If bmpHeight is negative, image is in top-down order.
        // This is not canon but has been observed in the wild.
        if(bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip      = false;
        }

        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if((x+w-1) >= tft.width())  w = tft.width()  - x;
        if((y+h-1) >= tft.height()) h = tft.height() - y;

        // Set TFT address window to clipped image bounds
        tft.setAddrWindow(x, y, x+w-1, y+h-1);

        for (row=0; row<h; row++) { // For each scanline...

          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if(flip) // Bitmap is stored bottom-to-top order (normal BMP)
            pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
          else     // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          if(bmpFile.position() != pos) { // Need seek?
            bmpFile.seek(pos);
            buffidx = sizeof(sdbuffer); // Force buffer reload
          }

          for (col=0; col<w; col++) { // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) { // Indeed
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0; // Set index to beginning
            }

            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            tft.pushColor(tft.color565(r,g,b));
          } // end pixel
        } // end scanline
        Serial.print(F("Loaded in "));
        Serial.print(millis() - startTime);
        Serial.println(" ms");
      } // end goodBmp
    }
  }

  bmpFile.close();
  if(!goodBmp) Serial.println(F("BMP format not recognized."));
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}



void loading(){
  for(int i=0;i<1;i++){
    bmpDraw("/TTV.bmp",0,0);
    bmpDraw("/TTV2.bmp",0,0);
    bmpDraw("/TTV3.bmp",0,0);
  }
  tft.fillScreen(HX8357_BLACK);
}

void daily(){
  //Serial.print("Day ");
  //Serial.print(dailyCounter);
  //Serial.println(" 72 Hours Remain");
  tft.fillScreen(0xFD11);
  tft.setRotation(1);
  rx = random(1,6)*10;
  ry = random(1,6)*10;
  tft.setCursor(90+rx, 100+ry);
  tft.setTextColor(HX8357_WHITE);  tft.setTextSize(4);
  tft.println(words[dayOfTheYear()]);
  tft.setRotation(0);  
}



int dayOfTheYear(){
  int dayOfYear = 0;
  String date = NTP.getDateStr(NTP.getLastNTPSync());
  date.toCharArray(buf, 12); // 0,1 3,4

  if(buf[0]-48 == 1){
    dayOfYear = dayOfYear + 10;
  }
  else if(buf[0]-48 == 2){
    dayOfYear = dayOfYear + 20;
  }
  else if(buf[0]-48 == 3){
    dayOfYear = dayOfYear + 30;
  }

  if(buf[1]-48 == 0){
    yield();
  }
  else if( buf[1]-48 == 1){
    dayOfYear = dayOfYear + 1;
  }
  else if( buf[1]-48 == 2){
    dayOfYear = dayOfYear + 2;
  }
  else if( buf[1]-48 == 3){
    dayOfYear = dayOfYear + 3;
  }
  else if( buf[1]-48 == 4){
    dayOfYear = dayOfYear + 4;
  }
  else if( buf[1]-48 == 5){
    dayOfYear = dayOfYear + 5;
  }
  else if( buf[1]-48 == 6){
    dayOfYear = dayOfYear + 6;
  }
  else if( buf[1]-48 == 7){
    dayOfYear = dayOfYear + 7;
  }
  else if( buf[1]-48 == 8){
    dayOfYear = dayOfYear + 8;
  }
  else if( buf[1]-48 == 9){
    dayOfYear = dayOfYear + 9;
  }

  if(buf[3]-48 == 0){
    if(buf[4]-48 == 0){
      yield();      
    }
    else if(buf[4]-48 == 1){
      yield();
    }
    else if(buf[4]-48 == 2){
      dayOfYear = dayOfYear +31;
    }
    else if(buf[4]-48 == 3){
      dayOfYear = dayOfYear +59;
    }
    else if(buf[4]-48 == 4){
      dayOfYear = dayOfYear +90;
    }
    else if(buf[4]-48 == 5){
      dayOfYear = dayOfYear +120;
    }
    else if(buf[4]-48 == 6){
      dayOfYear = dayOfYear +151;
    }
    else if(buf[4]-48 == 7){
      dayOfYear = dayOfYear +181;
    }
    else if(buf[4]-48 == 8){
      dayOfYear = dayOfYear +212;
    }
    else if(buf[4]-48 == 9){
      dayOfYear = dayOfYear +243;
    }
  }
  else if(buf[3]-48==1){
    if(buf[4]-48 == 0){
      dayOfYear = dayOfYear + 273;      
    }
    else if(buf[4]-48 == 1){
      dayOfYear = dayOfYear +304;
    }
    else if(buf[4]-48 == 2){
      dayOfYear = dayOfYear +334;
    }
  }

  
  if(dayOfYear == 0){
    dayOfYear++;
  }
  
  if(dayOfYear > 365){
    dayOfYear = 365;
  }

  return dayOfYear; 
}



void Clock(){
  String TIME = NTP.getTimeStr();
  TIME.remove(5);
    static int i = 0;
    static int last = 0;
  rx = random(1,6)*10;
  ry = random(1,6)*10;
  
  if(clockOn){
    tft.setRotation(1);
    tft.setCursor(80+rx, 110+ry);
    tft.setTextColor(HX8357_WHITE);  tft.setTextSize(8);
    tft.fillScreen(0x8F3F);
    tft.println(TIME);
    clockOn = false;
  }
  
  if ((millis () - last) > 10100) {
    tft.setRotation(1);
    tft.setCursor(80+rx, 110+ry);
    tft.setTextColor(HX8357_WHITE);  tft.setTextSize(8);
    tft.fillScreen(0x8F3F);
  
        //Serial.println(millis() - last);
        last = millis ();
        //Serial.print (i); Serial.print (" ");
        tft.println (TIME);
        // Serial.print (" ");
        //Serial.print (NTP.isSummerTime () ? "Summer Time. " : "Winter Time. ");
        //Serial.print ("WiFi is ");
        //Serial.print (WiFi.isConnected () ? "connected" : "not connected"); Serial.print (". ");
        //Serial.print ("Uptime: ");
        //Serial.print (NTP.getUptimeString()); Serial.print (" since ");
        //Serial.println (NTP.getTimeDateString (NTP.getFirstSync ()).c_str ());

        i++;
      if(i >= 12){
        firstOn = true;
        i = 0;
      }
    }
  tft.setRotation(0);    
}




void processSyncEvent (NTPSyncEvent_t ntpEvent) {
    if (ntpEvent) {
        Serial.print ("Time Sync error: ");
        if (ntpEvent == noResponse)
            Serial.println ("NTP server not reachable");
        else if (ntpEvent == invalidAddress)
            Serial.println ("Invalid NTP server address");
    } else {
        Serial.print ("Got NTP time: ");
        Serial.println (NTP.getTimeDateString (NTP.getLastNTPSync ()));
        firstOn = true;
    }
}



void vintageCircles(){
  if(vcOn){
    vcOn = false;
    v=0;
    tft.setRotation(1);
    tft.fillScreen(HX8357_BLACK);
    tft.setCursor(0,0);
    tft.setTextColor(HX8357_WHITE);  tft.setTextSize(2);
    tft.println("EQUATION Visualizations");
    eqText();
    tft.setRotation(0);

  }
  

  tft.drawPixel(yv(v)+ytrans,xv(v)+xtrans,HX8357_WHITE);
  
  if(v==1200){
    tft.setRotation(1);
    tft.drawRect(180,310, 350, 20, HX8357_BLACK);
    tft.fillRect(180,310, 350, 20, HX8357_BLACK);
    tft.setRotation(0);
  }
  else if(v == 800){
    tft.setRotation(1);
    tft.drawRect(0,0, 400, 20, HX8357_BLACK);
    tft.fillRect(0,0, 400, 20, HX8357_BLACK);
    tft.setRotation(0);
  }
  delay(1);
  v++;
}

uint16_t xv(uint16_t t){
  if(eq==0){
    // change that 250 variable
    return sin(t/10)*100 + sin(t/15)*250; // + sin(t*10)*100;
  }
  else if(eq==1){
    //change the 13 in the last sin variable
    return sin(t/10)*100 + sin(t/13)*40;
  }
  else if(eq==2){
    return sin(t*5/2)*300;
  }
  //sin(t/15)*100
  //return sin(t/10)*100 * sin(t/3)*100;
}

uint16_t yv(uint16_t t){
  if(eq==0){
    return cos(t/10)*100;
  }
  else if(eq==1){
    return cos(t/10)*100 + sin(t)*40;
  }
  else if(eq==2){
    return log(t)*5-50;
  }
   //sin(t*2)*100;
  //return cos(t/10)*100;
}

void eqText(){
  tft.setRotation(1);
  tft.setCursor(180,310); tft.setTextSize(1);
  if(eq==0){
    tft.println("x=cos(t/10)*100    y=sin(t/10)*100 + sin(t/15)*250");
  }
  else if(eq==1){
    tft.println("x=cos(t/10)*100 + sin(t)*40    y=sin(t/10)*100 + sin(t/13)*40");
  }
  else if(eq==2){
    tft.println("x=cos(t/10)*100    y=sin(t/10)*100 + sin(t*10)*100");
  }
  else{
    tft.println("x=log(t)*5-50    y=sin(t*5/2)*300");
  }
  tft.setRotation(0);
}

