#ifndef MAIL_ENCODING_H
#define MAIL_ENCODING_H

#include <string>

// Reine Kodierungs-Helfer fuer den SMTP-Versand (ohne Netzwerk/Arduino).
namespace Mail {

std::string toBase64(const std::string& input);
// Entfernt Zeilenumbrueche und Klammern, damit Konfigurationswerte keine SMTP-Befehle oder Header einschleusen.
std::string cleanAddress(const std::string& value);
bool isAscii(const std::string& text);
// Header-Text (Betreff): reines ASCII bleibt lesbar, sonst RFC 2047 ("=?UTF-8?B?...?="), UTF-8-sicher aufgeteilt.
std::string encodeHeaderText(const std::string& raw);
// Anzeigename fuer "From:": ASCII wird in Anfuehrungszeichen gesetzt, sonst RFC 2047.
std::string encodeDisplayName(const std::string& name);
// Mail-Text als Base64 in 76-Zeichen-Zeilen (CRLF), Zeilenenden werden vorher auf CRLF normalisiert.
std::string encodeBodyBase64(const std::string& body);

}  // namespace Mail

#endif  // MAIL_ENCODING_H
