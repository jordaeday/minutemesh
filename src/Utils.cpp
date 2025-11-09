#include "Utils.h"

#include <AES.h>
#include <CTR.h>
#include <Crypto.h>
#include <SHA256.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace mesh {

uint32_t RNG::nextInt(uint32_t _min, uint32_t _max) {
  uint32_t num;
  random((uint8_t *)&num, sizeof(num));
  return (num % (_max - _min)) + _min;
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t *msg, int msg_len) {
  SHA256 sha;
  sha.update(msg, msg_len);
  sha.finalize(hash, hash_len);
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t *frag1, int frag1_len, const uint8_t *frag2,
                   int frag2_len) {
  SHA256 sha;
  sha.update(frag1, frag1_len);
  sha.update(frag2, frag2_len);
  sha.finalize(hash, hash_len);
}

int Utils::decrypt(const uint8_t *shared_secret, uint8_t *dest, const uint8_t *src, int src_len) {
  AES128 aes;
  uint8_t *dp = dest;
  const uint8_t *sp = src;

  aes.setKey(shared_secret, CIPHER_KEY_SIZE);
  while (sp - src < src_len) {
    aes.decryptBlock(dp, sp);
    dp += 16;
    sp += 16;
  }

  return sp - src; // will always be multiple of 16
}
static const uint8_t minutemeshKey[] = { 0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                       0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0xe5 };

void Utils::makeNonce(uint8_t *nonce, const uint32_t fromNode, const uint64_t packetId) {
  memset(nonce, 0, 16);
  memcpy(nonce, &packetId, sizeof(uint64_t));
  memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t));
}

void Utils::decryptAESCtr(const uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) {
  // For CTR, the implementation is the same
  // TODO extract the nonce using packetId
  uint8_t nonce[16] = { 0 };
  makeNonce(nonce, fromNode, packetId);

  //print nonce
  Serial.print("Nonce: ");
  for (size_t i = 0; i < 16; ++i)
    Serial.printf("%x", nonce[i]);
  Serial.printf("\n");
  uint8_t temp[numBytes];
  memcpy(temp, bytes, numBytes);
  CTR<AES128> ctr;
  ctr.setKey(minutemeshKey, sizeof(minutemeshKey));
  ctr.setIV(nonce, 16);
  ctr.setCounterSize(4);
  ctr.decrypt(bytes, temp, numBytes);
}

int Utils::encrypt(const uint8_t *shared_secret, uint8_t *dest, const uint8_t *src, int src_len) {
  AES128 aes;
  uint8_t *dp = dest;

  aes.setKey(shared_secret, CIPHER_KEY_SIZE);
  while (src_len >= 16) {
    aes.encryptBlock(dp, src);
    dp += 16;
    src += 16;
    src_len -= 16;
  }
  if (src_len > 0) { // remaining partial block
    uint8_t tmp[16];
    memset(tmp, 0, 16);
    memcpy(tmp, src, src_len);
    aes.encryptBlock(dp, tmp);
    dp += 16;
  }
  return dp - dest; // will always be multiple of 16
}

void Utils::encryptAESCtr(uint8_t *_nonce, size_t numBytes, uint8_t *bytes) {
  CTR<AES128> ctr;
  ctr.setKey(minutemeshKey, sizeof(minutemeshKey));
  static uint8_t scratch[256];
  memcpy(scratch, bytes, numBytes);
  memset(scratch + numBytes, 0,
         sizeof(scratch) - numBytes); // Fill rest of buffer with zero (in case cypher looks at it)

  ctr.setIV(_nonce, 16);
  ctr.setCounterSize(4);
  ctr.encrypt(bytes, scratch, numBytes);
}

int Utils::encryptThenMAC(const uint8_t *shared_secret, uint8_t *dest, const uint8_t *src, int src_len) {
  int enc_len = encrypt(shared_secret, dest + CIPHER_MAC_SIZE, src, src_len);

  SHA256 sha;
  sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
  sha.update(dest + CIPHER_MAC_SIZE, enc_len);
  sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, dest, CIPHER_MAC_SIZE);

  return CIPHER_MAC_SIZE + enc_len;
}

int Utils::MACThenDecrypt(const uint8_t *shared_secret, uint8_t *dest, const uint8_t *src, int src_len) {
  if (src_len <= CIPHER_MAC_SIZE) return 0; // invalid src bytes

  uint8_t hmac[CIPHER_MAC_SIZE];
  {
    SHA256 sha;
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, hmac, CIPHER_MAC_SIZE);
  }
  if (memcmp(hmac, src, CIPHER_MAC_SIZE) == 0) {
    return decrypt(shared_secret, dest, src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
  }
  return 0; // invalid HMAC
}

static const char hex_chars[] = "0123456789ABCDEF";

void Utils::toHex(char *dest, const uint8_t *src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    *dest++ = hex_chars[b >> 4];
    *dest++ = hex_chars[b & 0x0F];
    len--;
  }
  *dest = 0;
}

void Utils::printHex(Stream &s, const uint8_t *src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    s.print(hex_chars[b >> 4]);
    s.print(hex_chars[b & 0x0F]);
    len--;
  }
}

static uint8_t hexVal(char c) {
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= '0' && c <= '9') return c - '0';
  return 0;
}

bool Utils::isHexChar(char c) {
  return c == '0' || hexVal(c) > 0;
}

bool Utils::fromHex(uint8_t *dest, int dest_size, const char *src_hex) {
  int len = strlen(src_hex);
  if (len != dest_size * 2) return false; // incorrect length

  uint8_t *dp = dest;
  while (dp - dest < dest_size) {
    char ch = *src_hex++;
    char cl = *src_hex++;
    *dp++ = (hexVal(ch) << 4) | hexVal(cl);
  }
  return true;
}

int Utils::parseTextParts(char *text, const char *parts[], int max_num, char separator) {
  int num = 0;
  char *sp = text;
  while (*sp && num < max_num) {
    parts[num++] = sp;
    while (*sp && *sp != separator)
      sp++;
    if (*sp) {
      *sp++ = 0; // replace the seperator with a null, and skip past it
    }
  }
  // if we hit the maximum parts, make sure LAST entry does NOT have separator
  while (*sp && *sp != separator)
    sp++;
  if (*sp) {
    *sp = 0; // replace the separator with null
  }
  return num;
}

} // namespace mesh
