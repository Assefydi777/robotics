/*
 *  RobotCarFollowerSimple.cpp
 *
 *  Enables follower mode driving of a 2 or 4 wheel car with an Arduino and a dual full bridge (e.g. TB6612 or L298) for motor control.
 *
 *  Copyright (C) 2020  Armin Joachimsmeyer
 *  armin.joachimsmeyer@gmail.com
 *
 *  This file is part of Arduino-RobotCar https://github.com/ArminJo/PWMMotorControl.
 *
 *  PWMMotorControl is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/gpl.html>.
 *
 */

#include <Arduino.h>

#include "CarPWMMotorControl.h"
#include "Servo.h"
#include "HCSR04.h"

/*
 * Speed compensation to enable driving straight ahead.
 * If positive, this value is subtracted from the speed of the right motor -> the car turns slightly right.
 * If negative, -value is subtracted from the left speed -> the car turns slightly left.
 */
#define SPEED_PWM_COMPENSATION_RIGHT    0

#define DISTANCE_MINIMUM_CENTIMETER 20 // If measured distance is less than this value, go backwards
#define DISTANCE_MAXIMUM_CENTIMETER 30 // If measured distance is greater than this value, go forward

//#define VIN_2_LIPO
#if defined(VIN_2_LIPO)
// values for 2xLIPO / 7.4 volt
#define START_SPEED_PWM                 55 // Speed PWM value at which car starts to move.
#define DRIVE_SPEED_PWM                 90 // Speed PWM value used for going fixed distance.
#define MAX_SPEED_PWM_FOLLOWER         135 // Max speed PWM value used for follower.
#else
// Values for 4xAA / 6.0 volt
#define START_SPEED_PWM                140 // Speed PWM value at which car starts to move.
#define DRIVE_SPEED_PWM                220 // Speed PWM value used for going fixed distance.
#define MAX_SPEED_PWM_FOLLOWER         255 // Max speed PWM value used for follower.
#endif

#if ! defined(USE_ADAFRUIT_MOTOR_SHIELD) // enable it in PWMDCMotor.h
/*
 * Pins for direct motor control with PWM and a dual full bridge e.g. TB6612 or L298.
 * 2 + 3 are reserved for encoder input
 */
#define PIN_RIGHT_MOTOR_FORWARD     4 // IN4 <- Label on the L298N board
#define PIN_RIGHT_MOTOR_BACKWARD    7 // IN3
#define PIN_RIGHT_MOTOR_PWM         5 // ENB - Must be PWM capable

#define PIN_LEFT_MOTOR_FORWARD      9 // IN1
#define PIN_LEFT_MOTOR_BACKWARD     8 // IN2
#define PIN_LEFT_MOTOR_PWM          6 // ENA - Must be PWM capable
#endif

#ifdef USE_ENCODER_MOTOR_CONTROL
#define RIGHT_MOTOR_INTERRUPT    INT0 // Pin 2
#define LEFT_MOTOR_INTERRUPT     INT1 // Pin 3
#endif

#define PIN_DISTANCE_SERVO         10 // Servo Nr. 2 on Adafruit Motor Shield

#define PIN_BUZZER                 12

#define PIN_TRIGGER_OUT            A0 // Connections on the Arduino Sensor Shield
#define PIN_ECHO_IN                A1

//Car Control
CarPWMMotorControl RobotCarPWMMotorControl;

Servo DistanceServo;

unsigned int getDistanceAndPlayTone();

/*
 * Start of robot car control program
 */
void setup() {
    Serial.begin(115200);

    // Just to know which program is running on my Arduino
    Serial.println(F("START " __FILE__ " from " __DATE__ "\r\nUsing library version " VERSION_PWMMOTORCONTROL));

#ifdef USE_ADAFRUIT_MOTOR_SHIELD
    RobotCarPWMMotorControl.init();
#else
#  ifdef USE_ENCODER_MOTOR_CONTROL
    RobotCarPWMMotorControl.init(PIN_RIGHT_MOTOR_FORWARD, PIN_RIGHT_MOTOR_BACKWARD, PIN_RIGHT_MOTOR_PWM, RIGHT_MOTOR_INTERRUPT, PIN_LEFT_MOTOR_FORWARD,
    PIN_LEFT_MOTOR_BACKWARD, PIN_LEFT_MOTOR_PWM, LEFT_MOTOR_INTERRUPT);
#  else
    RobotCarPWMMotorControl.init(PIN_RIGHT_MOTOR_FORWARD, PIN_RIGHT_MOTOR_BACKWARD, PIN_RIGHT_MOTOR_PWM, PIN_LEFT_MOTOR_FORWARD,
    PIN_LEFT_MOTOR_BACKWARD, PIN_LEFT_MOTOR_PWM);
#  endif
#endif

    /*
     * You will need to change these values according to your motor, wheels and motor supply voltage.
     */
    RobotCarPWMMotorControl.setValuesForFixedDistanceDriving(DEFAULT_START_SPEED_PWM, DEFAULT_DRIVE_SPEED_PWM, SPEED_PWM_COMPENSATION_RIGHT); // Set compensation
#if ! defined(USE_ENCODER_MOTOR_CONTROL)
    // set factor for converting distance to drive time
    RobotCarPWMMotorControl.setMillimeterPerSecondForFixedDistanceDriving(DEFAULT_MILLIMETER_PER_SECOND);
#endif
    /*
     * Set US servo to forward position
     */
    DistanceServo.attach(PIN_DISTANCE_SERVO);
    DistanceServo.write(90);

    initUSDistancePins(PIN_TRIGGER_OUT, PIN_ECHO_IN);

    /*
     * Do not start immediately with driving
     */
    delay(5000);

    /*
     * Tone feedback for start of driving
     */
    tone(PIN_BUZZER, 2200, 100);
    delay(200);
    tone(PIN_BUZZER, 2200, 100);
}

void loop() {

    unsigned int tCentimeter = getDistanceAndPlayTone();
    unsigned int tSpeedPWM;

    if (tCentimeter >= DISTANCE_MAXIMUM_CENTIMETER) {
        /*
         * Target too far -> drive forward with speed proportional to the gap
         */
        tSpeedPWM = START_SPEED_PWM + (tCentimeter - DISTANCE_MAXIMUM_CENTIMETER) * 2;
        if (tSpeedPWM > MAX_SPEED_PWM_FOLLOWER) {
            tSpeedPWM = MAX_SPEED_PWM_FOLLOWER;
        }
        if (RobotCarPWMMotorControl.getCarDirectionOrBrakeMode() != DIRECTION_FORWARD) {
            Serial.println(F("Go forward"));
        }
        Serial.print(F("SpeedPWM="));
        Serial.println(tSpeedPWM);

        RobotCarPWMMotorControl.setSpeedPWMCompensated(tSpeedPWM, DIRECTION_FORWARD);

    } else if (tCentimeter < DISTANCE_MINIMUM_CENTIMETER) {
        /*
         * Target too close -> drive backwards
         */
        tSpeedPWM = START_SPEED_PWM + (DISTANCE_MINIMUM_CENTIMETER - tCentimeter) * 4;
        if (tSpeedPWM > MAX_SPEED_PWM_FOLLOWER) {
            tSpeedPWM = MAX_SPEED_PWM_FOLLOWER;
        }
        if (RobotCarPWMMotorControl.getCarDirectionOrBrakeMode() != DIRECTION_BACKWARD) {
            Serial.println(F("Go backward"));
        }
        Serial.print(F("SpeedPWM="));
        Serial.println(tSpeedPWM);

        RobotCarPWMMotorControl.setSpeedPWMCompensated(tSpeedPWM, DIRECTION_BACKWARD);

    } else {
        /*
         * Target is in the right distance -> stop once
         */
        if (RobotCarPWMMotorControl.getCarDirectionOrBrakeMode() != MOTOR_RELEASE) {
            Serial.println(F("Stop"));
            RobotCarPWMMotorControl.stop(MOTOR_RELEASE);
        }
    }

    delay(100);
}

unsigned int getDistanceAndPlayTone() {
    /*
     * Get distance
     */
    unsigned int tCentimeter = getUSDistanceAsCentiMeter();
    if (tCentimeter == 0) {
        noTone(PIN_BUZZER);
        Serial.print("Distance timeout ");
        tCentimeter = US_DISTANCE_DEFAULT_TIMEOUT_CENTIMETER;
    } else {
        Serial.print("Distance=");
        Serial.print(tCentimeter);
        Serial.print("cm. ");
        /*
         * Play tone
         */
        int tFrequency = map(tCentimeter, 0, 100, 110, 1760); // 4 octaves per meter
        tone(PIN_BUZZER, tFrequency);
    }
    return tCentimeter;
}
