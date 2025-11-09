#pragma once

#include <Dispatcher.h>
#include <MinuteDisplay.h>
#include <MeshCore.h>



namespace mesh {

/**
 * \brief  The next layer in the basic Dispatcher task, Mesh recognises the particular Payload TYPES,
 *     and provides virtual methods for sub-classes on handling incoming, and also preparing outbound Packets.
*/
class Mesh : public Dispatcher {
  RTCClock* _rtc;
  RNG* _rng;
  MinuteDisplay* _display;

protected:
  DispatcherAction onRecvPacket(Packet* pkt) override;

  Mesh(Radio& radio, MillisecondClock& ms, PacketManager& mgr)
    : Dispatcher(radio, ms, mgr)
  {
  }

public:
  void begin();
  void loop();

  RNG* getRNG() const { return _rng; }
  RTCClock* getRTCClock() const { return _rtc; }

};

}
