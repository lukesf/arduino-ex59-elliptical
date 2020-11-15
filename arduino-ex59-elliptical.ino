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

// Todo: 
//   Add visualization to show resistance and RPM - NeoMatrix?
//

// For single motor instance
#include <L298N.h>

unsigned char EN_PIN = 4;
unsigned char IN1_PIN = 5;
unsigned char IN2_PIN = 6;

unsigned char RPM_PIN = A4;
unsigned char CNT_PIN = A5;
unsigned char ZERO_PIN = A6;

unsigned char LESS_PIN = 7;
unsigned char MORE_PIN = 8;

int SPEED = 255; // 0 - 255
boolean movingForward = false;
boolean atZero = false;
boolean cntPrevHigh = false;


// With Enable pin to control speed
L298N motor(EN_PIN, IN1_PIN, IN2_PIN);
int tension=0;
int ti=0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(RPM_PIN, INPUT); //RPM Hall effect pulse
  pinMode(CNT_PIN, INPUT); //Resistence level pulse
  pinMode(ZERO_PIN, INPUT); //Zero level pulse
  pinMode(LESS_PIN, INPUT_PULLUP); //Less button
  pinMode(MORE_PIN, INPUT_PULLUP); //More button
  motor.setSpeed(SPEED);

  tension = 0;
  ti = 0;
}

void loop() {
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
            tension = tension + 1;
          else
            tension = tension - 1;
        }
        cntPrevHigh = true;
      }
      else 
        cntPrevHigh = false;
  }
  ti = ti + 1;
  if (ti > 100) {
    printSomeInfo();
    ti =0;
  }
  delay(10); 
}


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
  Serial.println(analogRead(RPM_PIN)); // Seems to be open collector.

}
