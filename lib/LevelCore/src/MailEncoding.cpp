#include "MailEncoding.h"

#include <algorithm>
#include <cstdint>

#include "StringUtils.h"

namespace Mail {

std::string toBase64(const std::string& input) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    for (size_t i = 0; i < input.size(); i += 3) {
        const size_t remaining = input.size() - i;
        const uint32_t octetA = static_cast<unsigned char>(input[i]);
        const uint32_t octetB = remaining > 1 ? static_cast<unsigned char>(input[i + 1]) : 0;
        const uint32_t octetC = remaining > 2 ? static_cast<unsigned char>(input[i + 2]) : 0;
        const uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

        output.push_back(alphabet[(triple >> 18) & 0x3F]);
        output.push_back(alphabet[(triple >> 12) & 0x3F]);
        output.push_back(remaining > 1 ? alphabet[(triple >> 6) & 0x3F] : '=');
        output.push_back(remaining > 2 ? alphabet[triple & 0x3F] : '=');
    }

    return output;
}

// Entfernt Zeilenumbrueche und Klammern, damit Konfigurationswerte keine SMTP-Befehle oder Header einschleusen.
std::string cleanAddress(const std::string& value) {
    std::string result;
    for (char ch : value) {
        if (ch != '\r' && ch != '\n' && ch != '<' && ch != '>') {
            result.push_back(ch);
        }
    }
    return strutil::trim(result);
}

bool isAscii(const std::string& text) {
    for (unsigned char ch : text) {
        if (ch >= 0x80) {
            return false;
        }
    }
    return true;
}

// Header-Text (Betreff, Anzeigename): reines ASCII bleibt lesbar, sonst RFC 2047 ("=?UTF-8?B?...?=").
// Zeilenumbrueche werden entfernt; UTF-8-Zeichen werden beim Aufteilen nicht zerschnitten.
std::string encodeHeaderText(const std::string& raw) {
    std::string text;
    for (char ch : raw) {
        if (ch != '\r' && ch != '\n') {
            text.push_back(ch);
        }
    }
    if (isAscii(text)) {
        return text;
    }

    constexpr size_t kMaxBytesPerWord = 45;  // ergibt hoechstens 75 Zeichen je encoded-word
    std::string encoded;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = std::min(pos + kMaxBytesPerWord, text.size());
        while (end < text.size() && end > pos + 1 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end;
        }
        if (!encoded.empty()) {
            encoded += "\r\n ";
        }
        encoded += "=?UTF-8?B?" + toBase64(text.substr(pos, end - pos)) + "?=";
        pos = end;
    }
    return encoded;
}

std::string encodeDisplayName(const std::string& name) {
    const std::string cleaned = encodeHeaderText(name);
    if (!isAscii(name)) {
        return cleaned;
    }
    std::string quoted = "\"";
    for (char ch : cleaned) {
        if (ch != '"' && ch != '\\') {
            quoted.push_back(ch);
        }
    }
    return quoted + "\"";
}

// Mail-Text als Base64 in 76-Zeichen-Zeilen: vermeidet 8-Bit-Probleme und macht Dot-Stuffing unnoetig.
std::string encodeBodyBase64(const std::string& body) {
    std::string text = body;
    strutil::replaceAll(text, "\r\n", "\n");
    strutil::replaceAll(text, "\r", "\n");
    strutil::replaceAll(text, "\n", "\r\n");

    const std::string encoded = toBase64(text);
    std::string wrapped;
    wrapped.reserve(encoded.size() + (encoded.size() / 76 + 1) * 2);
    for (size_t pos = 0; pos < encoded.size(); pos += 76) {
        wrapped += encoded.substr(pos, 76);
        wrapped += "\r\n";
    }
    return wrapped;
}

}  // namespace Mail
