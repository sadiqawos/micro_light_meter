#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneButton.h>

#define OLED_RESET A3
Adafruit_SSD1306 display(OLED_RESET);
#include <TSL2561.h>

#if (SSD1306_LCDHEIGHT != 32)
#error("E");
#endif

//const int batinput = A0;            // battery voltage check, 100k voltage divider
// int powercheck = A1; // testing to see if the user is trying to turn off the unit
// boolean powercontrolflag = 0;
int powercontrol = 0; // turns off 3.3V boost regulator when put HIGH, holds low after startup
byte batLevel = A0;

byte SA = 0; // eeprom location for aperture setting storage
byte SI = 1; // eeprom location for ISO setting storage
byte SM = 2; // eeprom location for mode setting storage

//MODE ARRAY
byte MODE_SIZE = 2;
const char *modearray[] = {"INCI.", "REFL."};
byte modearraypointer = 0;

// MENU ARRAY
#define MENU_SIZE 7
const char *menuarray[] = {"BAT", "ISO", "PRI.", "METER", "SHUT.", "AP..", "T-OUT"};
byte menuarraypointer = 0;

// POWER OFF TIMER ARRAY
byte POWER_SIZE = 4;
const char *powerarray[] = {"5s", "15s", "30s", "1min"};
const int powermath[] = {5, 15, 30, 60};
byte powerOffPointer = 0;

// ISO VALUE ARRAY
byte ISO_SIZE = 14;
const int ISOarray[] = {1, 3, 6, 12, 25, 50, 100, 200, 400, 800, 1600, 3200, 6400, 12800}; // values for screen
const int ISOmath[] = {6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7};                   // values for math - adjusts calculated EV for change in sensitivity
byte ISOarraypointer = 9;

// Shutter Speed array
byte SHUTTER_SIZE = 24;
const char *shutterarray[] = {"1/8k", "1/4k", "1/2k", "1/1k", "1/500", "1/250", "1/125", "1/60", "1/30", "1/15", "1/8", "1/4", "1/2", "1s", "2s", "4s", "8s", "16s", "32s", "1m", "2m", "4m", "8m", "*"}; //values for on screen
const int shuttermath[] = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11}; // values for EV calculations
byte shutterarraypointer = 23;                                                                                       // pointer points for both arrays.  Starts at 23 so it comes up blank before first measurement

// Aperture array
byte APERTURE_SIZE = 16;
const char *aperturearray[] = {"0.7", "1", "1.4", "2", "2.8", "4", "5.6", "8", "11", "16", "22", "32", "45", "64", "90", "128"}; // values for on screen
const int aperturemath[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};                                               // values for ev calculations
byte aperturearraypointer = 3;                                                                                                    // pointer points for both arrays

unsigned long lastTick;
TSL2561 tsl(TSL2561_ADDR_HIGH); // spot sensor
TSL2561 ts2(TSL2561_ADDR_LOW);  // incident sensor

// input definitions
OneButton sampleBtn(1, true);
OneButton menuDownBtn(9, true);
OneButton menuLeftBtn(8, true);
OneButton menuRightBtn(10, true);

bool menuMode = false;

void setup() {
  // buttons setup
  sampleBtn.attachClick(takeSample);
  menuDownBtn.attachClick(menuDown);
  menuLeftBtn.attachClick(menuLeft);
  menuRightBtn.attachClick(menuRight);

  /**
   * Hold the device ON by driving the power control pin HIGH
  */
  pinMode(powercontrol, OUTPUT);
  digitalWrite(powercontrol, HIGH);

  tsl.setGain(TSL2561_GAIN_16X); // set 16x gain (for dim situations)
  ts2.setGain(TSL2561_GAIN_16X);

  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  tsl.setTiming(TSL2561_INTEGRATIONTIME_101MS); // medium integration time (medium light)
  ts2.setTiming(TSL2561_INTEGRATIONTIME_101MS);

  // load last settings from the EEPROM
  aperturearraypointer = EEPROM.read(SA);
  ISOarraypointer = EEPROM.read(SI);
  modearraypointer = EEPROM.read(SM);

  // aperture pointer out of range means the EEPROM is blank, need to clear all these bad reads
  if (aperturearraypointer < 0 || aperturearraypointer > 15) {
    aperturearraypointer = 0;
    ISOarraypointer = 0;
    modearraypointer = 0;
  }

  // for blue/yellow display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3c); // initialize with the I2C addr 0x3D (for the 128x64)
  display.setTextSize(2);
  display.setTextColor(WHITE);

  display.clearDisplay();
  display.display();
  delay(10);
  takeSample();
}

void loop() {
  //  updateBatteryLevel(readBatteryLevel());
  sampleBtn.tick();
  menuDownBtn.tick();
  menuLeftBtn.tick();
  menuRightBtn.tick();
  tryShutdown();
  delay(10);
}

void takeSample() {
  resetClock();
  if (menuMode) { // Save settings before exit menu page
    EEPROM.write(SA,aperturearraypointer);
    EEPROM.write(SI,ISOarraypointer);
    EEPROM.write(SM,modearraypointer);
  }
  menuMode = false;

  int lux = getLuminosityReading();

  float ftcd = lux / 10.764; // convert to foot candles
  float ev = 1.4481 * log(ftcd) + 1.6599; // convert footcandles to exposure value
  ev = log(lux / 2.5) / log(2); // EV formula using lux

  int roundev = round(ev); // round EV to help with math

  unsigned int dislux = lux;                 // truncated lux because the decimals aren't worth anything
  int disftcd = round(ftcd);        // round footcandles to remove useless extras in display value
  int evdecimals = ev * 10;         // separate the decimal digit
  evdecimals = evdecimals % 10; // remove all but the last digit
  evdecimals = abs(evdecimals); // prevent the decimals from possibly being negative

  // go calculate the shutter speed
  calculations(roundev); // recalculates the shutter/aperture combo based on new value
  displaySample();
}

void menuDown() {
  resetClock();
  if (menuarraypointer < MENU_SIZE-1) {
    menuarraypointer++;
  } else {
    menuarraypointer = 0;
  }
  menuMode = true;
  drawMenu(menuarraypointer, getMenuValue(menuarraypointer, false, false));
}

void menuLeft() {
  resetClock();
  if (!menuMode) {
    return;
  }
  String value = getMenuValue(menuarraypointer, true, false);
  drawMenu(menuarraypointer, value);
}

void menuRight() {
  resetClock();
  if (!menuMode) {
    return;
  }
  String value = getMenuValue(menuarraypointer, false, true);
  drawMenu(menuarraypointer, value);
}

String getMenuValue(int pointer, boolean left, boolean right) {
  String value;
  switch (pointer) {
    case 0:
      value = String("100 %");
      break;
    case 1:
      ISOarraypointer = recalculatePointer(ISOarraypointer, 0, ISO_SIZE, left, right);
      value = String(ISOarray[ISOarraypointer]);
      break;
    case 2:
      value = "AP"; //SP for shutter
      break;
    case 3:
      modearraypointer = recalculatePointer(modearraypointer, 0, MODE_SIZE, left, right);
      value = String(modearray[modearraypointer]);
      break;
    case 4:
      shutterarraypointer = recalculatePointer(shutterarraypointer, 0, SHUTTER_SIZE, left, right);
      value = String(shutterarray[shutterarraypointer]);
      break;
    case 5:
         aperturearraypointer = recalculatePointer(aperturearraypointer, 0, APERTURE_SIZE, left, right);
         value = String("f/" + String(aperturearray[aperturearraypointer]));//.concat(aperturearray[aperturearraypointer]);
      break;
    case 6:
      powerOffPointer = recalculatePointer(powerOffPointer, 0, POWER_SIZE, left, right);
      value = String(powerarray[powerOffPointer]);
      break;
  }
  return value;
}

byte recalculatePointer(byte pointer, byte minVal, byte maxVal, boolean left, boolean right) {
  if (left == true) {
    return decPointer(pointer, minVal, maxVal - 1);
  } else if (right == true) {
    return incPointer(pointer, maxVal, minVal);
  } else {
    return pointer;
  }
}

byte decPointer(byte val, byte minVal, byte resetVal) {
  if (val > minVal) {
    val--;
  } else if (val == minVal) {
    val = resetVal;
  }
  return val;
}

byte incPointer(byte val, byte maxVal, byte resetVal) {
  if (val < maxVal-1) {
    val++;
  } else {
    val = resetVal;
  }
  return val;
}

void displaySample() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(F("f/"));
  display.println(aperturearray[aperturearraypointer]);
  display.println(shutterarray[shutterarraypointer]);

  display.display();
}

void drawMenu(byte pointer, String value) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(menuarray[pointer]);
  display.print(value);
  display.display();
}

int getLuminosityReading() {
  switch (modearraypointer) {
    case 0:
      return tsl.getLuminosity(TSL2561_VISIBLE);
    case 1:
      return tsl.getLuminosity(TSL2561_VISIBLE);
    case 2:
      return tsl.getLuminosity(TSL2561_VISIBLE);
  }
}

void calculations(int roundev) {
  if (roundev != 0) { // checks there is a reading value, skip calculations and leave shutterarraypointer at 23 so it shows a blank
    int cev = 0;
    do {
      cev = shuttermath[shutterarraypointer] + aperturemath[aperturearraypointer] + ISOmath[ISOarraypointer];

      if (cev < roundev && shutterarraypointer >= 1 && shutterarraypointer <= 23) {// calculated EV is less than measured EV, decrement shutter speed (add more light)
        shutterarraypointer--;
      } else if (cev > roundev && shutterarraypointer >= 0 && shutterarraypointer <= 22) {// calculated EV is more than measured EV, increment shutter speed (remove light)
        shutterarraypointer++;
      }
    } while (cev != roundev);
  }
}

void resetClock() {
  lastTick = millis();
}

void tryShutdown() {
  unsigned long timeSinceLastActivity = millis() - lastTick;
  if (timeSinceLastActivity > (powermath[powerOffPointer] * 1000)) {
    display.clearDisplay();
    display.display();
    delay(10);
    pinMode(powercontrol, OUTPUT);   // power control pin goes to output mode
    digitalWrite(powercontrol, LOW); // power off
  }
}

/**
void updateBatteryLevel(currentLevel) {
  currentLevel = ((analogRead(batinput) - 310) * 1.61);
  if (batLevel != currentLevel) {
    batLevel = currentLevel;

    display.setCursor(36, 0);
    // display.print(F("BAT:"));
    display.print(batpercent);
    display.print(F("%"));
  }
}
*/
