#ifndef HARDWARE_VARIANT_H
#define HARDWARE_VARIANT_H

// Hardware-Variante wird beim Build festgelegt (PlatformIO-Umgebung), nicht zur Laufzeit.
//   Laser (Standard):  Umgebung esp32-c3-devkitm-1  -> VL53L1X, SDA=3, SCL=4, INT=2, XSHUT=1 (Platine Y3)
//   Ultraschall:       Umgebung esp32-c3-ultrasonic -> RCWL-1670, Trigger=4, Echo=5
#if defined(LEVEL_HW_ULTRASONIC)
namespace HardwareVariant {
constexpr const char* kSensorType = "rcwl1670";
constexpr bool kIsLaser = false;
}  // namespace HardwareVariant
#else
namespace HardwareVariant {
constexpr const char* kSensorType = "vl53l1x";
constexpr bool kIsLaser = true;
}  // namespace HardwareVariant
#endif

#endif  // HARDWARE_VARIANT_H
