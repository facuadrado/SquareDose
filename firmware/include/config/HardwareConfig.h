#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>

// TB6612 Motor Driver Pin Definitions
// Two TB6612 drivers control 4 motors total, sharing a common STBY pin

// Motor 1 (Pump Head 0)
#define MOTOR1_IN1_PIN    16
#define MOTOR1_IN2_PIN    15
#define MOTOR1_PWM_PIN    7   // Will be used as digital HIGH/LOW for full speed

// Motor 2 (Pump Head 1)
#define MOTOR2_IN1_PIN    6
#define MOTOR2_IN2_PIN    5
#define MOTOR2_PWM_PIN    4   // Will be used as digital HIGH/LOW for full speed

// Motor 3 (Pump Head 2)
#define MOTOR3_IN1_PIN    13
#define MOTOR3_IN2_PIN    12
#define MOTOR3_PWM_PIN    11  // Will be used as digital HIGH/LOW for full speed

// Motor 4 (Pump Head 3)
#define MOTOR4_IN1_PIN    21
#define MOTOR4_IN2_PIN    47
#define MOTOR4_PWM_PIN    48  // Will be used as digital HIGH/LOW for full speed

// Shared standby pin for both TB6612 drivers
#define MOTOR_STBY_PIN    14

// Motor Configuration
#define NUM_MOTORS        4

// Safety Limits
#define MAX_MOTOR_RUN_TIME_MS     300000  // 5 minutes max continuous run
#define EMERGENCY_STOP_TIMEOUT_MS 50      // Time to force motor stop

#endif // HARDWARE_CONFIG_H
