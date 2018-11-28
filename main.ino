#include <SPI.h>
#include <Wire.h>
#include <Math.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET A3
Adafruit_SSD1306 display(OLED_RESET);
#include <TSL2561.h>

#define NUMFLAKES 10
#define XPOS 0
#define YPOS 1
#define DELTAY 2

#define LOGO16_GLCD_HEIGHT 16
#define LOGO16_GLCD_WIDTH 16

#if (SSD1306_LCDHEIGHT != 64)
#error("E");
#endif

// input definitions
const int button1 = 8;
const int button2 = 11;
const int button3 = 3;
const int button4 = 4;
const int button5 = 9;
const int button6 = 5;
const int button7 = 10;

int powercheck = A1; // testing to see if the user is trying to turn off the unit

//output definitions
int led = 13;          // led in viewfinder
int powercontrol = A2; // turns off 3.3V boost regulator when put HIGH, holds low after startup

// button flags
int button1state, button2state, button3state, button4state, button5state, button6state, button7state, powercontrolstate = 0; // button input holders
boolean button1flag, button2flag, button3flag, button4flag, button5flag, button6flag, button7flag, powercontrolflag = 0;     // flags to detect read so one button press = one action only

const int batinput = A0;            // battery voltage check, 100k voltage divider
int cev = 0;                  // calculated EV value

int otc = 0; // on time counter

//EEPROM storage locations
int SA = 0; // eeprom location for aperture setting storage
int SI = 1; // eeprom location for ISO setting storage
int SM = 2; // eeprom location for mode setting storage

//MODE ARRAY
const char *modearray[] = {"INCIDENT", "REFLECT"};
byte modearraypointer = 0;

// MENU ARRAY
const char *menuarray[] = {"BATTERY", "ISO", "PRIORITY MODE", "METERING MODE", "SHUTTER", "APERTURE", "POWER OFF TIMER"};
int menuarraypointer = 0;

// POWER OFF TIMER ARRAY
const char *powerarray[] = {"5s", "15s", "30s", "1min"};
const int powermath[] = {5, 15, 30, 60};

// ISO VALUE ARRAY
const int ISOarray[] = {1, 3, 6, 12, 25, 50, 100, 200, 400, 800, 1600, 3200, 6400, 12800}; // values for screen
const int ISOmath[] = {6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7};                   // values for math - adjusts calculated EV for change in sensitivity
byte ISOarraypointer = 9;
int ISOmathholder = 0;

// Shutter Speed array

//FLASH_STRING_ARRAY(shutterarray,"1/8k","1/4k","1/2k","1/1k","1/500","1/250","1/125","1/60","1/30","1/15","1/8","1/4","1/2","1s","2s","4s","8s","16s","32s","1m","2m","4m","8m"," ");  //values for on screen
const char *shutterarray[] = {"1/8k", "1/4k", "1/2k", "1/1k", "1/500", "1/250", "1/125", "1/60", "1/30", "1/15", "1/8", "1/4", "1/2", "1s", "2s", "4s", "8s", "16s", "32s", "1m", "2m", "4m", "8m", " "}; //values for on screen
//const char* shutterarray[] = {};  //values for on screen
const int shuttermath[] = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11}; // values for EV calculations
byte shutterarraypointer = 23;                                                                                       // pointer points for both arrays.  Starts at 23 so it comes up blank before first measurement
int shuttermathholder = 0;

// Aperture array
const char *aperturearray[] = {"0.7", "1", "1.4", "2", "2.8", "4", "5.6", "8", "11", "16", "22", "32", "45", "64", "90", "128"}; // values for on screen
const int aperturemath[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};                                               // values for ev calculations
byte aperturearraypointer = 3;                                                                                                    // pointer points for both arrays
int aperturemathholder = 0;

// Example for demonstrating the TSL2561 library - public domain!

// connect SCL to analog 5
// connect SDA to analog 4
// connect VDD to 3.3V DC
// connect GROUND to common ground
// ADDR can be connected to ground, or vdd or left floating to change the i2c address

// The address will be different depending on whether you let
// the ADDR pin float (addr 0x39), or tie it to ground or vcc. In those cases
// use TSL2561_ADDR_LOW (0x29) or TSL2561_ADDR_HIGH (0x49) respectively

TSL2561 tsl(TSL2561_ADDR_HIGH); // spot sensor
TSL2561 ts2(TSL2561_ADDR_LOW);  // incident sensor

long lux = 0;            // lux
unsigned int dislux = 0; // truncated lux value for display
float ftcd = 0;          // foot-candles
int disftcd = 0;         // rounded ftcd value for display
float ev = 0;            // Exposure value
int roundev = 0;         // rounded Exposure value
int evints = 0;          // the integer part of EV
int evdecimals = 0;      // the decimal part of EV
int evhold = 0;          // for converting from float to int
boolean ledblink = 0;    // viewfinder led

//###################   SETUP #####################

void setup()
{
  // buttons setup
  pinMode(button1, INPUT_PULLUP); // read button
  pinMode(button2, INPUT_PULLUP);
  pinMode(button3, INPUT_PULLUP);
  pinMode(button4, INPUT_PULLUP);

  // other pins setup
  pinMode(led, OUTPUT);         // viewfinder LED
  digitalWrite(led, HIGH);      //viewfinder LED on
  pinMode(powercontrol, INPUT); // power control pin

  //activating internal pullups
  digitalWrite(button1, HIGH);
  digitalWrite(button2, HIGH);
  digitalWrite(button3, HIGH);
  digitalWrite(button4, HIGH);

  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  //tsl.setGain(TSL2561_GAIN_0X);         // set no gain (for bright situtations)

  tsl.setGain(TSL2561_GAIN_16X); // set 16x gain (for dim situations)
  ts2.setGain(TSL2561_GAIN_16X);

  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  //tsl.setTiming(TSL2561_INTEGRATIONTIME_13MS);  // shortest integration time (bright light)
  tsl.setTiming(TSL2561_INTEGRATIONTIME_101MS); // medium integration time (medium light)
  //tsl.setTiming(TSL2561_INTEGRATIONTIME_402MS);  // longest integration time (dim light)
  ts2.setTiming(TSL2561_INTEGRATIONTIME_101MS);
  //ts2.setTiming(TSL2561_INTEGRATIONTIME_402MS);

  // load last settings from the EEPROM
  aperturearraypointer = EEPROM.read(SA);
  ISOarraypointer = EEPROM.read(SI);
  modearraypointer = EEPROM.read(SM);

  if (aperturearraypointer < 0 || aperturearraypointer > 15) {// aperture pointer out of range means the EEPROM is blank, need to clear all these bad reads
    aperturearraypointer = 0;
    ISOarraypointer = 0;
    modearraypointer = 0;
  }

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)

  // for blue/yellow display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D); // initialize with the I2C addr 0x3D (for the 128x64)

  // for white display (address 0x3C - ebay board says 0x78)
  // display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64)
  // init done
}

//###################   MAIN LOOP #####################

void loop() {

  //power control
  /*
  if(analogRead(powercheck) > 300 && powercontrolflag == 0 && powercontrolstate == 0) // state = 0, power was previously off (default state on boot) 
  {

         powercontrolflag = 1; // set flag high
         powercontrolstate = 1; // power was turned on this time

  }
  */
  if (analogRead(powercheck) > 300 || otc > 300) // power button is being pressed, or time is up, shut down
  {
    otc = 0;                // clear OTC
    display.clearDisplay(); // clears old stuff
    delay(500);
    display.display();

    pinMode(powercontrol, OUTPUT);   // power control pin goes to output mode
    digitalWrite(powercontrol, LOW); // power off
  }
  /*  
  else if(analogRead(powercheck) < 300 && powercontrolflag == 1)
 {
   powercontrolflag = 0;  // reset flag if button is unpressed but flag is high - ready to be pressed again
 } 
*/

  // battery level check
  batlevel = analogRead(batinput); // read the battery

  batpercent = (batlevel - 310) * 1.61;

  // text display
  display.setTextSize(1);
  display.setTextColor(WHITE);

  //MODE DISPLAY

  display.clearDisplay(); // clears old stuff

  /* 
  display.setCursor(5,0);
  display.print(F("MODE: "));
  display.println(modearraypointer);
  display.println(modearray[modearraypointer]); 
*/

  //battery display
  display.setCursor(36, 0);
  //  display.print(F("BAT:"));
  display.print(batpercent);
  display.print(F("%"));
  //display.print(batlevelint);
  // display.print(F("."));
  // display.print(batleveldec);
  // display.println(F("V"));
  //divider line

  //display.drawLine(63, 16, 63, 64, WHITE);
  /*
  //right side line 1
  display.setCursor(70,20);
  display.print(F("ISO:"));
  display.println(ISOarray[ISOarraypointer]);
  // line 2
  display.setCursor(70,32);
  display.print(F("EV:"));
  display.print(evints);
  display.print(F("."));
  display.println(evdecimals);
  //line 3
  display.setCursor(70,44);
  display.print(dislux);
  display.println(F(" Lx"));
  //line 4
  display.setCursor(70,56);
  display.print(disftcd); //disftcd
  display.println(F(" FtCd"));
 */

  // second set

  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0); // first num is horz, second is vert
  display.print(F("f/"));
  display.println(aperturearray[aperturearraypointer]);

  // display.setTextSize(1);
  //display.setCursor(5,32); // first num is horz, second is vert

  // TEST CODE FOR DEVELOPMENT
  //display.print(roundev);
  // display.print(" ");
  // display.print(aperturearraypointer);
  //display.print(" ");
  //display.print(ISOarraypointer);

  display.setTextSize(2);
  display.setCursor(0, 16); // first num is horz, second is vert
  display.println(shutterarray[shutterarraypointer]);

  //writes to the display
  display.display();
  delay(10);

  otc++; // increment on time counter

  // button checks - reads all the buttons
  button1state = digitalRead(button1);
  button2state = digitalRead(button2);
  button3state = digitalRead(button3);
  button4state = digitalRead(button4);

  // BUTTON 1 ACTIONS - SAMPLE AND PROCESS

  // if button is pressed and read flag is low (no reading yet)
  if (button1state == LOW && button1flag == 0) {
    // lux read and corrective math
    otc = 0; // reset OTC
    if (modearraypointer == 0) {
      lux = ts2.getLuminosity(TSL2561_VISIBLE); // inc sensor
      // lux = lux / 3.828; // calibrated value for sensor 1as per Sekonic L398 readings
    } else if (modearraypointer == 1) {
      // lux = lux / 3.828; // calibrated value for sensor 1 as per Pentax V spotmeter off grey card
      lux = tsl.getLuminosity(TSL2561_VISIBLE); // spot sensor
    } else if (modearraypointer == 2) {
      /**
       * lux = lux * 26.719;  // calibrated value for sensor 2 as per Pentax V spotmeter off grey card
       * sensor will be at bottom of tube so value will be drastically reduced 27.3504
       **/
      lux = tsl.getLuminosity(TSL2561_VISIBLE); // spot sensor
    }

    ftcd = lux / 10.764; // convert to foot candles

    // Find the EV

    ev = 1.4481 * log(ftcd) + 1.6599; // convert footcandles to exposure value

    // new formula seems more accurate

    ev = log(lux / 2.5) / log(2); // EV formula using lux

    roundev = round(ev); // round EV to help with math

    // display values prep
    dislux = lux;                 // truncated lux because the decimals aren't worth anything
    disftcd = round(ftcd);        // round footcandles to remove useless extras in display value
    evdecimals = ev * 10;         // separate the decimal digit
    evdecimals = evdecimals % 10; // remove all but the last digit
    evdecimals = abs(evdecimals); // prevent the decimals from possibly being negative

    // go calculate the shutter speed
    calculations(); // recalculates the shutter/aperture combo based on new value

    button1flag = 1; // set flag high
  } else if (button1state == HIGH && button1flag == 1) {
    button1flag = 0; // reset lux read flag if button is unpressed but flag is high - ready to be pressed again
  }

  // BUTTON 2 ACTIONS - MENU SCROLL

  if (button2state == LOW && button2flag == 0) { //&& ISOarraypointer >= 1 && ISOarraypointer <= 13
    otc = 0;            // reset OTC
    menuarraypointer++; //increment the menu pointer

    button2flag = 1; // set flag high
  } else if (button2state == HIGH && button2flag == 1) {
    button2flag = 0; // reset flag if button is unpressed but flag is high - ready to be pressed again
  }

  // BUTTON 3 ACTIONS - MENU GO LEFT

  if (button3state == LOW && button3flag == 0) {// && aperturearraypointer >= 1 && aperturearraypointer <= 15
    otc = 0; // reset OTC

    //add case/switch to increment a counter for the current menu option
    button3flag = 1; // set flag high
  } else if (button3state == HIGH && button3flag == 1) {
    button3flag = 0; // reset flag if button is unpressed but flag is high - ready to be pressed again
  }

  // BUTTON 4 ACTIONS  - MENU GO RIGHT

  if (button4state == LOW && button4flag == 0 && aperturearraypointer >= 0 && aperturearraypointer <= 14) {
    otc = 0; // reset OTC

    // add case/switch to decrement a counter for the current menu option

    button4flag = 1; // set flag high
  } else if (button4state == HIGH && button4flag == 1) {
    button4flag = 0; // reset flag if button is unpressed but flag is high - ready to be pressed again
  }

  /*save eeprom code
      EEPROM.write(SA,aperturearraypointer);
      EEPROM.write(SI,ISOarraypointer);
      EEPROM.write(SM,modearraypointer);
*/
}

void calculations()
{

  if (roundev != 0) // checks there is a reading value, skip calculations and leave shutterarraypointer at 23 so it shows a blank
  {
    do
    {

      shuttermathholder = shuttermath[shutterarraypointer];
      aperturemathholder = aperturemath[aperturearraypointer];
      ISOmathholder = ISOmath[ISOarraypointer];

      cev = shuttermathholder + aperturemathholder + ISOmathholder;

      evints = roundev + ISOmathholder;

      // Calculate shutter/aperture combination

      if (cev < roundev && shutterarraypointer >= 1 && shutterarraypointer <= 23) // calculated EV is less than measured EV, decrement shutter speed (add more light)
      {
        shutterarraypointer--;
      }
      else if (cev > roundev && shutterarraypointer >= 0 && shutterarraypointer <= 22) // calculated EV is more than measured EV, increment shutter speed (remove light)
      {
        shutterarraypointer++;
      }

    } while (cev != roundev);
  }
}
