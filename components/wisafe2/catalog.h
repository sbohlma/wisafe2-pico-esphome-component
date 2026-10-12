#pragma once

#include <cstdint>

namespace esphome {
namespace wisafe2 {

// Modell-IDs aus der Protokollanalyse; das Gateway zeigt sie dezimal.
static constexpr uint16_t MODEL_WSM_1 = 743;
static constexpr uint16_t MODEL_WST_630 = 785;
static constexpr uint16_t MODEL_W2_CO_10X = 888;
static constexpr uint16_t MODEL_W2_SVP_630 = 907;
static constexpr uint16_t MODEL_W2_SVP_630_B = 1219;
static constexpr uint16_t MODEL_W2_LFS_630 = 930;
static constexpr uint16_t MODEL_IFG_100 = 1022;
static constexpr uint16_t MODEL_WHT_630 = 1041;
static constexpr uint16_t MODEL_WHM_1 = 1044;
static constexpr uint16_t MODEL_IFG_200 = 1052;
static constexpr uint16_t MODEL_W2_TSL = 1093;
static constexpr uint16_t MODEL_ST_630_DE = 1148;
static constexpr uint16_t MODEL_WETA_10X = 1157;
static constexpr uint16_t MODEL_FP2620W2 = 2285;
static constexpr uint16_t MODEL_WST_630N = 3636;

// Modell-ID aus zwei Bytes in Paket-Reihenfolge: 11 04 ergibt 0x0411 = 1041.
uint16_t read_model(const uint8_t *bytes);
void write_model(uint16_t model, uint8_t *bytes);

// nullptr, wenn unbekannt.
const char *model_name(uint16_t model);
const char *class_name(uint8_t device_class);
// 0x61 nutzt nur Bit 7 (Feuer) und Bit 6 (CO).
const char *silence_class_name(uint8_t device_class);

// Fehlerflags in 0x71, ein Bit je Aufruf. Bit 0x04 bedeutet „auf Sockel“; ist es gelöscht, ist der Melder
// abgenommen.
const char *fault_flag_name(uint8_t bit);
// Fehlertext für ein Bit des 16-Bit-Werts FS in 0x73, abhängig vom Modell. Unbekannte Modelle gelten als
// Rauchmelder. nullptr für Bits ohne Bedeutung.
const char *fault_detail_name(uint16_t model, int bit);

// Gateways benutzen für ihr eigenes RM die Geräte-ID 01 02 03.
bool is_gateway_id(const uint8_t *id);

}  // namespace wisafe2
}  // namespace esphome
