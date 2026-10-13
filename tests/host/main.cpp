#include "check.h"

void run_framing_tests();
void run_message_tests();
void run_radio_tests();
void run_host_identity_tests();
void run_scheduler_tests();
void run_registry_tests();
void run_discovery_tests();
void run_node_view_tests();

int main() {
  run_framing_tests();
  run_message_tests();
  run_radio_tests();
  run_host_identity_tests();
  run_scheduler_tests();
  run_registry_tests();
  run_discovery_tests();
  run_node_view_tests();
  std::printf("%d Prüfungen, %d fehlgeschlagen\n", check::checks(), check::failures());
  return check::failures() == 0 ? 0 : 1;
}
