#include "node_store.h"

#include <LittleFS.h>

namespace esphome {
namespace wisafe2 {
namespace {

const char *const NODES_PATH = "/nodes.bin";
const char *const NODES_TMP_PATH = "/nodes.tmp";
bool mounted = false;

}  // namespace

bool node_store_begin() {
  if (!mounted) {
    mounted = LittleFS.begin();
  }
  return mounted;
}

StoreRead node_store_read(uint8_t *data, size_t len, size_t &read) {
  read = 0;
  if (!mounted) {
    return StoreRead::Error;
  }
  if (!LittleFS.exists(NODES_PATH)) {
    return StoreRead::Missing;
  }
  File file = LittleFS.open(NODES_PATH, "r");
  if (!file) {
    return StoreRead::Error;
  }
  read = file.read(data, len);
  // Eine längere Datei gilt als ungültig; deserialize prüft die Länge.
  if (file.available() > 0) {
    read = len + 1;
  }
  file.close();
  return StoreRead::Ok;
}

bool node_store_write(const uint8_t *data, size_t len) {
  if (!mounted) {
    return false;
  }
  // Erst vollständig in eine zweite Datei schreiben, dann umbenennen: Ein Stromausfall lässt so die alte
  // oder die neue Fassung zurück.
  File file = LittleFS.open(NODES_TMP_PATH, "w");
  if (!file) {
    return false;
  }
  const size_t written = file.write(data, len);
  file.close();
  if (written != len) {
    LittleFS.remove(NODES_TMP_PATH);
    return false;
  }
  return LittleFS.rename(NODES_TMP_PATH, NODES_PATH);
}

bool node_store_remove() {
  if (!mounted) {
    return false;
  }
  LittleFS.remove(NODES_TMP_PATH);
  return !LittleFS.exists(NODES_PATH) || LittleFS.remove(NODES_PATH);
}

}  // namespace wisafe2
}  // namespace esphome
