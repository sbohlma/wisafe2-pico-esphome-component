#include "catalog.h"

namespace esphome {
namespace wisafe2 {
namespace {

using DetailTable = const char *const[16];

const DetailTable SMOKE_DETAILS = {
    "ADC (Failed)",
    "Temperaturdiode (Failed)",
    "Melder-Batterie (LOW)",
    "Batterie-Impedanz (High)",
    "Kammer (Degraded)",
    "Kalibrierung (Corrupted)",
    "RM (Failed)",
    "RM-Batterie (LOW)",
    nullptr,
    "SPI-Puffer (Overflow)",
    "EEPROM (Failed)",
    "EEPROM-Puffer (Overflow)",
    "Thermoptek (Failed)",
    nullptr,
    nullptr,
    nullptr,
};

const DetailTable HEAT_DETAILS = {
    "ADC (Failed)",
    nullptr,
    "Melder-Batterie (LOW)",
    "Batterie-Impedanz (High)",
    "Thermistor (Degraded)",
    "Kalibrierung (Corrupted)",
    "RM (Failed)",
    "RM-Batterie (LOW)",
    nullptr,
    "SPI-Puffer (Overflow)",
    "EEPROM (Failed)",
    "EEPROM-Puffer (Overflow)",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

const DetailTable CO_DETAILS = {
    "ADC1 (Failed)",
    "Temperaturdiode1 (Failed)",
    "Melder-Batterie (LOW)",
    "Batterie-Impedanz1 (High)",
    "Vref (LOW)",
    "Kalibrierung (Corrupted)",
    "RM (Failed)",
    "RM-Batterie (LOW)",
    "SPI-Puffer (Overflow)",
    "EEPROM (Failed)",
    "EEPROM-Puffer (Overflow)",
    "Sensor offen",
    "Sensor Kurzschluss",
    "Lebensende (EOL)",
    "Temperaturkalibrierung (Failed)",
    "Batterielebensdauer (Ended)",
};

const DetailTable WETA_DETAILS = {
    "ADC (Failed)",
    "Temperatursensor (Failed)",
    "Melder-Batterie (LOW)",
    "Batterie-Impedanz (High)",
    "Vref (LOW)",
    "Kalibrierung (Corrupted)",
    "RM (Failed)",
    "RM-Batterie (LOW)",
    "SPI-Puffer (Overflow)",
    "EEPROM (Failed)",
    "EEPROM-Puffer (Overflow)",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

const DetailTable SVP_DETAILS = {
    "Nachbar fehlt",
    "Pad erkannt (No)",
    "Pad (Failed)",
    "Remote-Fehler",
    "Batterie (Failed)",
    "Funk (Failed)",
    "Remote nicht auf Sockel",
    "Remote-Batterie",
    "Nachbar fehlt",
    "Netz (Failed)",
    "Batterie (Failed)",
    "Pad (Failed)",
    nullptr,
    nullptr,
    nullptr,
    "Netz angeschlossen (No)",
};

const DetailTable LFS_DETAILS = {
    "Nachbar fehlt",
    nullptr,
    nullptr,
    "Remote-Fehler",
    "Batterie (Failed)",
    "Funk (Failed)",
    "Remote nicht auf Sockel",
    "Remote-Batterie",
    "Nachbar fehlt",
    "Netz (Failed)",
    "Batterie (Failed)",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    "Netz angeschlossen (No)",
};

// Netzbetriebene Varianten WSM-1 und WHM-1 belegen zusätzlich die Bits 13–15.
const char *mains_detail_name(int bit) {
  switch (bit) {
    case 13:
      return "Interlink-Störung (Failed)";
    case 14:
      return "Sirene (Failed)";
    case 15:
      return "Sicherung (Failed)";
    default:
      return nullptr;
  }
}

}  // namespace

uint16_t read_model(const uint8_t *bytes) { return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8)); }

void write_model(uint16_t model, uint8_t *bytes) {
  bytes[0] = static_cast<uint8_t>(model & 0xFF);
  bytes[1] = static_cast<uint8_t>(model >> 8);
}

const char *model_name(uint16_t model) {
  switch (model) {
    case MODEL_WSM_1:
      return "WSM-1";
    case MODEL_WST_630:
      return "WST-630";
    case MODEL_W2_CO_10X:
      return "W2-CO-10X";
    case MODEL_W2_SVP_630:
    case MODEL_W2_SVP_630_B:
      return "W2-SVP-630";
    case MODEL_W2_LFS_630:
      return "W2-LFS-630";
    case MODEL_IFG_100:
      return "IFG-100";
    case MODEL_WHT_630:
      return "WHT-630";
    case MODEL_WHM_1:
      return "WHM-1";
    case MODEL_IFG_200:
      return "IFG-200";
    case MODEL_W2_TSL:
      return "W2-TSL";
    case MODEL_ST_630_DE:
      return "ST-630-DE";
    case MODEL_WETA_10X:
      return "WETA-10X";
    case MODEL_FP2620W2:
      return "FP2620W2";
    case MODEL_WST_630N:
      return "WST-630N";
    default:
      return nullptr;
  }
}

const char *class_name(uint8_t device_class) {
  switch (device_class) {
    case 0x01:
      return "WETA-10X";
    case 0x41:
      return "CO";
    case 0x81:
      return "Rauch";
    case 0x82:
      return "Hitze";
    case 0x86:
      return "Netz-Rauch";
    case 0x87:
      return "Netz-Hitze";
    case 0xC4:
      return "W2-TSL";
    case 0xC5:
    case 0xC6:
      return "Schnittstelle";
    case 0xC7:
      return "IFG-100";
    case 0xC8:
      return "IFG-200";
    case 0xC9:
      return "Gateway";
    case 0xFA:
      return "W2-LFS-630";
    case 0xFF:
      return "Blitz/Vibration (alle Alarme)";
    default:
      return nullptr;
  }
}

const char *silence_class_name(uint8_t device_class) {
  switch (device_class & 0xC0) {
    case 0x80:
      return "Feuer";
    case 0x40:
      return "CO";
    case 0xC0:
      return "Feuer und CO";
    default:
      return nullptr;
  }
}

const char *fault_flag_name(uint8_t bit) {
  switch (bit) {
    case 0x01:
      return "kalibriert";
    case 0x02:
      return "fehlerhaft";
    case 0x04:
      return "auf Sockel";
    case 0x08:
      return "Melder-Batterie";
    case 0x10:
      return "Netzausfall";
    case 0x20:
      return "RM-Batterie";
    case 0x40:
      return "Bit 0x40 (schwache Batterie)";
    case 0x80:
      return "Bit 0x80 (unbekannt)";
    default:
      return nullptr;
  }
}

const char *fault_detail_name(uint16_t model, int bit) {
  if (bit < 0 || bit > 15) {
    return nullptr;
  }
  switch (model) {
    case MODEL_WHT_630:
      return HEAT_DETAILS[bit];
    case MODEL_WHM_1:
      return bit >= 13 ? mains_detail_name(bit) : HEAT_DETAILS[bit];
    case MODEL_WSM_1:
      return bit >= 13 ? mains_detail_name(bit) : SMOKE_DETAILS[bit];
    case MODEL_W2_CO_10X:
      return CO_DETAILS[bit];
    case MODEL_WETA_10X:
      return WETA_DETAILS[bit];
    case MODEL_W2_SVP_630:
    case MODEL_W2_SVP_630_B:
      return SVP_DETAILS[bit];
    case MODEL_W2_LFS_630:
      return LFS_DETAILS[bit];
    default:
      return SMOKE_DETAILS[bit];
  }
}

uint16_t battery_low_detail_mask(uint16_t model) {
  switch (model) {
    case MODEL_W2_SVP_630:
    case MODEL_W2_SVP_630_B:
    case MODEL_W2_LFS_630:
      return 0;
    default:
      return 1u << 2;
  }
}

uint16_t rm_battery_low_detail_mask(uint16_t model) {
  switch (model) {
    case MODEL_W2_SVP_630:
    case MODEL_W2_SVP_630_B:
    case MODEL_W2_LFS_630:
      return 0;
    default:
      return 1u << 7;
  }
}

bool is_gateway_id(const uint8_t *id) { return id[0] == 0x01 && id[1] == 0x02 && id[2] == 0x03; }

}  // namespace wisafe2
}  // namespace esphome
