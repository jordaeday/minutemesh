#include "Mesh.h"
#include <Arduino.h>

namespace mesh {

void Mesh::begin() {
  char _text[16] = "MinuteMesh";
  uint16_t versionWidth = _display->getTextWidth(_text);
  _display->setCursor((_display->width() - versionWidth) / 2, 22);
  _display->print("MinuteMesh");
  Serial.printf("Printed to screen.\n");
  Dispatcher::begin();
}

void Mesh::loop() {
  Dispatcher::loop();
}

DispatcherAction Mesh::onRecvPacket(Packet* pkt) {
    return 0;
}

}
