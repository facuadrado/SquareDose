#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <Arduino.h>
#include "config/HardwareConfig.h"

/**
 * @brief Motor rotation direction
 */
enum class MotorDirection {
    FORWARD,   // IN1=HIGH, IN2=LOW
    REVERSE,   // IN1=LOW, IN2=HIGH
    BRAKE,     // IN1=HIGH, IN2=HIGH (short brake)
    STOP       // IN1=LOW, IN2=LOW (coast to stop)
};

/**
 * @brief Motor state tracking
 */
struct MotorState {
    bool isRunning;
    MotorDirection direction;
    unsigned long startTime;
    unsigned long runDuration;
};

/**
 * @brief TB6612 Motor Driver Abstraction
 *
 * Controls 4 DC motors using 2 TB6612 dual H-bridge drivers.
 * Motors run at full speed (digital HIGH) when enabled.
 * Both drivers share a common STBY pin.
 *
 * Thread-safety: Not thread-safe. Use mutexes if calling from multiple FreeRTOS tasks.
 */
class MotorDriver {
public:
    /**
     * @brief Construct a new Motor Driver object
     */
    MotorDriver();

    /**
     * @brief Initialize GPIO pins and enable standby
     * Must be called before using any motor control functions
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Start a specific motor in given direction at full speed
     * @param motorIndex Motor index (0-3)
     * @param direction Motor direction (FORWARD or REVERSE)
     * @return true if motor started successfully
     */
    bool startMotor(uint8_t motorIndex, MotorDirection direction = MotorDirection::FORWARD);

    /**
     * @brief Stop a specific motor (coast to stop)
     * @param motorIndex Motor index (0-3)
     * @return true if motor stopped successfully
     */
    bool stopMotor(uint8_t motorIndex);

    /**
     * @brief Brake a specific motor (short brake for quick stop)
     * @param motorIndex Motor index (0-3)
     * @return true if brake applied successfully
     */
    bool brakeMotor(uint8_t motorIndex);

    /**
     * @brief Emergency stop all motors immediately
     */
    void emergencyStopAll();

    /**
     * @brief Check if a motor is currently running
     * @param motorIndex Motor index (0-3)
     * @return true if motor is running
     */
    bool isMotorRunning(uint8_t motorIndex) const;

    /**
     * @brief Get the current state of a motor
     * @param motorIndex Motor index (0-3)
     * @return MotorState structure
     */
    MotorState getMotorState(uint8_t motorIndex) const;

    /**
     * @brief Get how long a motor has been running (in milliseconds)
     * @param motorIndex Motor index (0-3)
     * @return Runtime in milliseconds, or 0 if motor is not running
     */
    unsigned long getMotorRuntime(uint8_t motorIndex) const;

    /**
     * @brief Enable standby (allows motors to run)
     */
    void enableStandby();

    /**
     * @brief Disable standby (puts all motors in low-power standby mode)
     */
    void disableStandby();

    /**
     * @brief Check if standby is enabled
     * @return true if standby is enabled (motors can run)
     */
    bool isStandbyEnabled() const;

private:
    /**
     * @brief Set motor direction and speed pins
     * @param motorIndex Motor index (0-3)
     * @param direction Motor direction
     */
    void setMotorPins(uint8_t motorIndex, MotorDirection direction);

    /**
     * @brief Validate motor index
     * @param motorIndex Motor index to validate
     * @return true if valid (0-3)
     */
    bool isValidMotorIndex(uint8_t motorIndex) const;

    // Motor pin configurations (indexed by motor number 0-3)
    struct MotorPins {
        uint8_t in1;
        uint8_t in2;
        uint8_t pwm;  // Used as digital HIGH/LOW for full speed
    };

    MotorPins motorPins[NUM_MOTORS];
    MotorState motorStates[NUM_MOTORS];
    bool initialized;
    bool standbyEnabled;
};

#endif // MOTOR_DRIVER_H
