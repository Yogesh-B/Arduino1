#pragma once

#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <functional>

#define WS_MAX_CLIENTS 4

namespace MiniWS {

// ────────────────────────── SHA-1 & Base64 Helpers ──────────────────────────

typedef struct {
  uint32_t state[5];
  uint32_t count[2];
  uint8_t  buffer[64];
} SHA1_CTX;

#define ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define BLK0(i) (block->l[i] = (ROL(block->l[i],24)&0xFF00FF00) | (ROL(block->l[i],8)&0x00FF00FF))
#define BLK(i)  (block->l[i&15] = ROL(block->l[(i+13)&15] ^ block->l[(i+8)&15] ^ block->l[(i+2)&15] ^ block->l[i&15], 1))

#define R0(v,w,x,y,z,i) z += ((w&(x^y))^y)     + BLK0(i) + 0x5A827999 + ROL(v,5); w=ROL(w,30);
#define R1(v,w,x,y,z,i) z += ((w&(x^y))^y)     + BLK(i)  + 0x5A827999 + ROL(v,5); w=ROL(w,30);
#define R2(v,w,x,y,z,i) z += (w^x^y)           + BLK(i)  + 0x6ED9EBA1 + ROL(v,5); w=ROL(w,30);
#define R3(v,w,x,y,z,i) z += (((w|x)&y)|(w&x)) + BLK(i)  + 0x8F1BBCDC + ROL(v,5); w=ROL(w,30);
#define R4(v,w,x,y,z,i) z += (w^x^y)           + BLK(i)  + 0xCA62C1D6 + ROL(v,5); w=ROL(w,30);

typedef union {
  uint8_t  c[64];
  uint32_t l[16];
} CHAR64LONG16;

inline void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
  CHAR64LONG16 block[1];
  memcpy(block, buffer, 64);
  R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
  R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
  R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
  R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
  R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
  R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
  R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
  R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
  R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
  R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
  R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
  R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
  R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
  R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
  R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
  R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
  R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
  R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
  R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
  R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
  state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

inline void sha1_init(SHA1_CTX* context) {
  context->state[0] = 0x67452301;
  context->state[1] = 0xEFCDAB89;
  context->state[2] = 0x98BADCFE;
  context->state[3] = 0x10325476;
  context->state[4] = 0xC3D2E1F0;
  context->count[0] = context->count[1] = 0;
}

inline void sha1_update(SHA1_CTX* context, const uint8_t* data, size_t len) {
  size_t i, j = (context->count[0] >> 3) & 63;
  if ((context->count[0] += (uint32_t)len << 3) < ((uint32_t)len << 3)) context->count[1]++;
  context->count[1] += (uint32_t)(len >> 29);
  if ((j + len) > 63) {
    memcpy(&context->buffer[j], data, (i = 64 - j));
    sha1_transform(context->state, context->buffer);
    for (; i + 63 < len; i += 64) sha1_transform(context->state, &data[i]);
    j = 0;
  } else i = 0;
  memcpy(&context->buffer[j], &data[i], len - i);
}

inline void sha1_final(uint8_t digest[20], SHA1_CTX* context) {
  uint8_t finalcount[8];
  for (int i = 0; i < 8; i++) {
    finalcount[i] = (uint8_t)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
  }
  uint8_t c = 0200;
  sha1_update(context, &c, 1);
  while ((context->count[0] & 504) != 448) {
    c = 0000;
    sha1_update(context, &c, 1);
  }
  sha1_update(context, finalcount, 8);
  for (int i = 0; i < 20; i++) {
    digest[i] = (uint8_t)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
  }
}

inline void base64_encode(const uint8_t* in, size_t in_len, char* out) {
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i = 0, j = 0;
  while (i < in_len) {
    size_t rem = in_len - i;
    uint32_t a = in[i++];
    uint32_t b = (rem > 1) ? in[i++] : 0;
    uint32_t c = (rem > 2) ? in[i++] : 0;
    uint32_t triple = (a << 16) | (b << 8) | c;
    out[j++] = b64[(triple >> 18) & 63];
    out[j++] = b64[(triple >> 12) & 63];
    out[j++] = (rem > 1) ? b64[(triple >> 6) & 63] : '=';
    out[j++] = (rem > 2) ? b64[triple & 63] : '=';
  }
  out[j] = '\0';
}

inline String generateAcceptKey(const String& clientKey) {
  const char* guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  String combined = clientKey + guid;
  SHA1_CTX ctx;
  sha1_init(&ctx);
  sha1_update(&ctx, (const uint8_t*)combined.c_str(), combined.length());
  uint8_t digest[20];
  sha1_final(digest, &ctx);
  char out[36];
  base64_encode(digest, 20, out);
  return String(out);
}

} // namespace MiniWS

// ────────────────────────── WebSocket Client & Server ──────────────────────

class WebSocketServer {
public:
  using MessageCallback = std::function<void(uint8_t clientId, const String &msg)>;
  using EventCallback   = std::function<void(uint8_t clientId)>;

  explicit WebSocketServer(uint16_t port = 81) : server(port), port_(port) {}

  void begin() {
    server.begin();
    server.setNoDelay(true);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
      clients[i].active = false;
      clients[i].handshaked = false;
    }
  }

  void onMessage(MessageCallback cb)     { onMessage_ = cb; }
  void onConnect(EventCallback cb)       { onConnect_ = cb; }
  void onDisconnect(EventCallback cb)    { onDisconnect_ = cb; }

  int activeClients() const {
    int cnt = 0;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
      if (clients[i].active && clients[i].handshaked) cnt++;
    }
    return cnt;
  }

  void broadcast(const String &msg) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
      if (clients[i].active && clients[i].handshaked) {
        sendText(i, msg);
      }
    }
  }

  void sendText(uint8_t clientId, const String &msg) {
    if (clientId >= WS_MAX_CLIENTS || !clients[clientId].active || !clients[clientId].handshaked) return;
    size_t len = msg.length();
    uint8_t header[10];
    size_t hLen = 0;
    header[0] = 0x81; // FIN + Text frame
    if (len <= 125) {
      header[1] = (uint8_t)len;
      hLen = 2;
    } else if (len <= 65535) {
      header[1] = 126;
      header[2] = (uint8_t)((len >> 8) & 0xFF);
      header[3] = (uint8_t)(len & 0xFF);
      hLen = 4;
    } else {
      return; // frames > 64KB not supported
    }
    clients[clientId].client.write(header, hLen);
    clients[clientId].client.write((const uint8_t*)msg.c_str(), len);
  }

  void loop() {
    // 1. Accept new incoming client connection
    if (server.hasClient()) {
      WiFiClient newClient = server.accept();
      newClient.setNoDelay(true);
      int slot = -1;
      for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!clients[i].active) { slot = i; break; }
      }
      if (slot != -1) {
        clients[slot].client = newClient;
        clients[slot].active = true;
        clients[slot].handshaked = false;
        clients[slot].handshakeBuffer = "";
        clients[slot].lastActivity = millis();
      } else {
        newClient.stop(); // No free slots
      }
    }

    // 2. Process all connected clients
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
      if (!clients[i].active) continue;

      if (!clients[i].client.connected()) {
        closeClient(i);
        continue;
      }

      // Handshake phase
      if (!clients[i].handshaked) {
        processHandshake(i);
        continue;
      }

      // Frame reception phase
      while (clients[i].client.available() >= 2) {
        uint8_t b0 = clients[i].client.read();
        uint8_t b1 = clients[i].client.read();

        uint8_t opcode = b0 & 0x0F;
        bool masked    = (b1 & 0x80) != 0;
        size_t payLen  = b1 & 0x7F;

        if (opcode == 0x08) { // Connection Close
          closeClient(i);
          break;
        }

        if (payLen == 126) {
          while (clients[i].client.available() < 2) delayMicroseconds(50);
          uint8_t p1 = clients[i].client.read();
          uint8_t p2 = clients[i].client.read();
          payLen = ((size_t)p1 << 8) | p2;
        } else if (payLen == 127) {
          // Extremely large frames not supported for mouse/keyboard
          closeClient(i);
          break;
        }

        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked) {
          while (clients[i].client.available() < 4) delayMicroseconds(50);
          clients[i].client.read(mask, 4);
        }

        // Read payload
        if (payLen > 512) {
          // Guard against memory exhaustion
          closeClient(i);
          break;
        }

        char payload[513];
        size_t bytesRead = 0;
        unsigned long startWait = millis();
        while (bytesRead < payLen && (millis() - startWait < 100)) {
          int avail = clients[i].client.available();
          if (avail > 0) {
            int toRead = min(avail, (int)(payLen - bytesRead));
            int r = clients[i].client.read((uint8_t*)&payload[bytesRead], toRead);
            if (r > 0) bytesRead += r;
          } else {
            delayMicroseconds(50);
          }
        }

        if (bytesRead == payLen) {
          if (masked) {
            for (size_t k = 0; k < payLen; k++) {
              payload[k] = (char)(payload[k] ^ mask[k % 4]);
            }
          }
          payload[payLen] = '\0';

          if (opcode == 0x09) {
            // Ping -> Pong
            uint8_t pongHeader[2] = {0x8A, 0x00};
            clients[i].client.write(pongHeader, 2);
          } else if (opcode == 0x01) {
            // Text frame -> notify callback
            if (onMessage_) onMessage_(i, String(payload));
          }
        }
        clients[i].lastActivity = millis();
      }
    }
  }

private:
  struct ClientSlot {
    WiFiClient    client;
    bool          active = false;
    bool          handshaked = false;
    String        handshakeBuffer;
    unsigned long lastActivity = 0;
  };

  void processHandshake(int id) {
    while (clients[id].client.available()) {
      char c = clients[id].client.read();
      clients[id].handshakeBuffer += c;
      if (clients[id].handshakeBuffer.endsWith("\r\n\r\n")) {
        // Complete HTTP header received
        String key = "";
        int keyIndex = clients[id].handshakeBuffer.indexOf("Sec-WebSocket-Key: ");
        if (keyIndex != -1) {
          int lineEnd = clients[id].handshakeBuffer.indexOf("\r\n", keyIndex);
          if (lineEnd != -1) {
            key = clients[id].handshakeBuffer.substring(keyIndex + 19, lineEnd);
            key.trim();
          }
        }

        if (key.length() > 0) {
          String acceptKey = MiniWS::generateAcceptKey(key);
          String response = "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";
          clients[id].client.print(response);
          clients[id].client.flush();
          clients[id].handshaked = true;
          clients[id].handshakeBuffer = "";
          if (onConnect_) onConnect_(id);
        } else {
          closeClient(id);
        }
        return;
      }
    }
  }

  void closeClient(int id) {
    if (clients[id].active) {
      clients[id].client.stop();
      clients[id].active = false;
      clients[id].handshaked = false;
      clients[id].handshakeBuffer = "";
      if (onDisconnect_) onDisconnect_(id);
    }
  }

  WiFiServer      server;
  uint16_t        port_;
  ClientSlot      clients[WS_MAX_CLIENTS];
  MessageCallback onMessage_    = nullptr;
  EventCallback   onConnect_    = nullptr;
  EventCallback   onDisconnect_ = nullptr;
};
