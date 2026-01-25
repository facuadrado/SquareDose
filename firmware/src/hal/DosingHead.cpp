#include "hal/DosingHead.h"
#include <Preferences.h>

// Default calibration value (mL per second) - will be refined through calibration
// This is the initial estimate for pumps at full speed
static constexpr float DEFAULT_ML_PER_SECOND = 1.0f;  // 1 mL/s initial estimate
static constexpr float CALIBRATION_VOLUME_ML = 4.0f;   // Standard calibration dose

DosingHead::DosingHead(uint8_t headIndex, MotorDriver* motorDriver)
    : headIndex(headIndex), motor(motorDriver), initialized(false) {
    // Initialize calibration data with default values
    calibration = {
        DEFAULT_ML_PER_SECOND,  // mlPerSecond - default estimate
        false,                  // isCalibrated - not yet calibrated
        0                       // lastCalibrationTime
    };
}

bool DosingHead::begin() {
    if (initialized) {
        return true;
    }

    if (motor == nullptr) {
        return false;
    }

    // Load calibration data from NVS (or use defaults if not found)
    loadCalibration();

    initialized = true;
    return true;
}

DosingResult DosingHead::dispense(float volumeMl) {
    DosingResult result = {false, 0, volumeMl, 0.0f, ""};

    // Validate initialization
    if (!initialized) {
        result.errorMessage = "Dosing head not initialized";
        return result;
    }

    // Validate volume
    if (!isValidVolume(volumeMl)) {
        result.errorMessage = "Invalid volume: " + String(volumeMl) + " mL (range: " +
                            String(MIN_VOLUME_ML) + "-" + String(MAX_VOLUME_ML) + " mL)";
        return result;
    }

    // Calculate required runtime
    uint32_t runtimeMs = calculateRuntime(volumeMl);
    if (runtimeMs == 0 || !isValidRuntime(runtimeMs)) {
        result.errorMessage = "Invalid runtime calculated: " + String(runtimeMs) + " ms";
        return result;
    }

    // Start the motor
    if (!motor->startMotor(headIndex, MotorDirection::FORWARD)) {
        result.errorMessage = "Failed to start motor";
        return result;
    }

    // Run for calculated duration (blocking)
    // Note: In production, this will be called from a FreeRTOS task so it won't block the whole system
    unsigned long startTime = millis();
    delay(runtimeMs);

    // Stop the motor
    motor->stopMotor(headIndex);

    // Calculate actual runtime
    result.actualRuntime = millis() - startTime;
    result.estimatedVolume = estimateVolume(result.actualRuntime);
    result.success = true;

    return result;
}

void DosingHead::stopDispensing() {
    if (initialized && motor != nullptr) {
        motor->stopMotor(headIndex);
    }
}

bool DosingHead::calibrate(float actualVolumeMl) {
    if (!initialized) {
        return false;
    }

    // Validate actual volume measurement
    if (actualVolumeMl <= 0.0f) {
        return false;
    }

    // Calculate the duration that was used for the 4mL calibration dose
    // using the current calibration (or default if never calibrated)
    uint32_t durationMs = calculateRuntime(CALIBRATION_VOLUME_ML);

    if (durationMs == 0) {
        return false;
    }

    // Calculate new mL/second rate based on actual measurement
    // Example: System dosed for 4000ms (thought it was 4mL at 1.0 mL/s)
    //          User measured 3.8mL actually dispensed
    //          New rate = 3.8 mL / 4.0 seconds = 0.95 mL/s
    float seconds = durationMs / 1000.0f;
    float newMlPerSecond = actualVolumeMl / seconds;

    // Validate the calculated rate is reasonable
    if (newMlPerSecond <= 0.0f || newMlPerSecond > 100.0f) {  // Max 100 mL/s sanity check
        return false;
    }

    // Update calibration
    calibration.mlPerSecond = newMlPerSecond;
    calibration.isCalibrated = true;
    calibration.lastCalibrationTime = millis();

    // Save to NVS
    return saveCalibration();
}

uint32_t DosingHead::runForDuration(uint32_t durationMs) {
    if (!initialized || !isValidRuntime(durationMs)) {
        return 0;
    }

    // Start the motor
    if (!motor->startMotor(headIndex, MotorDirection::FORWARD)) {
        return 0;
    }

    // Run for specified duration (blocking)
    unsigned long startTime = millis();
    delay(durationMs);

    // Stop the motor
    motor->stopMotor(headIndex);

    // Return actual runtime
    return millis() - startTime;
}

bool DosingHead::isDispensing() const {
    if (!initialized || motor == nullptr) {
        return false;
    }
    return motor->isMotorRunning(headIndex);
}

CalibrationData DosingHead::getCalibrationData() const {
    return calibration;
}

bool DosingHead::isCalibrated() const {
    return calibration.isCalibrated;
}

uint8_t DosingHead::getHeadIndex() const {
    return headIndex;
}

void DosingHead::resetCalibration() {
    calibration.mlPerSecond = DEFAULT_ML_PER_SECOND;
    calibration.isCalibrated = false;
    calibration.lastCalibrationTime = 0;

    saveCalibration();
}

bool DosingHead::loadCalibration() {
    Preferences prefs;
    String ns = getNVSNamespace();

    if (!prefs.begin(ns.c_str(), true)) {  // true = read-only
        // If NVS not available, keep default values
        return false;
    }

    // Load calibration data (use current values as defaults if not found)
    calibration.mlPerSecond = prefs.getFloat("mlPerSec", DEFAULT_ML_PER_SECOND);
    calibration.isCalibrated = prefs.getBool("calibrated", false);
    calibration.lastCalibrationTime = prefs.getULong("lastCalTime", 0);

    prefs.end();
    return true;
}

bool DosingHead::saveCalibration() {
    Preferences prefs;
    String ns = getNVSNamespace();

    if (!prefs.begin(ns.c_str(), false)) {  // false = read-write
        return false;
    }

    // Save calibration data
    prefs.putFloat("mlPerSec", calibration.mlPerSecond);
    prefs.putBool("calibrated", calibration.isCalibrated);
    prefs.putULong("lastCalTime", calibration.lastCalibrationTime);

    prefs.end();
    return true;
}

uint32_t DosingHead::calculateRuntime(float volumeMl) const {
    if (calibration.mlPerSecond <= 0.0f) {
        return 0;
    }

    // Calculate runtime in milliseconds
    // Example: Want 4 mL at 1.0 mL/s = 4 seconds = 4000 ms
    float seconds = volumeMl / calibration.mlPerSecond;
    uint32_t runtimeMs = static_cast<uint32_t>(seconds * 1000.0f);

    return runtimeMs;
}

float DosingHead::estimateVolume(uint32_t runtimeMs) const {
    // Calculate volume in milliliters
    // Example: Ran for 4000ms at 1.0 mL/s = 4.0 seconds * 1.0 mL/s = 4.0 mL
    float seconds = runtimeMs / 1000.0f;
    float volumeMl = calibration.mlPerSecond * seconds;

    return volumeMl;
}

String DosingHead::getNVSNamespace() const {
    return "dosingHead" + String(headIndex);
}

bool DosingHead::isValidVolume(float volumeMl) const {
    return volumeMl >= MIN_VOLUME_ML && volumeMl <= MAX_VOLUME_ML;
}

bool DosingHead::isValidRuntime(uint32_t runtimeMs) const {
    return runtimeMs >= MIN_RUNTIME_MS && runtimeMs <= MAX_RUNTIME_MS;
}
