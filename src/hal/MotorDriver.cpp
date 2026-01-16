#include "hal/MotorDriver.h"

MotorDriver::MotorDriver() : initialized(false), standbyEnabled(false) {
    // Initialize motor pin configurations
    motorPins[0] = {MOTOR1_IN1_PIN, MOTOR1_IN2_PIN, MOTOR1_PWM_PIN};
    motorPins[1] = {MOTOR2_IN1_PIN, MOTOR2_IN2_PIN, MOTOR2_PWM_PIN};
    motorPins[2] = {MOTOR3_IN1_PIN, MOTOR3_IN2_PIN, MOTOR3_PWM_PIN};
    motorPins[3] = {MOTOR4_IN1_PIN, MOTOR4_IN2_PIN, MOTOR4_PWM_PIN};

    // Initialize motor states
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        motorStates[i] = {false, MotorDirection::STOP, 0, 0};
    }
}

bool MotorDriver::begin() {
    if (initialized) {
        return true;
    }

    // Configure all motor control pins as outputs
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        pinMode(motorPins[i].in1, OUTPUT);
        pinMode(motorPins[i].in2, OUTPUT);
        pinMode(motorPins[i].pwm, OUTPUT);

        // Initialize all pins to LOW (motor stopped)
        digitalWrite(motorPins[i].in1, LOW);
        digitalWrite(motorPins[i].in2, LOW);
        digitalWrite(motorPins[i].pwm, LOW);
    }

    // Configure standby pin
    pinMode(MOTOR_STBY_PIN, OUTPUT);
    digitalWrite(MOTOR_STBY_PIN, LOW);  // Start in standby (disabled)

    initialized = true;
    return true;
}

bool MotorDriver::startMotor(uint8_t motorIndex, MotorDirection direction) {
    if (!initialized || !isValidMotorIndex(motorIndex)) {
        return false;
    }

    // Only allow FORWARD or REVERSE for starting
    if (direction != MotorDirection::FORWARD && direction != MotorDirection::REVERSE) {
        return false;
    }

    // Enable standby if not already enabled
    if (!standbyEnabled) {
        enableStandby();
    }

    // Set direction and enable motor at full speed
    setMotorPins(motorIndex, direction);

    // Update state
    motorStates[motorIndex].isRunning = true;
    motorStates[motorIndex].direction = direction;
    motorStates[motorIndex].startTime = millis();

    return true;
}

bool MotorDriver::stopMotor(uint8_t motorIndex) {
    if (!initialized || !isValidMotorIndex(motorIndex)) {
        return false;
    }

    // Coast to stop (IN1=LOW, IN2=LOW, PWM=LOW)
    setMotorPins(motorIndex, MotorDirection::STOP);

    // Update state
    if (motorStates[motorIndex].isRunning) {
        motorStates[motorIndex].runDuration = millis() - motorStates[motorIndex].startTime;
        motorStates[motorIndex].isRunning = false;
    }
    motorStates[motorIndex].direction = MotorDirection::STOP;

    return true;
}

bool MotorDriver::brakeMotor(uint8_t motorIndex) {
    if (!initialized || !isValidMotorIndex(motorIndex)) {
        return false;
    }

    // Short brake (IN1=HIGH, IN2=HIGH, PWM=HIGH)
    setMotorPins(motorIndex, MotorDirection::BRAKE);

    // Update state
    if (motorStates[motorIndex].isRunning) {
        motorStates[motorIndex].runDuration = millis() - motorStates[motorIndex].startTime;
        motorStates[motorIndex].isRunning = false;
    }
    motorStates[motorIndex].direction = MotorDirection::BRAKE;

    return true;
}

void MotorDriver::emergencyStopAll() {
    // Immediately stop all motors using brake
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        brakeMotor(i);
    }

    // Disable standby for safety
    disableStandby();
}

bool MotorDriver::isMotorRunning(uint8_t motorIndex) const {
    if (!isValidMotorIndex(motorIndex)) {
        return false;
    }
    return motorStates[motorIndex].isRunning;
}

MotorState MotorDriver::getMotorState(uint8_t motorIndex) const {
    if (!isValidMotorIndex(motorIndex)) {
        return {false, MotorDirection::STOP, 0, 0};
    }
    return motorStates[motorIndex];
}

unsigned long MotorDriver::getMotorRuntime(uint8_t motorIndex) const {
    if (!isValidMotorIndex(motorIndex)) {
        return 0;
    }

    if (motorStates[motorIndex].isRunning) {
        return millis() - motorStates[motorIndex].startTime;
    }

    return motorStates[motorIndex].runDuration;
}

void MotorDriver::enableStandby() {
    if (initialized) {
        digitalWrite(MOTOR_STBY_PIN, HIGH);
        standbyEnabled = true;
    }
}

void MotorDriver::disableStandby() {
    if (initialized) {
        digitalWrite(MOTOR_STBY_PIN, LOW);
        standbyEnabled = false;

        // Mark all motors as stopped since standby disables them
        for (uint8_t i = 0; i < NUM_MOTORS; i++) {
            if (motorStates[i].isRunning) {
                motorStates[i].runDuration = millis() - motorStates[i].startTime;
                motorStates[i].isRunning = false;
            }
        }
    }
}

bool MotorDriver::isStandbyEnabled() const {
    return standbyEnabled;
}

void MotorDriver::setMotorPins(uint8_t motorIndex, MotorDirection direction) {
    if (!isValidMotorIndex(motorIndex)) {
        return;
    }

    const MotorPins& pins = motorPins[motorIndex];

    switch (direction) {
        case MotorDirection::FORWARD:
            // IN1=HIGH, IN2=LOW, PWM=HIGH (full speed forward)
            digitalWrite(pins.in1, HIGH);
            digitalWrite(pins.in2, LOW);
            digitalWrite(pins.pwm, HIGH);
            break;

        case MotorDirection::REVERSE:
            // IN1=LOW, IN2=HIGH, PWM=HIGH (full speed reverse)
            digitalWrite(pins.in1, LOW);
            digitalWrite(pins.in2, HIGH);
            digitalWrite(pins.pwm, HIGH);
            break;

        case MotorDirection::BRAKE:
            // IN1=HIGH, IN2=HIGH, PWM=HIGH (short brake)
            digitalWrite(pins.in1, HIGH);
            digitalWrite(pins.in2, HIGH);
            digitalWrite(pins.pwm, HIGH);
            break;

        case MotorDirection::STOP:
        default:
            // IN1=LOW, IN2=LOW, PWM=LOW (coast to stop)
            digitalWrite(pins.in1, LOW);
            digitalWrite(pins.in2, LOW);
            digitalWrite(pins.pwm, LOW);
            break;
    }
}

bool MotorDriver::isValidMotorIndex(uint8_t motorIndex) const {
    return motorIndex < NUM_MOTORS;
}
