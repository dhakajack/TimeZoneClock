//**************************************************************//
//  Name    : Three Time Zone Clock
//  Author  : Jack
//  Date    : 16 May 2016                                                       
//  Repo    : https://github.com/dhakajack/TimeZoneClock
//  License  : MIT License, (c) 2016 Jack Welch; see LICENSE file
//  RTClib from Adafruit
//
//****************************************************************

#include <Wire.h>
#include "RTClib.h" //Adafruit RTClib library for DS3231

RTC_DS3231 rtc;

//switches - all are active low
#define SLEEP_WAKE  0
#define SET_HOURS   1
#define SET_MINUTES 2
#define SET_ZONE2   3
#define SET_ZONE3   4
#define EDIT_MODE   5

//control lines for shift registers
#define SER         7
#define SRCK        8
#define RCK         9

//lines controlling individual segments; switched through PNP, so active low
#define SEG_E      10
#define SEG_F      11
#define SEG_G      12
#define SEG_DP     13  // dpi isn't used but is retained for future use
#define SEG_A      14
#define SEG_B      15
#define SEG_C      16
#define SEG_D      17

// SDA   a4/d18/pin27
// SCL   a5/d19/pin28

//index values for each segment
#define TOP 0 // A
#define UPPER_R 1 // B
#define LOWER_R 2 // C
#define BOTTOM 3 // D
#define LOWER_L 4 // E
#define UPPER_L 5 // F 
#define MIDDLE 6 // G
#define DECIMAL 7 // dp - although dp isn't used for the clock, it is wired up

//index values for zone
#define ZONE1 0
#define ZONE2 1
#define ZONE3 2

//all times in milliseconds
#define REFRESH_TIME   500    // ms, clock refresh and blink rate
#define RESPONSE_TIME  300   // how often to respond to button presses
#define BLINK_TIME     100   // how fast to blink
#define DELAY_TIME     1     // 1-2 seems to work well, with low flicker

//rollover values
#define HOURS_MAX 24
#define MINUTES_MAX 60

const int clock_address = 0x68; // I2C write address for clock
const int clock_statusRegister = 0x0F; 

//digital outputs driving the respective segments
int segments[8] = {SEG_A,SEG_B,SEG_C,SEG_D,SEG_E,SEG_F,SEG_G,SEG_DP};

boolean number[11][7]; // holds information about which segments are needed for each of the 10 numbers

// holds values of the display digits - ZONE1, ZONE2, ZONE3
int digits[3][4];

int hour;  // reference time, zone 1
int minute; // same for all time zones; would need modification to accommodate fractional time zones, e.g., India
int editZone = ZONE1;
int offset[3]; // offet[timezone]

boolean editMode = false;
boolean sleepMode = false;
boolean blinkStatus = false; 

unsigned long lastRefresh = 0;
unsigned long lastResponse = 0;
unsigned long lastBlink = 0;

void setup() {
  
  rtc.begin();
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  pinMode(RCK, OUTPUT);
  pinMode(SRCK, OUTPUT);
  pinMode(SER, OUTPUT);
  pinMode(SLEEP_WAKE,INPUT_PULLUP);
  pinMode(SET_MINUTES,INPUT_PULLUP);
  pinMode(SET_HOURS,INPUT_PULLUP);
  pinMode(SET_ZONE2,INPUT_PULLUP);
  pinMode(SET_ZONE3,INPUT_PULLUP);
  pinMode(EDIT_MODE,INPUT_PULLUP);
  
  for (int segIndex = 0;segIndex < 8;segIndex++){
    pinMode(segments[segIndex], OUTPUT);
    digitalWrite(segments[segIndex],HIGH);  // turn off - since segments are driven by high-side PNP
  
  initNumber();  
  }
 
  offset[ZONE1] = 0;  //UTC
  /*adjust these +/- 23h */
  offset[ZONE2] = 3; //mada
  offset[ZONE3] = -4; //Eastern

}

void loop() {
  DateTime now;
  
  now = rtc.now();  //snapshot of the current time - one atomic read for hour, minutes, etc.
  
  if((millis() - lastRefresh) >=  REFRESH_TIME) {  // how often to update the display
      lastRefresh = millis();
      
    if (!editMode) { 
      hour = now.hour();
      minute = now.minute();
    }
  }
  
  if ((millis() - lastResponse) >= RESPONSE_TIME) { // button presses are relatively slow, 
                                                    // so not specifically debounced
    
    if(digitalRead(EDIT_MODE) == LOW) {
      editMode = true;
      sleepMode = false;
    } else {
      editMode = false;
    }
    
   if (editMode) {
     
     if (digitalRead(SET_HOURS) == LOW) {  // bumping zone one up, bumps up all the other zones as well
       editZone = ZONE1;
       hour++;
       fixPeriod(&hour,HOURS_MAX);
     } else if (digitalRead(SET_MINUTES) == LOW) { // minutes are the same across all zones
       editZone = ZONE1;
       minute++;
       fixPeriod(&minute,MINUTES_MAX);
     } else if (digitalRead(SET_ZONE2) == LOW) {  // adjust hours only for zone 2
       editZone = ZONE2;
       offset[ZONE2]++;
       fixPeriod(&offset[ZONE2],HOURS_MAX);
     } else if (digitalRead(SET_ZONE3) == LOW) {  // adjust hours only for zone 3
       offset[ZONE3]++;
       editZone = ZONE3;
       fixPeriod(&offset[ZONE3],HOURS_MAX);
     }  
   
     rtc.adjust(DateTime(2016, 1, 1, hour, minute, 0));  // dummy values for year, month, day
   } 
   
   if(!editMode) {
     if (digitalRead(SLEEP_WAKE) == LOW) {  // don't want to sleep/wake in edit mode
       sleepMode = !sleepMode;
     }
   }
   
   lastResponse = millis(); 
  }
    
  updateTime(); // stick new time in the zone arrays
  
  if ((millis() - lastBlink) >= BLINK_TIME) {
    blinkStatus = !blinkStatus;
    lastBlink = millis();
  }
  
  if(editMode && blinkStatus) {
    blankBlinky(editZone);
  }

  if(!sleepMode) {
    updateDisplay();
  }
}

void updateTime() {
  int temphr;
  int tempmintens;
  int tempminones;
  
  tempmintens = (minute/10) %10;
  tempminones = minute %10;
  
  for(int zone=ZONE1; zone < ZONE3 + 1; zone++) {
    temphr = hour + offset[zone];
    fixPeriod(&temphr,HOURS_MAX);
    
     digits[zone][0] = (temphr/10) %10;
     digits[zone][1] = temphr %10;
     digits[zone][2] = tempmintens;
     digits[zone][3] = tempminones;
  }
}

void updateDisplay() {
    int reg; //register

  for (int segIndex = 0; segIndex < 7; segIndex++){  
    reg = 0;
    clearBuffer();  // blanks the display
    digitalWrite(segments[segIndex],LOW);
    
    for(int units = ZONE3; units > -1; units--) {
      for(int index = 3; index > -1; index--) {
        reg = reg << 1;
        if(number[digits[units][index]][segIndex]) { // set up shift register pattern
          reg += 1;
        }
      } 
    } 
    stuffBuffer(reg);
    displayDelay();
    digitalWrite(segments[segIndex],HIGH); // turn off segment 
    
  }
}

void displayDelay() {
  delay(DELAY_TIME); // time to persist after lighting up a segment
}

void clearBuffer() {
    digitalWrite(RCK, LOW);
    shiftOut(SER, SRCK, MSBFIRST, 0);
    shiftOut(SER, SRCK, MSBFIRST, 0);
    digitalWrite(RCK, HIGH);
}

void stuffBuffer(int pattern) {
    digitalWrite(RCK, LOW); 
    shiftOut(SER, SRCK, MSBFIRST, pattern >> 8);
    shiftOut(SER, SRCK, MSBFIRST, pattern & 255);
    digitalWrite(RCK, HIGH);
}

void blankBlinky (int units) {
  digits[units][0] = digits[units][1] = digits[units][2] = digits[units][3] = 10;
}

void fixPeriod(int *unitval, int maxval) {
  if (*unitval > (maxval - 1)) {
    *unitval-= maxval;
  }
  if (*unitval < 0) {
    *unitval+= maxval;
  }
}
  
void initNumber() {
  
  number[0][MIDDLE]  = false;
  number[0][UPPER_L] = true;
  number[0][LOWER_L] = true;
  number[0][BOTTOM]  = true;
  number[0][LOWER_R] = true;
  number[0][UPPER_R] = true;
  number[0][TOP]     = true;

  number[1][MIDDLE]  = false;
  number[1][UPPER_L] = false;
  number[1][LOWER_L] = false;
  number[1][BOTTOM]  = false;
  number[1][LOWER_R] = true;
  number[1][UPPER_R] = true;
  number[1][TOP]     = false;

  number[2][MIDDLE]  = true;
  number[2][UPPER_L] = false;
  number[2][LOWER_L] = true;
  number[2][BOTTOM]  = true;
  number[2][LOWER_R] = false;
  number[2][UPPER_R] = true;
  number[2][TOP]     = true;

  number[3][MIDDLE]  = true;
  number[3][UPPER_L] = false;
  number[3][LOWER_L] = false;
  number[3][BOTTOM]  = true;
  number[3][LOWER_R] = true;
  number[3][UPPER_R] = true;
  number[3][TOP]     = true;

  number[4][MIDDLE]  = true;
  number[4][UPPER_L] = true;
  number[4][LOWER_L] = false;
  number[4][BOTTOM]  = false;
  number[4][LOWER_R] = true;
  number[4][UPPER_R] = true;
  number[4][TOP]     = false;

  number[5][MIDDLE]  = true;
  number[5][UPPER_L] = true;
  number[5][LOWER_L] = false;
  number[5][BOTTOM]  = true;
  number[5][LOWER_R] = true;
  number[5][UPPER_R] = false;
  number[5][TOP]     = true;

  number[6][MIDDLE]  = true;
  number[6][UPPER_L] = true;
  number[6][LOWER_L] = true;
  number[6][BOTTOM]  = true;
  number[6][LOWER_R] = true;
  number[6][UPPER_R] = false;
  number[6][TOP]     = true;

  number[7][MIDDLE]  = false;
  number[7][UPPER_L] = false;
  number[7][LOWER_L] = false;
  number[7][BOTTOM]  = false;
  number[7][LOWER_R] = true;
  number[7][UPPER_R] = true;
  number[7][TOP]     = true;

  number[8][MIDDLE]  = true;
  number[8][UPPER_L] = true;
  number[8][LOWER_L] = true;
  number[8][BOTTOM]  = true;
  number[8][LOWER_R] = true;
  number[8][UPPER_R] = true;
  number[8][TOP]     = true;

  number[9][MIDDLE]  = true;
  number[9][UPPER_L] = true;
  number[9][LOWER_L] = false;
  number[9][BOTTOM]  = true;
  number[9][LOWER_R] = true;
  number[9][UPPER_R] = true;
  number[9][TOP]     = true;
  
  //the value 10 is treated as a blank
  number[10][MIDDLE]  = false;
  number[10][UPPER_L] = false;
  number[10][LOWER_L] = false;
  number[10][BOTTOM]  = false;
  number[10][LOWER_R] = false;
  number[10][UPPER_R] = false;
  number[10][TOP]     = false;
}

