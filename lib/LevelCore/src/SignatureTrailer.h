#ifndef SIGNATURE_TRAILER_H
#define SIGNATURE_TRAILER_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Signierte Update-Dateien bestehen aus <Image> + Trailer (81 Byte):
//   Magic "LCSIGV1!" (8) | Laenge der Signatur (1) | Signatur (72, mit Nullen aufgefuellt)
// Beim Empfang weiss man nicht, wo das Image endet: SignatureTrailer haelt deshalb immer die letzten 81 Byte
// zurueck. Alles davor ist sicher Image und kann geschrieben werden.
class SignatureTrailer {
public:
    static constexpr size_t kMagicSize = 8;
    static constexpr size_t kMaxSignatureBytes = 72;
    static constexpr size_t kSize = kMagicSize + 1 + kMaxSignatureBytes;

    void reset();

    // Haengt Daten an. Rueckgabe: Anzahl Bytes am Anfang von pending(), die sicher Image sind (ggf. 0).
    size_t push(const uint8_t* data, size_t len);
    const uint8_t* pending() const { return bytes_.data(); }
    // Entfernt die ersten `count` Bytes (nachdem sie geschrieben wurden).
    void consume(size_t count);

    // Nach dem letzten Datenblock: genau 81 Byte uebrig, Magic stimmt, Signaturlaenge 1..72.
    bool valid() const;
    size_t signatureLength() const;
    const uint8_t* signature() const;

private:
    std::vector<uint8_t> bytes_;
};

#endif  // SIGNATURE_TRAILER_H
