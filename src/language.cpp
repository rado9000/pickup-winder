#include "language.h"

#include <stdio.h>
#include <string.h>

static const char* const kPl[] = {
    "   PICKUP WINDER",    // AppTitle
    "   INITIALIZING",    // Initializing
    "   SYSTEM READY",    // SystemReady
    "AUTO",               // Auto
    "MANUAL",             // ManualMode
    "PRESETY",            // Presets
    "USTAWIENIA",         // Settings
    "JEZYK",              // Language
    "DIAGNOSTYKA",        // Diagnostics
    "WSTECZ",             // Back
    "ZWOJE",              // Turns
    "PREDKOSC",           // Rpm
    "RAMP UP",            // RampUp
    "RAMP DOWN",          // RampDown
    "CZAS UP",            // UpTime
    "CZAS DOWN",          // DownTime
    "KIERUNEK",           // Direction
    "START",              // Start
    "S-CURVE",            // SCurve
    "LINEAR",             // Linear
    "CW",                 // Cw
    "CCW",                // Ccw
    "KLIK = START",       // ClickStart
    "KLIK: PONOW",        // ClickAgain
    "HOLD: WSTECZ",       // HoldBack
    "HOLD = PAUZA",       // HoldPause
    "KLIK:GO HOLD:STOP",  // ClickGoHoldStop
    "PAUZA",              // Paused
    "GOTOWE",             // Complete
    "STOP",               // Stopped
    "PRACA",              // Run
    "+ NOWY PRESET",      // NewPreset
    "ZAPISZ",             // Save
    "ZMIEN NAZWE",        // Rename
    "EDYTUJ",             // Edit
    "USUN",               // Delete
    "USUNAC?",            // ConfirmDelete
    "TAK",                // Yes
    "NIE",                // No
    "BLAD SILNIKA",       // MotorError
    "BRAK ODPOWIEDZI",    // NoRs485
    "KLIK = PONOW",       // ClickRetry
    "POLSKI",             // Polski
    "ENGLISH",            // English
    "SILNIK: OK",         // MotorOk
    "RS485: OK",          // Rs485Ok
    "FW",                 // Firmware
    "HOLD: STOP",         // HoldStop
    "ANULUJ",             // Cancel
    "UST:",               // ManualSet
    "ACT:",               // ManualAct
    "ZWOJE:",             // ManualTurns
    "KLIK:KIER HOLD:WYJ", // ManualClickDir
    "KLIK:STOP HOLD:WYJ", // ManualClickStop
    "HOLD: WYJDZ",        // ManualHoldBack
};

static const char* const kEn[] = {
    "   PICKUP WINDER",
    "   INITIALIZING",
    "   SYSTEM READY",
    "AUTO",
    "MANUAL",
    "PRESETS",
    "SETTINGS",
    "LANGUAGE",
    "DIAGNOSTICS",
    "BACK",
    "TURNS",
    "RPM",
    "RAMP UP",
    "RAMP DOWN",
    "UP TIME",
    "DOWN TIME",
    "DIRECTION",
    "START",
    "S-CURVE",
    "LINEAR",
    "CW",
    "CCW",
    "CLICK TO START",
    "CLICK: AGAIN",
    "HOLD: BACK",
    "HOLD = PAUSE",
    "CLICK:GO HOLD:STOP",
    "PAUSED",
    "COMPLETE",
    "STOPPED",
    "RUN",
    "+ NEW PRESET",
    "SAVE",
    "RENAME",
    "EDIT",
    "DELETE",
    "DELETE?",
    "YES",
    "NO",
    "MOTOR ERROR",
    "NO RS485 RESPONSE",
    "CLICK = RETRY",
    "POLSKI",
    "ENGLISH",
    "MOTOR: OK",
    "RS485: OK",
    "FW",
    "HOLD: STOP",
    "CANCEL",
    "SET:",            // ManualSet
    "ACT:",            // ManualAct
    "TURNS:",          // ManualTurns
    "CLICK:DIR HOLD:BCK",  // ManualClickDir
    "CLICK:STOP HOLD:BCK", // ManualClickStop
    "HOLD: BACK",      // ManualHoldBack
};

static_assert(sizeof(kPl) / sizeof(kPl[0]) == static_cast<int>(StrId::COUNT), "PL count");
static_assert(sizeof(kEn) / sizeof(kEn[0]) == static_cast<int>(StrId::COUNT), "EN count");

const char* tr(Language lang, StrId id) {
  const int i = static_cast<int>(id);
  if (i < 0 || i >= static_cast<int>(StrId::COUNT)) return "?";
  return (lang == Language::Polish) ? kPl[i] : kEn[i];
}

void formatRampType(Language lang, RampType t, char* out, int n) {
  snprintf(out, n, "%s", tr(lang, t == RampType::SCurve ? StrId::SCurve : StrId::Linear));
}

void formatDir(Language lang, WindDir d, char* out, int n) {
  snprintf(out, n, "%s", tr(lang, d == WindDir::CW ? StrId::Cw : StrId::Ccw));
}
