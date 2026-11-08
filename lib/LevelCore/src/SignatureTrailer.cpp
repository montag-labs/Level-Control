#include "SignatureTrailer.h"

#include <cstring>

namespace {
const uint8_t kMagic[SignatureTrailer::kMagicSize] = { 'L', 'C', 'S', 'I', 'G', 'V', '1', '!' };
}

void SignatureTrailer::reset() {
    bytes_.clear();
    bytes_.reserve(kSize + 8192);
}

size_t SignatureTrailer::push(const uint8_t* data, size_t len) {
    bytes_.insert(bytes_.end(), data, data + len);
    return bytes_.size() > kSize ? bytes_.size() - kSize : 0;
}

void SignatureTrailer::consume(size_t count) {
    bytes_.erase(bytes_.begin(), bytes_.begin() + count);
}

bool SignatureTrailer::valid() const {
    if (bytes_.size() != kSize || std::memcmp(bytes_.data(), kMagic, kMagicSize) != 0) {
        return false;
    }
    const size_t length = bytes_[kMagicSize];
    return length >= 1 && length <= kMaxSignatureBytes;
}

size_t SignatureTrailer::signatureLength() const {
    return bytes_.size() == kSize ? bytes_[kMagicSize] : 0;
}

const uint8_t* SignatureTrailer::signature() const {
    return bytes_.data() + kMagicSize + 1;
}
