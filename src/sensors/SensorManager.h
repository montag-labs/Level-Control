#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <string>
#include <Wire.h>
#include <VL53L1X.h>
#include "ConfigStore.h"

class SensorManager {
public:
    static SensorManager& getInstance();

    void init();
    void measure();

    // Gibt die Laser-Version als String zurück (z.B. "VL53L1X", "VL53L0X", "Unbekannt")
    std::string getLaserVersion();

    float getRawDistance() const { return rawDistance_; }
    float getDistanceCm() const { return distanceCm_; }
    float getDistancePercent() const { return distancePercent_; }
    unsigned int getLastPingUs() const { return lastPingUs_; }
    bool hasValidReading() const { return lastValid_; }

    void setBehaelterhoehe(float h) { behaelterhoehe_ = h; }
    void setOffset(float o) { offset_ = o; }
    void setSampleIntervalSeconds(unsigned long seconds);
    void setSensorType(const std::string& type);
    float getBehaelterhoehe() const { return behaelterhoehe_; }
    float getOffset() const { return offset_; }
    unsigned long getSampleIntervalSeconds() const { return sampleIntervalMs_ / 1000UL; }
    unsigned long getSampleIntervalMs() const { return sampleIntervalMs_; }
    std::string getSensorType() const { return sensorType_; }

private:
    SensorManager();
    unsigned int ping();
    void initRcwl1670();
    void initVl53l1x();
    void measureRcwl1670();
    void measureVl53l1x();
    void updateLevel(float measuredDistanceCm);

    float rawDistance_ = 0.0;
    float distanceCm_ = 0.0;
    float distancePercent_ = 0.0;
    unsigned int lastPingUs_ = 0;
    bool lastValid_ = false;
    float behaelterhoehe_ = 95.0;
    float offset_ = 0.0;
    unsigned long sampleIntervalMs_ = 5000UL;
    std::string sensorType_ = "rcwl1670";

    // RCWL-1670 (ultrasonic) pins
    const int triggerPin_ = 4;
    const int echoPin_ = 5;

    // VL53L1X (ToF) pins
    static constexpr int kXshutPin = 2;
    // GPIO1 (interrupt output from VL53L1X) – reserved for future interrupt-driven reads
    static constexpr int kGpio1Pin = 3;
    static constexpr int kSdaPin = 6;
    static constexpr int kSclPin = 7;

    VL53L1X vl53l1x_;
    bool vl53l1xInitialized_ = false;
};

#endif // SENSOR_MANAGER_H