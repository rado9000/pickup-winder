#include "language.h"

#include <stdio.h>
#include <string.h>

// Polish — no diacritics (HD44780 ASCII-compatible)
static const char* const kPl[] = {
    "   PICKUP WINDER",     // AppTitle
    "   INITIALIZING",     // Initializing
    "   SYSTEM READY",     // SystemReady
    "AUTOMATYCZNY",        // Auto          (main menu item)
    "TRYB AUTOMATYCZNY",   // AutoTitle
    "RECZNY",              // ManualMode    (main menu item)
    "TRYB RECZNY",         // ManualTitle
    "PRESETY",             // Presets
    "USTAWIENIA",          // Settings
    "JEZYK",               // Language
    "DIAGNOSTYKA",         // Diagnostics
    "WSTECZ",              // Back
    "ZWOJE",               // Turns
    "PREDKOSC",            // Rpm
    "RAMP UP",             // RampUp
    "RAMP DOWN",           // RampDown
    "CZAS UP",             // UpTime
    "CZAS DOWN",           // DownTime
    "KIERUNEK",            // Direction
    "START",               // Start
    "S-CURVE",             // SCurve
    "LINEAR",              // Linear
    "CW",                  // Cw
    "CCW",                 // Ccw
    "STOP",                // Stop
    "KLIK = START",        // ClickStart
    "KLIK: PONOW",         // ClickAgain
    "HOLD: WSTECZ",        // HoldBack
    "HOLD = PAUZA",        // HoldPause
    "KLIK:GO HOLD:STOP",   // ClickGoHoldStop
    "PAUZA",               // Paused
    "GOTOWE",              // Complete
    "PRZERWANO",           // Stopped
    "PRACA",               // Run
    "+ NOWY PRESET",       // NewPreset
    "ZAPISZ",              // Save
    "ZMIEN NAZWE",         // Rename
    "EDYTUJ",              // Edit
    "USUN",                // Delete
    "USUNAC?",             // ConfirmDelete
    "TAK",                 // Yes
    "NIE",                 // No
    "BLAD SILNIKA",        // MotorError
    "BRAK ODPOWIEDZI",     // NoRs485
    "KLIK = PONOW",        // ClickRetry
    "POLSKI",              // Polski
    "ENGLISH",             // English
    "SILNIK: OK",          // MotorOk
    "RS485: OK",           // Rs485Ok
    "FW",                  // Firmware
    "HOLD: STOP",          // HoldStop
    "ANULUJ",              // Cancel
    "NAZWA PRESETU",       // PresetName
    "OBROT/KLIK DALEJ",    // RotClickNext
    "HOLD=ZAPISZ",         // HoldSave
    "UST:",                // ManualSet
    "AKT:",                // ManualAct
    "ZWOJE:",              // ManualTurns
    "KLIK:STOP HOLD:WYJ",  // ManualClickStop
    "HOLD: WYJDZ",         // ManualHoldBack
    "LIMIT ZWOJOW",        // ManualTurnLimit
    "BEZ LIMITU",          // Unlimited
};

static const char* const kEn[] = {
    "   PICKUP WINDER",
    "   INITIALIZING",
    "   SYSTEM READY",
    "AUTO",
    "AUTO MODE",
    "MANUAL",
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
    "STOP",
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
    "PRESET NAME",
    "ROT/CLICK NEXT",
    "HOLD=SAVE",
    "SET:",
    "ACT:",
    "TURNS:",
    "CLICK:STOP HOLD:BCK",
    "HOLD: BACK",
    "TURN LIMIT",
    "UNLIMITED",
};

static_assert(sizeof(kPl)/sizeof(kPl[0]) == static_cast<int>(StrId::COUNT), "PL count mismatch");
static_assert(sizeof(kEn)/sizeof(kEn[0]) == static_cast<int>(StrId::COUNT), "EN count mismatch");

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
