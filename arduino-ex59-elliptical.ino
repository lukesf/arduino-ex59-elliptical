//
// For Horizon EX59 Elliptical.
//
// Author: Luke Fletcher
//
// Bypassing broken console head unit and connecting directly to cable tension servo unit cable.

// Info: 
// Principle of operation: Motor with gear reduction pulls on cable (like bike brake cable). 
//    IMPORTANT: Cable pulls mechanism of magnets _AWAY_ from flywheel. 
//               Maximum resistance occurs with no tension on cable.
//               I got this logic wrong and stripped plastic gear in gearbox by pulling cable too far.
//               (See CAD in repo for model to 3D print a replacement).
//
// Connector pinout:
// 12+   Black
// GND   Orange
// Count Red
// M+    Blue
// Zero  Yellow
// M-    Green
// RPM   Purple
// NA    ???? - unknown.
//
//                   
// Zero, Count and RPM seem to be open collector digital signals so since connected to analog input to debug I added 5k pullup resistor.
// Zero - Zero endstop hall effect.
// Count - Revolution counter of servo motor unknown resolution and gear reduction.
// RPM - hall effect sensor on the flywheel.

// WARNING: It is easy break gear in servo unit if continue to drive motor when cable is tight.
// WARNING: Not Sure on original configuration of ZERO cable pulley position and Normally high/low status.
//
// Configuration: 
// Arduino Nano and L298N H-bridge to drive motor.
// Serial monitor shows status of inputs and buttons.
//
// Less button - Incrementally pulls on cable until the zero endstop is reached.
// More button - Decreases cable tension until slack. An internal counter stops at 200 "revolution counts". (Ccounter is reset by zero endstop).
// Serial monitor.

/*
Circuits+Pins:
        Led leds:
          NeoPix Pin 3 GRB - 
        buttons (as digital):
          7 = B1 - less
          8 = B2 - more
          9 = B3 - mode
        output (as digital):
          10 = beep?
          L298N
          Neopix
          
        analog (as ): 
          A4 = RPM: Active low, one pulse per revolution of flywheel 
          A5 = Count: Active low about 20 pulses per 1 tension setting on original machine: ie 10 levels => 200 pulses
          A6 = Zero:  Active high (actually active low but use the high signal)

Notes:
        BUTT1 - "Less" button - Reduces resistance. Hold down to reduce. 
	BUTT2 - "More" button - Increases resistance. Hold done to increase.  Up to max tension counter ~200 counts.
	BUTT3 - "Mode" button - Changes display to Resistance counter, RPM or dude climbing hill.
	
        If the zero hall effect signal goes high we declare we are at zero and resets the tension counter.
	On power on we don't know what the tension is so we set to 100 (mid range). 
	Holding down "Less" button will reduce tension until we hit the zero endstop and then rezero'd.
*/

#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>
#include <EasyButton.h>

// For single motor instance
#include <L298N.h>

#include <millisDelay.h>
#include <loopTimer.h>
#include "neopix-const.h"


// NEOPIX BOX WIRING CONSTANTS
#define NEOPIX_PIN 3
#define BEEP_PIN 10

#define ACTUAL_DEVICE

// Actual device
#ifdef ACTUAL_DEVICE
#define BUTT1_PIN 7
#define BUTT2_PIN 8
#define BUTT3_PIN 9
#define IN1_PIN 5
#define IN2_PIN 6
#define ZERO_PIN A6
#else
// For testing with scorer
#define BUTT1_PIN 7
#define BUTT3_PIN 6
#define BUTT4_PIN 5
#define BUTT2_PIN 4
#define IN1_PIN 8
#define IN2_PIN 9
#define ZERO_PIN A0
#endif 

#define EN_PIN 4
#define RPM_PIN A4
#define CNT_PIN A5
#define LESS_PIN BUTT1_PIN
#define MORE_PIN BUTT2_PIN

#define BAUDRATE 115200

// Max is 255, 32 is a conservative value to not overload
// a USB power supply (500mA) for 12x12 pixels.
#define BRIGHTNESS 32

// leds DECLARATION:
// Define full leds width and height.
#define mw 16
#define mh 8

Adafruit_NeoMatrix *leds = new Adafruit_NeoMatrix(8, mh, 
  mw/8, 1, 
  NEOPIX_PIN,
#ifdef ACTUAL_DEVICE
  NEO_MATRIX_TOP + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE + 
  NEO_TILE_TOP + NEO_TILE_RIGHT +  NEO_TILE_PROGRESSIVE,
#else
  NEO_MATRIX_BOTTOM + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE + 
  NEO_TILE_TOP + NEO_TILE_LEFT +  NEO_TILE_PROGRESSIVE,
#endif
  NEO_GRB + NEO_KHZ800 );

// Instance of the button.
EasyButton butt1(BUTT1_PIN);
EasyButton butt2(BUTT2_PIN);
EasyButton butt3(BUTT3_PIN);

#ifndef ACTUAL_DEVICE
EasyButton butt4(BUTT4_PIN);
millisDelay tickDelay; // the delay object
#endif

millisDelay ledsDelay; // the delay object
millisDelay printDelay; // the delay object


enum MODE { MODE_DEFAULT, MODE_RESIST, MODE_SPEED, NUM_MODES};
const char * MODE_TXT[] = {"Dude:","Re:", "Sp:", "NoMode"};
  
MODE mode = MODE_DEFAULT;

int SPEED = 255; // 0 - 255
boolean movingForward = false;
boolean atZero = false;
boolean cntPrevHigh = false;
boolean rpmPrevHigh = false;

// With Enable pin to control speed
L298N motor(EN_PIN, IN1_PIN, IN2_PIN);
int tension = 100;
unsigned long rpm_lasttime = 0;
double actual = 0.;
double rpm = 0.;
boolean dude_walker = false;

void display_scrollText(const char *txt) {
  uint8_t size = max(int(mw/8), 1);
  leds->clear();
  leds->setTextWrap(false);  // we don't wrap text so it scrolls nicely
  leds->setTextSize(1);
  leds->setRotation(0);
  int len = strlen(txt) *6;
  for (int8_t x=7; x>=-len; x--) {
    leds->clear();
    leds->setCursor(x,0);
    leds->setTextColor(LED_GREEN_HIGH);
    leds->print(txt);
    leds->show();
    delay(60);
  }
}
// Callback function to be called when the button is pressed.
void switchMode()
{
  mode = mode + 1;
  if (mode == NUM_MODES) {
    mode = 0;
  }
  String txt = "switchMode(" + String(MODE_TXT[mode]) + ")";
  Serial.println(txt);
  display_scrollText(MODE_TXT[mode]);
}



void setup() {
  Serial.begin(BAUDRATE);

  leds->begin();
  leds->setTextWrap(false);
  leds->setBrightness(BRIGHTNESS);
  // Test full bright of all LEDs. If brightness is too high
  // for your current limit (i.e. USB), decrease it.
  leds->fillScreen(LED_WHITE_HIGH);
  leds->show();
  delay(500);
  leds->clear();
  leds->show();
  ledsDelay.start(200);

  // setup IO
  pinMode(BEEP_PIN,  OUTPUT);
  // analog inputs for sensors
  pinMode(RPM_PIN, INPUT); //RPM Hall effect pulse
  pinMode(CNT_PIN, INPUT); //Resistence level pulse
  pinMode(ZERO_PIN, INPUT); //Zero level pulse

  // setup buttons
  butt1.begin();
  butt2.begin();
  butt3.begin();

#ifdef ACTUAL_DEVICE
  butt3.onPressed(switchMode);
#else
  // debugging
  butt4.begin();
  butt3.onPressedFor(2000,switchMode);
  butt3.onPressed(decr);
  butt4.onPressed(incr);
  tickDelay.start(50);
#endif

  // put your setup code here, to run once:
  motor.setSpeed(SPEED);
  rpm = 0.0;
  actual = 0.0;
  rpm_lasttime = micros();
  printDelay.start(4000);
}

void loop() {
  loopTimer.check(&Serial);
  
  // buttons
  butt1.read();
  butt2.read();
  butt3.read();
#ifndef ACTUAL_DEVICE
  butt4.read();
#endif

  // At zero cable tension
  if (analogRead(ZERO_PIN) > 200) {
    atZero = true;
    tension = 0;
  }
  else
    atZero = false;
    
  // put your main code here, to run repeatedly:
  if (digitalRead(LESS_PIN) == LOW) {
    if (not atZero) {
      motor.backward();
      movingForward = false;
    }
    else 
      motor.stop();
  }
  else if (digitalRead(MORE_PIN) == LOW) {
    if (tension < 200) {
      motor.forward();
      movingForward = true;  
    }
    else 
      motor.stop();
  }
  else {
    motor.stop();
  }
  if (motor.isMoving()) {
      if (analogRead(CNT_PIN) > 200) {
        if (not cntPrevHigh) {
          if (movingForward)
            tension += 1;
          else
            tension -= 1;
        }
        cntPrevHigh = true;
      }
      else 
        cntPrevHigh = false;
  }
  if (analogRead(RPM_PIN) < 500) {
    if (!rpmPrevHigh) {
      unsigned long now = micros();
      unsigned long deltat = now - rpm_lasttime;
      actual = 60000000.0/(double)deltat;
      const double alpha = 0.75;
      rpm = (1.-alpha) * rpm + alpha * actual;
      rpm_lasttime = now;
      dude_walker = !dude_walker;
    }
    rpmPrevHigh = true;
  }
  else
    rpmPrevHigh = false;

  // redraw  
  switch (mode) {
    case MODE_RESIST:
      display_resistance();
      break;
    case MODE_SPEED:
      display_speed();
      break;
     default:
      display_default();
      break;
  }

  // refresh
  if (ledsDelay.justFinished()) {
    leds->show();
    ledsDelay.repeat(); // for next print
  }
  if (printDelay.justFinished()) {
    printSomeInfo();
    printDelay.repeat(); // for next print
  }
#ifndef ACTUAL_DEVICE
  if (tickDelay.justFinished()) {
    tickDelay.repeat(); // for next print
    rpm_accum +=1;
  }
#endif
}

#ifndef ACTUAL_DEVICE
void incr()
{
  tension +=11;
}

void decr()
{
  tension -=11;
}
#endif

/*
Print some informations in Serial Monitor
*/
void printSomeInfo()
{
  Serial.print(" Less: ");
  Serial.print(digitalRead(LESS_PIN));
  Serial.print(" More: ");
  Serial.print(digitalRead(MORE_PIN));
  Serial.print(" ZERO: ");
  Serial.print(analogRead(ZERO_PIN));
  Serial.print(" CNT: ");
  Serial.print(analogRead(CNT_PIN));
  Serial.print(" Tension: ");
  Serial.print(tension);
  Serial.print(" Moving: ");
  Serial.print(motor.isMoving());
  Serial.print(" Speed: ");
  Serial.print(motor.getSpeed());
  Serial.print(" RPM: ");
  Serial.print(actual);
  Serial.print(" RPM: ");
  Serial.println(analogRead(RPM_PIN)); // Seems to be open collector.
}


void display_resistance() {
  leds->clear();
  leds->setTextWrap(false);  // we don't wrap text so it scrolls nicely
  leds->setTextSize(1);
  leds->setRotation(0);
  leds->clear();
  leds->setCursor(0,0);
  leds->setTextColor(LED_GREEN_HIGH);
  leds->print(tension);
}


void display_speed() {
  leds->clear();
  leds->setTextWrap(false);  // we don't wrap text so it scrolls nicely
  leds->setTextSize(1);
  leds->setRotation(0);
  leds->clear();
  leds->setCursor(0,0);
  leds->setTextColor(LED_BLUE_HIGH);
  leds->print(rpm);
}

void display_default() {
  leds->clear();
  int height = map(tension, 0, 70, 1, 8);
  for (int i=0; i<height; i++) {
    leds->drawLine(0, mh-1, mw-1, mh-1-i, LED_GREEN_LOW);
  }
  int pos_x = map(rpm, 0, 120, 1,13);
  int pos_y = 6-(int)((float) height*pos_x/13.);
  leds->drawLine(pos_x, pos_y, pos_x+(int)dude_walker, pos_y+1, LED_BLUE_MEDIUM);
  leds->drawPixel(pos_x, pos_y-1, leds->Color(100,50,20));

}
