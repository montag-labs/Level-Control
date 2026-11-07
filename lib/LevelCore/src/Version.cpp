#include "Version.h"

#include <climits>
#include <cctype>

namespace {
bool parseNumber(const std::string& text, size_t& pos, int& value) {
    if (pos >= text.size() || !isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
    }

    int parsed = 0;
    while (pos < text.size() && isdigit(static_cast<unsigned char>(text[pos]))) {
        const int digit = text[pos] - '0';
        // Ueberlauf bei fehlerhaften Versionstexten verhindern.
        if (parsed > (INT_MAX - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
        ++pos;
    }

    value = parsed;
    return true;
}
}  // namespace

namespace Version {

bool parse(const std::string& version, int& major, int& minor, int& patch) {
    major = 0;
    minor = 0;
    patch = 0;

    size_t pos = 0;
    if (!parseNumber(version, pos, major) || pos >= version.size() || version[pos] != '.') {
        return false;
    }
    ++pos;
    if (!parseNumber(version, pos, minor) || pos >= version.size() || version[pos] != '.') {
        return false;
    }
    ++pos;

    // Dev-Stand: genau ein Buchstabe an der PATCH-Stelle.
    if (pos + 1 == version.size() && isalpha(static_cast<unsigned char>(version[pos]))) {
        patch = -1;
        return true;
    }
    return parseNumber(version, pos, patch) && pos == version.size();
}

int compare(const std::string& left, const std::string& right) {
    int leftParts[3];
    int rightParts[3];
    if (!parse(left, leftParts[0], leftParts[1], leftParts[2])
        || !parse(right, rightParts[0], rightParts[1], rightParts[2])) {
        return left.compare(right);
    }

    for (int index = 0; index < 3; ++index) {
        if (leftParts[index] != rightParts[index]) {
            return leftParts[index] < rightParts[index] ? -1 : 1;
        }
    }
    return 0;
}

std::string normalizeTag(std::string tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) {
        tag.erase(0, 1);
    }
    return tag;
}

}  // namespace Version
