#ifndef DOSING_LOG_H
#define DOSING_LOG_H

#include <Arduino.h>

// Configuration
#define NUM_DOSING_HEADS 4
#define LOG_RETENTION_HOURS 336  // 14 days × 24 hours
#define MAX_LOG_ENTRIES (LOG_RETENTION_HOURS * NUM_DOSING_HEADS)  // 1344 entries

/**
 * @brief Hourly dosing log entry
 *
 * Stores aggregated dosing data per hour, per head
 * Separates scheduled vs ad-hoc doses for analytics
 */
struct HourlyDoseLog {
    uint32_t hourTimestamp;     // Unix epoch rounded to hour (e.g., 1768617600 for 2pm)
    uint8_t head;               // Dosing head index (0-3)
    float scheduledVolume;      // Total mL from scheduled doses this hour
    float adhocVolume;          // Total mL from ad-hoc/manual doses this hour

    // Helper methods
    float getTotalVolume() const { return scheduledVolume + adhocVolume; }
    bool isValid() const;
    String toString() const;
};

/**
 * @brief Daily summary for dashboard
 */
struct DailySummary {
    uint8_t head;
    float dailyTarget;          // From schedule configuration
    float scheduledActual;      // What was actually dosed via schedule today
    float adhocTotal;           // What was manually dosed today
    uint16_t dosesPerDay;       // From schedule configuration
    float perDoseVolume;        // From schedule configuration

    float getTotalToday() const { return scheduledActual + adhocTotal; }
    float getPercentComplete() const {
        return (dailyTarget > 0.0f) ? (scheduledActual / dailyTarget * 100.0f) : 0.0f;
    }
};

#endif // DOSING_LOG_H
