#ifndef DOSING_HEAD_H
#define DOSING_HEAD_H

#include <Arduino.h>
#include "hal/MotorDriver.h"

/**
 * @brief Calibration data for a dosing head
 */
struct CalibrationData {
    float mlPerSecond;                  // Milliliters dispensed per second at full speed
    bool isCalibrated;                  // Whether this head has been calibrated
    unsigned long lastCalibrationTime;  // Timestamp of last calibration
};

/**
 * @brief Dosing operation result
 */
struct DosingResult {
    bool success;
    uint32_t actualRuntime;   // Actual motor runtime in milliseconds
    float targetVolume;       // Target volume in mL
    float estimatedVolume;    // Estimated volume dispensed based on calibration
    String errorMessage;
};

/**
 * @brief Individual Dosing Head Controller
 *
 * Manages a single pump head including:
 * - Volume-based dosing using calibration data
 * - Calibration procedure and storage
 * - Dose tracking and statistics
 *
 * Thread-safety: Not thread-safe. Use mutexes if calling from multiple FreeRTOS tasks.
 */
class DosingHead {
public:
    /**
     * @brief Construct a new Dosing Head object
     * @param headIndex Index of this dosing head (0-3)
     * @param motorDriver Shared pointer to MotorDriver instance
     */
    DosingHead(uint8_t headIndex, MotorDriver* motorDriver);

    /**
     * @brief Initialize the dosing head
     * Loads calibration data from NVS if available
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Dispense a specific volume of liquid
     * Blocks until dispensing is complete
     * @param volumeMl Volume to dispense in milliliters
     * @return DosingResult with operation details
     */
    DosingResult dispense(float volumeMl);

    /**
     * @brief Stop dispensing immediately
     */
    void stopDispensing();

    /**
     * @brief Calibrate the dosing head
     * System doses 4mL (using current calibration), user measures actual volume
     * Call this with the actual measured volume to update calibration
     * @param actualVolumeMl Actual volume that was dispensed (measured by user)
     * @return true if calibration successful
     */
    bool calibrate(float actualVolumeMl);

    /**
     * @brief Run motor for a specific duration (for manual calibration)
     * Use this to run a calibration dose, then measure the actual volume
     * and call calibrate() with the measured volume
     * @param durationMs How long to run the motor in milliseconds
     * @return Actual runtime in milliseconds
     */
    uint32_t runForDuration(uint32_t durationMs);

    /**
     * @brief Check if this head is currently dispensing
     * @return true if motor is running
     */
    bool isDispensing() const;

    /**
     * @brief Get calibration data for this head
     * @return CalibrationData structure
     */
    CalibrationData getCalibrationData() const;

    /**
     * @brief Check if this head has been calibrated
     * @return true if calibrated
     */
    bool isCalibrated() const;

    /**
     * @brief Get the head index
     * @return Head index (0-3)
     */
    uint8_t getHeadIndex() const;

    /**
     * @brief Reset calibration data back to defaults
     */
    void resetCalibration();

    /**
     * @brief Load calibration data from NVS storage
     * @return true if calibration data loaded successfully
     */
    bool loadCalibration();

    /**
     * @brief Save calibration data to NVS storage
     * @return true if calibration data saved successfully
     */
    bool saveCalibration();

    /**
     * @brief Calculate runtime needed for a given volume
     * @param volumeMl Target volume in milliliters
     * @return Runtime in milliseconds, or 0 if not calibrated
     */
    uint32_t calculateRuntime(float volumeMl) const;

    /**
     * @brief Get estimated volume for a given runtime
     * @param runtimeMs Runtime in milliseconds
     * @return Estimated volume in milliliters
     */
    float estimateVolume(uint32_t runtimeMs) const;

private:
    uint8_t headIndex;
    MotorDriver* motor;
    CalibrationData calibration;
    bool initialized;

    // Volume and runtime limits
    static constexpr float MIN_VOLUME_ML = 0.1;      // Minimum volume: 0.1 mL
    static constexpr float MAX_VOLUME_ML = 1000.0;   // Maximum volume: 1 liter
    static constexpr uint32_t MIN_RUNTIME_MS = 100;  // Minimum runtime: 100ms
    static constexpr uint32_t MAX_RUNTIME_MS = 300000; // Maximum runtime: 5 minutes

    /**
     * @brief Get NVS namespace for dosing head calibration
     * @return NVS namespace string
     */
    String getNVSNamespace() const;

    /**
     * @brief Validate volume parameter
     * @param volumeMl Volume to validate
     * @return true if volume is valid
     */
    bool isValidVolume(float volumeMl) const;

    /**
     * @brief Validate runtime parameter
     * @param runtimeMs Runtime to validate
     * @return true if runtime is valid
     */
    bool isValidRuntime(uint32_t runtimeMs) const;

};

#endif // DOSING_HEAD_H
