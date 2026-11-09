#include <Arduino.h>
#include <Wire.h>
#include "ConfigStore.h"
#include "DebugLogger.h"
#include <algorithm>
#include <string>

#include "SensorManager.h"

namespace {
// Median ueber die ersten `count` Messwerte (sortiert das Array in place).
template <typename T>
T medianOf(T* values, int count) {
    std::sort(values, values + count);
    return values[count / 2];
}
}  // namespace

std::string SensorManager::getLaserVersion() {
    const uint8_t ADDR = 0x29;

    // Nicht-invasiv: Wenn der VL53L1X bereits aktiv ist, keine Re-Initialisierung
    // oder XSHUT-Toggles durchführen.
    if (sensorType_ == "vl53l1x" && vl53l1xInitialized_) {
        return "VL53L1X";
    }

    Wire.begin(kSdaPin, kSclPin);
    Wire.setClock(400000); // VL53L1X unterstützt Fast-Mode (400 kHz)
    Wire.setClock(100000);
    delay(5);

    // Erst pruefen, ob ueberhaupt ein Geraet auf 0x29 antwortet.
    Wire.beginTransmission(ADDR);
    if (Wire.endTransmission() != 0) {
        return "Unbekannt";
    }

    auto readReg8 = [&](uint16_t reg, uint8_t &out) -> bool {
        Wire.beginTransmission(ADDR);
        Wire.write((uint8_t)(reg >> 8));
        Wire.write((uint8_t)(reg & 0xFF));
        if (Wire.endTransmission(false) != 0) {
            return false;
        }
        if (Wire.requestFrom((uint8_t)ADDR, (uint8_t)1) != 1) {
            return false;
        }
        out = Wire.read();
        return true;
    };

    // VL53L1X: Model-ID Register 0x010F == 0xEA
    uint8_t id_l1 = 0;
    if (readReg8(0x010F, id_l1) && id_l1 == 0xEA) {
        return "VL53L1X";
    }

    // VL53L0X: IDENTIFICATION_MODEL_ID Register 0x00C0 == 0xEE
    uint8_t id_l0 = 0;
    if (readReg8(0x00C0, id_l0) && id_l0 == 0xEE) {
        return "VL53L0X";
    }

    return "Unbekannt";
}

SensorManager& SensorManager::getInstance() {
    static SensorManager instance;
    return instance;
}

SensorManager::SensorManager() {}

void SensorManager::setSampleIntervalSeconds(unsigned long seconds) {
    seconds = std::min(std::max(seconds, ConfigLimits::kMinSampleIntervalSeconds),
                       ConfigLimits::kMaxSampleIntervalSeconds);
    sampleIntervalMs_ = seconds * 1000UL;
}

void SensorManager::setSensorType(const std::string& type) {
    sensorType_ = (type == "vl53l1x") ? "vl53l1x" : "rcwl1670";
}

void SensorManager::init() {
    // Kalibrierungswerte aus gespeicherter Config laden
    Config cfg;
    if (ConfigStore::getInstance().load(cfg)) {
        behaelterhoehe_ = cfg.behaelterhoehe;
        offset_ = cfg.offset;
        setSampleIntervalSeconds(cfg.sampleIntervalSeconds);
        setSensorType(cfg.sensorType);
        setTofRoiSize(cfg.tofRoiSize);
    }

    DebugLogger::getInstance().log(LogLevel::INFO,
        "SensorManager init (hoehe=" + std::to_string(behaelterhoehe_) +
        "cm, offset=" + std::to_string(offset_) +
        "cm, intervall=" + std::to_string(getSampleIntervalSeconds()) +
        "s, sensor=" + sensorType_ + ")");

    if (sensorType_ == "vl53l1x") {
        initVl53l1x();
    } else {
        initRcwl1670();
    }
}

void SensorManager::initRcwl1670() {
    pinMode(triggerPin_, OUTPUT);
    pinMode(echoPin_, INPUT);
    DebugLogger::getInstance().log(LogLevel::INFO, "RCWL-1670 initialisiert (trigger=" +
        std::to_string(triggerPin_) + ", echo=" + std::to_string(echoPin_) + ")");
}

// I2C-Bus freigeben: Haelt der Sensor nach einem MCU-Reset SDA auf LOW, taktet man SCL bis er loslaesst.
void SensorManager::recoverI2cBus() {
    Wire.end();
    pinMode(kSdaPin, INPUT_PULLUP);
    pinMode(kSclPin, OUTPUT);
    digitalWrite(kSclPin, HIGH);
    delayMicroseconds(10);
    for (int i = 0; i < 9 && digitalRead(kSdaPin) == LOW; i++) {
        digitalWrite(kSclPin, LOW);
        delayMicroseconds(10);
        digitalWrite(kSclPin, HIGH);
        delayMicroseconds(10);
    }
    // STOP-Bedingung: SDA von LOW nach HIGH bei SCL HIGH
    pinMode(kSdaPin, OUTPUT);
    digitalWrite(kSdaPin, LOW);
    delayMicroseconds(10);
    digitalWrite(kSclPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(kSdaPin, HIGH);
    delayMicroseconds(10);
    pinMode(kSdaPin, INPUT_PULLUP);
}

bool SensorManager::tryInitVl53l1x() {
    // XSHUT: Sensor sicher in Hardware-Standby und wieder hochfahren
    pinMode(kXshutPin, OUTPUT);
    digitalWrite(kXshutPin, LOW);
    delay(50);
    digitalWrite(kXshutPin, HIGH);
    delay(50);

    // VL53L1X GPIO1 ist active-low bei data-ready.
    pinMode(kGpio1Pin, INPUT_PULLUP);

    recoverI2cBus();
    Wire.begin(kSdaPin, kSclPin);
    vl53l1x_.setBus(&Wire);
    vl53l1x_.setTimeout(500);
    return vl53l1x_.init();
}

void SensorManager::initVl53l1x() {
    vl53l1xInitialized_ = false;
    consecutiveInvalid_ = 0;
    lastInitAttemptMs_ = millis();

    bool ok = false;
    for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
        ok = tryInitVl53l1x();
        if (!ok) {
            DebugLogger::getInstance().log(LogLevel::WARN,
                "VL53L1X init fehlgeschlagen (Versuch " + std::to_string(attempt) + "/3)");
        }
    }
    if (!ok) {
        DebugLogger::getInstance().log(LogLevel::ERROR, "VL53L1X init fehlgeschlagen, neuer Versuch folgt automatisch");
        return;
    }

    vl53l1x_.setDistanceMode(VL53L1X::Short);
    // Sichtfeld verkleinern (4x4 / 8x8 SPADs), zentriert; 16 = volles Sichtfeld (Werkseinstellung).
    if (tofRoiSize_ >= 4 && tofRoiSize_ < 16) {
        vl53l1x_.setROISize(tofRoiSize_, tofRoiSize_);
        vl53l1x_.setROICenter(199);
    }
    // 100 ms: bessere Wiederholgenauigkeit (Datenblatt DS12385); Füllstand ändert sich langsam.
    vl53l1x_.setMeasurementTimingBudget(100000);
    vl53l1x_.startContinuous(100);
    vl53l1xInitialized_ = true;

    DebugLogger::getInstance().log(LogLevel::INFO, "VL53L1X initialisiert (ROI=" + std::to_string(tofRoiSize_) + ", SDA=" +
        std::to_string(kSdaPin) + ", SCL=" + std::to_string(kSclPin) +
        ", XSHUT=" + std::to_string(kXshutPin) +
        ", GPIO1=" + std::to_string(kGpio1Pin) + ")");
}

unsigned int SensorManager::ping() {
    // Trigger pulse
    digitalWrite(triggerPin_, LOW);
    delayMicroseconds(4);
    digitalWrite(triggerPin_, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin_, LOW);

    // pulseIn ist auf ESP32-C3 meist stabiler als manuelles Busy-Waiting.
    unsigned long duration = pulseIn(echoPin_, HIGH, 30000UL);
    return static_cast<unsigned int>(duration);
}

void SensorManager::measure() {
    if (sensorType_ == "vl53l1x") {
        measureVl53l1x();
    } else {
        measureRcwl1670();
    }
}

// offset_ = Abstand Sensor -> Behälteroberkante (cm)
// Füllstand = (behaelterhoehe_ + offset_) - gemessene_Distanz_cm
void SensorManager::updateLevel(float measuredDistanceCm) {
    distanceCm_ = std::min(std::max((behaelterhoehe_ + offset_) - measuredDistanceCm, 0.0f), behaelterhoehe_);
    distancePercent_ = behaelterhoehe_ > 0.0f
        ? std::min((distanceCm_ / behaelterhoehe_) * 100.0f, 100.0f)
        : 0.0f;
}

void SensorManager::measureRcwl1670() {
    // Take median of 10 measurements
    unsigned int measurements[10];
    int validCount = 0;
    for (int i = 0; i < 10; i++) {
        unsigned int uS = ping();
        if (uS > 0 && uS < 25000) { // Valid range
            measurements[validCount++] = uS;
        }
        delay(30); // Wait between measurements
    }

    if (validCount == 0) {
        lastPingUs_ = 0;
        lastValid_ = false;
        DebugLogger::getInstance().log(LogLevel::WARN, "Sensor timeout - no valid measurements");
        return;
    }

    const unsigned int uS = medianOf(measurements, validCount);

    lastPingUs_ = uS;
    lastValid_ = true;
    rawDistance_ = uS / 1000000.0f * 340.0f / 2.0f; // in m
    updateLevel(rawDistance_ * 100.0f);

    DebugLogger::getInstance().log(LogLevel::DEBUG,
        "raw=" + std::to_string(rawDistance_) + "m cm=" +
        std::to_string(distanceCm_) + " %=" + std::to_string(distancePercent_));
}

void SensorManager::measureVl53l1x() {
    if (!vl53l1xInitialized_) {
        // Init war fehlgeschlagen (z. B. nach Reset): alle 10 s erneut versuchen statt dauerhaft im Fehler zu bleiben.
        if (millis() - lastInitAttemptMs_ >= 10000UL) {
            initVl53l1x();
        }
        if (!vl53l1xInitialized_) {
            lastPingUs_ = 0;
            lastValid_ = false;
            DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X nicht initialisiert");
            return;
        }
    }

    // Take median of 5 measurements
    float readings_mm[5];
    int validCount = 0;

    for (int i = 0; i < 5; i++) {
        const unsigned long waitStartMs = millis();
        while (digitalRead(kGpio1Pin) == HIGH && (millis() - waitStartMs) < 120UL) {
            delay(1);
        }
        if (digitalRead(kGpio1Pin) == HIGH) {
            continue;
        }

        uint16_t dist_mm = vl53l1x_.read();

        // range_status == 0 means valid measurement
        if (!vl53l1x_.timeoutOccurred() &&
            vl53l1x_.ranging_data.range_status == 0 && dist_mm > 0 && dist_mm < 4000) {
            readings_mm[validCount++] = static_cast<float>(dist_mm);
        }
    }

    if (validCount == 0) {
        lastPingUs_ = 0;
        lastValid_ = false;
        DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X timeout - no valid measurements");
        // Mehrere Fehlmessungen in Folge: Sensor haengt vermutlich, komplett neu initialisieren.
        if (++consecutiveInvalid_ >= 3) {
            DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X: Neuinitialisierung nach wiederholten Fehlmessungen");
            initVl53l1x();
        }
        return;
    }
    consecutiveInvalid_ = 0;

    const float dist_mm = medianOf(readings_mm, validCount);

    // Simulate ping_us for dashboard consistency: dist_mm / 1000 [m] / 340 [m/s] * 2 [round-trip] * 1e6 [µs/s]
    lastPingUs_ = static_cast<unsigned int>(dist_mm / 1000.0f / 340.0f * 2.0f * 1000000.0f);
    lastValid_ = true;
    rawDistance_ = dist_mm / 1000.0f; // mm -> m
    updateLevel(dist_mm / 10.0f);     // mm -> cm

    DebugLogger::getInstance().log(LogLevel::DEBUG,
        "VL53L1X raw=" + std::to_string(rawDistance_) + "m cm=" +
        std::to_string(distanceCm_) + " %=" + std::to_string(distancePercent_));
}
