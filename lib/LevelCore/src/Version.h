#ifndef VERSION_H
#define VERSION_H

#include <string>

// Versionsvergleich fuer "MAJOR.MINOR.PATCH". Dev-Staende wie "1.5.A" (Buchstabe statt PATCH) gelten als
// Vorabversion und sind aelter als jedes numerische 1.5.x, damit ein Dev-Geraet jedes Release als Update sieht.
namespace Version {

bool parse(const std::string& version, int& major, int& minor, int& patch);
// <0, 0, >0 wie strcmp. Nicht parsebare Versionen werden als Text verglichen.
int compare(const std::string& left, const std::string& right);
// Entfernt ein fuehrendes "v"/"V" (Git-Tag -> Version).
std::string normalizeTag(std::string tag);

}  // namespace Version

#endif  // VERSION_H
