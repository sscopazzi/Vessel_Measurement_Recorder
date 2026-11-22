// VMR <-> WTB link over ESP-NOW (VMR / WTB / IWB interface spec, section 3). The same file is in both sketches.
// Frames: an 8-byte header, then one of the bodies below, little-endian, at most 250 bytes in all.
#pragma once
#include <stdint.h>

#define LNK_MAGIC   0xA5
#define LNK_VER     1
#define LNK_MAX     250
#define LNK_DATA    224          // file and firmware bytes per frame

// VMR -> WTB
#define T_BEACON    0x01          // 2 Hz broadcast
#define T_SHIP      0x02          // 1 Hz
#define T_SESSION   0x03          // acked
#define T_CONFIG    0x04          // acked
#define T_FLIST     0x05
#define T_FGET      0x06
#define T_FDEL      0x07
#define T_FWBEGIN   0x08
#define T_FWCHUNK   0x09
#define T_FWEND     0x0A
#define T_IWBCMD    0x0B
#define T_PAIROK    0x0C          // reply to a HELLO during the pairing minute
#define T_FIRE      0x0D          // acked: fire, arm or make safe a Niskin bottle
#define T_NKCFG     0x0E          // acked: Niskin bottles on / off, lowering speed, planned depths
#define T_ACK       0x0F          // either way: the application ACK for an acked frame
// WTB -> VMR
#define T_HELLO     0x21          // asks to pair (the VMR answers PAIROK while "Pair WTB" is open)
#define T_STATUS    0x22          // 1 Hz
#define T_LIVE      0x23          // 1 Hz: the last second's means
#define T_CASTDONE  0x24          // acked
#define T_PROFILE   0x25          // acked
#define T_FLISTR    0x26
#define T_FDATA     0x27
#define T_FWACK     0x28
#define T_IWBREPLY  0x29
#define T_EVENT     0x2A          // acked
#define T_BOTTLES   0x2B          // 1 Hz (and on a change) while Niskin bottles are on
#define T_BOTTLE    0x2C          // acked: one bottle fired, for the fire log and a VMR mark

#define F_ACKED     0x01          // header flag: answer with T_ACK

// session states (STATUS.state)
#define S_OFF        0            // disarmed, or asking the VMR to open the relay (idle timeout, disarm finished)
#define S_STARTING   1
#define S_READY      2
#define S_CAST       3
#define S_PROC       4
#define S_FAULT      5

#pragma pack(push, 1)
struct LnkHdr { uint8_t magic, ver, type, flags; uint16_t seq, len; };

struct BBeacon  { char fw[8]; uint8_t ch, pairOpen; char name[16]; };
struct BShip    { int64_t utcMs; uint8_t utcSrc; float lat, lon, draftDeepM; };              // utcSrc 0 none, 1 phone, 2 GPS; draft NaN = none
struct BSession { uint8_t arm, manual, action; };                                            // action 0 none, 1 start cast, 2 stop cast, 3 tare
struct BConfig  { float startDepthM, startHoldS, endDepthM, endHoldS, binM, soakS; uint16_t idleMin; uint8_t castToDraft; };
struct BFList   { uint8_t dir; uint16_t start; };                                            // dir 0 /sess, 1 /casts
struct BFEntry  { char name[40]; uint32_t size; };
struct BFListR  { uint8_t dir; uint16_t start, total; uint8_t n; BFEntry e[5]; };
struct BFGet    { uint32_t reqId; uint8_t dir; char name[40]; uint32_t off, len; };          // len up to 64 KB, streamed back in FDATA frames
struct BFData   { uint32_t reqId, off, total; uint8_t status; uint8_t d[LNK_DATA]; };      // status 0 ok, 1 no file, 2 SD busy; length from the header
struct BFDel    { uint8_t dir; char name[40]; };
struct BFwBegin { uint8_t target; uint32_t size, crc; char ver[12]; };                     // target 1 WTB, 2 IWB
struct BFwChunk { uint32_t off; uint8_t d[LNK_DATA]; };
struct BFwEnd   { uint32_t crc; };
struct BFwAck   { uint8_t status; uint32_t next; char text[48]; };                           // status 0 going, 1 done, 2 error
struct BText    { char text[200]; };                                                         // IWBCMD, IWBREPLY
struct BAck     { uint16_t seq; uint8_t status; };
struct BHello   { char fw[8]; char name[16]; };
struct BStatus  { char wtbFw[8], iwbFw[8]; uint8_t eth, iwbAlive, state, castOpen, manual, sdOk; uint16_t castNum;
                  float depthM, tareDbar, iwbV, tubeT, tubeRH, dewpt, iwbCpu; uint32_t sdFreeMB; char rawName[40]; };
struct BLive    { int64_t utcSec; float pSea, depth, t, ec, sp, rho; uint8_t nPT, nC; };   // NaN where that second had no sample
struct BCastDone{ uint16_t castNum; int64_t startUtc, endUtc; float lat, lon, maxDepth, meanRho, rangeTop, rangeBottom, draftUsed;
                  uint16_t nBins, nUsed; uint8_t toDraft, reason; char profName[40]; };     // reason 0 ok, 1 shallower than the draft, 2 no density
struct BBin     { float p, depth, t, sp, rho; uint16_t n; };
struct BProfile { uint16_t castNum, first, total; uint8_t n; BBin b[9]; };
struct BEvent   { uint8_t code; char text[100]; };
// Niskin bottles (6): a firing is refused unless the bottle is armed, a cast is open and the capacitor is charged
#define NK_N         6
struct BFire    { uint32_t id; uint8_t bottle, action; };                                    // action: NK_A_*; bottle 0-5 (ignored by the "all" ones)
struct BNkCfg   { uint8_t on, set; float lowerMps, planM[NK_N]; };                           // set: NK_SET_* (what this frame changes); planM NaN = none
struct BBottles { uint8_t on, hw, ready, held, pend, castOpen; float capV, vin, readyInS, rechargeS, minGapM, lowerMps;
                  uint8_t st[NK_N], src[NK_N]; float planM[NK_N], depthM[NK_N], t[NK_N], sp[NK_N], rho[NK_N]; int64_t utcMs[NK_N]; };
struct BBottle  { uint8_t bottle, src; uint16_t castNum; int64_t utcMs; float depthM, pSea, t, sp, rho, lat, lon, capBefore, capAfter; };
#pragma pack(pop)
// BNkCfg.set
#define NK_SET_ON     0x01
#define NK_SET_SPEED  0x02
#define NK_SET_PLAN   0x04
// BFire.action
#define NK_A_FIRE     1
#define NK_A_ARM      2
#define NK_A_SAFE     3           // a fired bottle back to safe clears its record here (the cast's bottle file keeps it)
#define NK_A_ARMALL   4           // the safe ones
#define NK_A_SAFEALL  5           // the armed ones
#define NK_A_RESET    6           // every bottle back to safe, records cleared
// BBottles.st
#define NK_SAFE       0
#define NK_ARMED      1
#define NK_FIRED      2
#define NK_NOANSWER   3           // FIRE sent, the IWB never answered: it may have closed
// BBottles.src / BBottle.src
#define NK_SRC_DASH   1
#define NK_SRC_BUTTON 2
#define NK_SRC_PLAN   3
// BBottles.hw bits
#define NK_HW_BUTTONS 0x01        // WTB: the PCF8574 (5545) answered
#define NK_HW_LEDS    0x02        // WTB: the LED driver (6318 at 0x21) answered
#define NK_HW_SOL     0x04        // IWB: the solenoid driver (6318) answered
#define NK_HW_CAP     0x08        // IWB: capacitor readings arriving

// event codes
#define E_LINK_DOWN  1
#define E_LINK_UP    2
#define E_IWB_RESET  3
#define E_DEWPOINT   4
#define E_IWB_LOWV   5
#define E_SENSOR     6
#define E_IDLE       7
#define E_BOTTLE     8            // Niskin: refused, missed, no answer, plan rejected

// application keys (open in the source on purpose, like the Wi-Fi password)
static const uint8_t LNK_PMK[16] = {'A','s','p','e','c','t','W','a','v','e','-','V','M','R','-','1'};
static const uint8_t LNK_LMK[16] = {'S','e','a','L','a','b','M','O','S','-','W','T','B','-','0','1'};

// CRC-32 (IEEE, the zlib one), incremental: start with 0
static inline uint32_t lnkCrc32(uint32_t crc, const uint8_t* p, uint32_t n) {
  crc = ~crc;
  while (n--) { crc ^= *p++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
  return ~crc;
}
