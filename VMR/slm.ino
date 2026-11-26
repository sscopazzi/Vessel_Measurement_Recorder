// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// ============================================================================
//   SeaLabMOS: the WTB link, the relay, the CTD, Niskin bottles, the 12 V battery and the laptop port
// ============================================================================
// See VMR_WTB_IWB_Interface_Spec. With SeaLabMOS ticked in the Add-ons card the VMR:
//  - powers the WTB and the IWB through a Latching Mini Relay FeatherWing (SET A4, UNSET pin 9) for a cast
//    session, armed from the Cast session card or the knob, and keeps the session through a reset (RTC memory), not a power-up
//  - talks to the WTB over ESP-NOW on the hotspot's channel (vmr_link.h): beacon and the VMR's time and position out, status,
//    the last second's CTD means, each finished cast's profile in, files and firmware relayed both ways
//  - writes the iwb_* CSV columns, a /casts/ profile file per cast, and opens / closes its own cast marks
//  - relays the Fire control card's Niskin requests (arm, fire, safe, reset, planned depths) to the WTB, which owns the
//    bottles' state, and keeps its last BOTTLES report; a request is refused (409) unless the session is armed
//    and the WTB answers. A fired bottle becomes a location mark
//  - reads the 12 V battery through the INA260 (0x40): % counted from the amps, low = under 20 % or at the
//    chemistry's floor (batt12Low), never while charging
//  - serves the dashboard on the optional laptop port (Wiz5500, http://169.254.1.1)
// Ownership as elsewhere: loop() owns P, I2C, the relay and the link's state; the web task stages requests
// (slmReq*) and reads snapshots. The ESP-NOW receive callback only queues frames.

#define RELAY_SET_PIN      A4
#define RELAY_UNSET_PIN    9
#define RELAY_PULSE_MS     12          // the latching relay needs about 10 ms
#define SLM_DISARM_WAIT_MS 30000UL     // the WTB closes an open cast first
#define WTB_LOST_MS        5000UL
#define SLM_PROF_MAX       1000        // bins kept for the CTD profile card (250 m at 0.25 m)
#define SLM_TRACE_MAX      3600        // the CTD profile card's live trace: 1-s points of the open cast
#define SLM_EV_N           8
#define SLM_RTC_MAGIC      0x534C4D31UL

RTC_NOINIT_ATTR uint32_t slmRtcMagic;  // kept through a reset (watchdog, crash, restart), lost on a power-up
RTC_NOINIT_ATTR uint8_t  slmRtcArmed;

bool     slmArmed = false, slmDisarming = false, lnkOn = false;
uint32_t slmDisarmMs = 0, slmArmMs = 0, pairUntilMs = 0, lastBeaconTx = 0, lastVmrTx = 0;
uint16_t lnkSeq = 0;
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
QueueHandle_t slmRxQ = nullptr, slmFileQ = nullptr, slmFwQ = nullptr;

// what the web task reads (copied under slmMux)
portMUX_TYPE slmMux = portMUX_INITIALIZER_UNLOCKED;
BStatus  slmSt;  uint32_t slmStMs = 0;
BLive    slmLv;  uint32_t slmLvMs = 0;  bool slmLvNew = false;
BCastDone slmCd; bool slmCdValid = false; bool slmCdSaved = true;
BBin*    slmProf = nullptr; uint16_t slmProfGot = 0, slmProfCast = 0; uint8_t* slmProfHave = nullptr;
TracePt* slmTrace = nullptr; uint16_t slmTraceN = 0; uint16_t slmTraceCast = 0;
SlmEv*   slmEv = slmPs<SlmEv>(SLM_EV_N);   // in PSRAM (576 bytes off internal RAM)
uint8_t slmEvN = 0; uint32_t slmEvRev = 0;
char     slmIwbReply[120] = "";
char     slmVmrCastFile[48] = "";      // the last profile file written on this card
bool     ctdCastOpen = false, ctdOpenedMark = false;

// requests from the web task (loop applies them)
volatile int8_t  slmReqArm = -1;        // 1 arm, 0 disarm
volatile uint8_t slmReqAct = 0;         // 1 start, 2 stop, 3 tare
volatile bool    slmReqPair = false, slmReqUnpair = false, slmReqCfg = false;
volatile int8_t  slmReqOn = -1, slmReqManual = -1, slmReqToDraft = -1, slmReqIdle = -1;
volatile int8_t  slmReqBattType = -1, slmReqBattChem = -1, slmReqEthOn = -1, slmReqEthCs = -1, slmReqEthInt = -1;
char             slmReqIwbCmd[120] = ""; volatile bool slmReqIwbCmdOn = false;

// Niskin bottles (Fire Control, the Fire control card). The WTB owns the bottles' state; this keeps its last BOTTLES frame.
#define NK_FIRE_TTL_MS   3000UL        // a Fire not delivered by then is dropped (it must not go off late)
BBottles slmBt; uint32_t slmBtMs = 0;
uint16_t slmBtSeq[NK_N];               // the last T_BOTTLE per bottle (a resend is the same seq)
uint32_t nkFireId = 0;
volatile int8_t slmReqNkOn = -1;
struct NkReq { uint8_t bottle, action; };
NkReq slmReqNk[8]; volatile uint8_t slmReqNkHead = 0, slmReqNkTail = 0;
float slmReqNkPlan[NK_N]; volatile bool slmReqNkPlanOn = false;
volatile float slmReqNkSpd = NAN;
bool nkEnabled() { return P.flags31 & 2; }
bool slmBtFresh() { return slmBtMs && millis() - slmBtMs < 3000 && slmLinkFresh(); }
void slmBtGet(BBottles& b) { portENTER_CRITICAL(&slmMux); b = slmBt; portEXIT_CRITICAL(&slmMux); }   // for the LCD

// acked frames to the WTB (resent until it answers)
#define VPEND_N 8
VPend* vpend = slmPs<VPend>(VPEND_N);      // in PSRAM (2 KB off internal RAM)

bool slmPaired() { for (int i = 0; i < 6; i++) if (P.wtbMac[i]) return true; return false; }
bool slmLinkFresh() { return slmStMs && millis() - slmStMs < WTB_LOST_MS; }
bool slmRelayPin(uint8_t fpinCode) {                 // Feather pin codes taken from the LEDs (relay, laptop port)
  if (P.slmOn && (fpinCode == 5 || fpinCode == FPIN_P9)) return true;      // A4, pin 9
  if (P.ethOn && (fpinCode == P.ethCsPin || (P.ethIntPin && fpinCode == P.ethIntPin))) return true;
  return false;
}

// ----------------------------------------------------------------- ESP-NOW
void lnkRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(LnkHdr) || len > LNK_MAX) return;
  SlmRx f; memcpy(f.mac, info->src_addr, 6); f.len = (uint8_t)len; memcpy(f.d, data, len);
  uint8_t t = ((const LnkHdr*)data)->type;
  QueueHandle_t q = (t == T_FDATA || t == T_FLISTR) ? slmFileQ : t == T_FWACK ? slmFwQ : slmRxQ;
  if (q) xQueueSend(q, &f, 0);
}
void lnkPeer(const uint8_t* mac, bool encrypt) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  esp_now_peer_info_t p = {}; memcpy(p.peer_addr, mac, 6); p.channel = 0; p.ifidx = WIFI_IF_AP;
  p.encrypt = encrypt; if (encrypt) memcpy(p.lmk, LNK_LMK, 16);
  esp_now_add_peer(&p);
}
void lnkStart() {
  if (lnkOn) return;
  // the receive queues (56 frames of 257 bytes, about 14 KB) in PSRAM: internal RAM is what the Wi-Fi needs to let a phone join
  if (!slmRxQ) {
    slmRxQ   = xQueueCreateWithCaps(16, sizeof(SlmRx), MALLOC_CAP_SPIRAM);
    slmFileQ = xQueueCreateWithCaps(24, sizeof(SlmRx), MALLOC_CAP_SPIRAM);
    slmFwQ   = xQueueCreateWithCaps(16, sizeof(SlmRx), MALLOC_CAP_SPIRAM);
    if (!slmRxQ || !slmFileQ || !slmFwQ) {                                   // no PSRAM: internal RAM, as a last resort
      if (!slmRxQ)   slmRxQ   = xQueueCreate(16, sizeof(SlmRx));
      if (!slmFileQ) slmFileQ = xQueueCreate(24, sizeof(SlmRx));
      if (!slmFwQ)   slmFwQ   = xQueueCreate(16, sizeof(SlmRx));
    }
  }
  if (esp_now_init() != ESP_OK) return;
  esp_now_set_pmk(LNK_PMK);
  esp_now_register_recv_cb(lnkRecv);
  lnkPeer(BCAST, false);
  if (slmPaired()) lnkPeer(P.wtbMac, true);
  lnkOn = true;
  Log.println("SeaLabMOS: ESP-NOW on");
}
void lnkStop() { if (!lnkOn) return; esp_now_deinit(); lnkOn = false; }
bool lnkTx(const uint8_t* mac, uint8_t type, const void* body, uint16_t blen, uint8_t flags = 0, uint16_t seq = 0) {
  if (!lnkOn) return false;
  uint8_t b[LNK_MAX]; LnkHdr* h = (LnkHdr*)b;
  h->magic = LNK_MAGIC; h->ver = LNK_VER; h->type = type; h->flags = flags; h->seq = seq ? seq : ++lnkSeq; h->len = blen;
  if (blen) memcpy(b + sizeof(LnkHdr), body, blen);
  return esp_now_send(mac, b, sizeof(LnkHdr) + blen) == ESP_OK;
}
bool wtbTx(uint8_t type, const void* body, uint16_t blen) { return slmPaired() && lnkTx(P.wtbMac, type, body, blen); }
void wtbQueue(uint8_t type, const void* body, uint16_t len) {
  BNkCfg nc; if (type == T_NKCFG) memcpy(&nc, body, sizeof(nc));
  for (int i = 0; i < VPEND_N; i++) if (vpend[i].used && vpend[i].type == type && type != T_ACK && type != T_FIRE) {   // the newest of a kind wins
    if (type == T_NKCFG) {                                         // a Niskin setting not delivered yet is kept with the new one
      const BNkCfg* o = (const BNkCfg*)vpend[i].d;
      if ((o->set & NK_SET_ON) && !(nc.set & NK_SET_ON)) { nc.on = o->on; nc.set |= NK_SET_ON; }
      if ((o->set & NK_SET_SPEED) && !(nc.set & NK_SET_SPEED)) { nc.lowerMps = o->lowerMps; nc.set |= NK_SET_SPEED; }
      if ((o->set & NK_SET_PLAN) && !(nc.set & NK_SET_PLAN)) { memcpy(nc.planM, o->planM, sizeof(nc.planM)); nc.set |= NK_SET_PLAN; }
    }
    vpend[i].used = false;
  }
  if (type == T_NKCFG) body = &nc;
  for (int i = 0; i < VPEND_N; i++) if (!vpend[i].used) {
    VPend& p = vpend[i]; p.used = true; p.type = type; p.seq = ++lnkSeq; p.len = len; memcpy(p.d, body, len); p.lastMs = 0; p.bornMs = millis(); return;
  }
}
void slmSendNk(uint8_t set) {
  BNkCfg c = {}; c.on = nkEnabled() ? 1 : 0; c.set = set; c.lowerMps = NAN; for (int i = 0; i < NK_N; i++) c.planM[i] = NAN;
  wtbQueue(T_NKCFG, &c, sizeof(c));
}
void slmEvent(uint8_t code, const char* text) {
  for (int i = SLM_EV_N - 1; i > 0; i--) slmEv[i] = slmEv[i - 1];
  slmEv[0].unix = clockSynced ? nowUnix() : 0; slmEv[0].code = code; strlcpy(slmEv[0].text, text, sizeof(slmEv[0].text));
  if (slmEvN < SLM_EV_N) slmEvN++;
  slmEvRev++;
  Log.printf("SeaLabMOS: %s\n", text);
}

// ----------------------------------------------------------------- relay and session
void relayPulse(bool on) {
  uint8_t pin = on ? RELAY_SET_PIN : RELAY_UNSET_PIN;
  pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); delay(RELAY_PULSE_MS); digitalWrite(pin, LOW);
}
void slmSendCfg() {
  BConfig c = {0.15f, 2.0f, 0.10f, 5.0f, 0.25f, 30.0f, (uint16_t)((P.slmFlags & 4) ? 20 : 0), (uint8_t)((P.slmFlags & 2) ? 1 : 0)};
  wtbQueue(T_CONFIG, &c, sizeof(c));
}
void slmSendSession(uint8_t action) {
  BSession s = {(uint8_t)(slmArmed && !slmDisarming), (uint8_t)(P.slmFlags & 1), action};
  wtbQueue(T_SESSION, &s, sizeof(s));
}
void slmArm(bool on) {
  if (on) {
    if (slmArmed && !slmDisarming) return;
    relayPulse(true); slmArmed = true; slmDisarming = false; slmArmMs = millis();
    slmRtcMagic = SLM_RTC_MAGIC; slmRtcArmed = 1;
    memset(&slmSt, 0, sizeof(slmSt)); slmStMs = 0;
    slmSendCfg(); slmSendSession(0); slmSendNk(NK_SET_ON);
    slmEvent(0, "Cast session armed: WTB and IWB on");
  } else if (slmArmed && !slmDisarming) {
    slmDisarming = true; slmDisarmMs = millis();
    slmSendSession(0);
    slmEvent(0, "Disarming: the WTB closes any open cast first");
  }
}
void slmRelayOff(const char* why) {
  relayPulse(false); slmArmed = false; slmDisarming = false; slmRtcArmed = 0;
  for (int i = 0; i < VPEND_N; i++) vpend[i].used = false;
  memset(slmBtSeq, 0, sizeof(slmBtSeq)); slmBtMs = 0;
  slmEvent(0, why);
}
// at boot: a power-up opens the relay; a reset with a session armed keeps it going
void slmBoot() {
  if (!P.slmOn) return;
  pinMode(RELAY_SET_PIN, OUTPUT); digitalWrite(RELAY_SET_PIN, LOW);
  pinMode(RELAY_UNSET_PIN, OUTPUT); digitalWrite(RELAY_UNSET_PIN, LOW);
  esp_reset_reason_t r = esp_reset_reason();
  bool clean = r == ESP_RST_POWERON || r == ESP_RST_BROWNOUT || r == ESP_RST_UNKNOWN || slmRtcMagic != SLM_RTC_MAGIC;
  if (clean || !slmRtcArmed) { relayPulse(false); slmArmed = false; slmRtcMagic = SLM_RTC_MAGIC; slmRtcArmed = 0; }
  else { relayPulse(true); slmArmed = true; slmArmMs = millis(); slmSendCfg(); slmSendSession(0); slmSendNk(NK_SET_ON); slmEvent(0, "Restarted with a session armed: carrying on"); }
  if (!slmProf) slmProf = (BBin*)ps_calloc(SLM_PROF_MAX, sizeof(BBin));
  if (!slmProfHave) slmProfHave = (uint8_t*)ps_calloc(SLM_PROF_MAX, 1);
  if (!slmTrace) slmTrace = (TracePt*)ps_calloc(SLM_TRACE_MAX, sizeof(TracePt));
}

// ----------------------------------------------------------------- frames from the WTB (loop)
void slmSaveProfile() {                                // the binned profile to this card: /casts/<stamp>_castNNN.csv
  if (!slmCdValid || slmCdSaved || !sdOK) return;
  const char* base = strrchr(slmCd.profName, '/'); base = base ? base + 1 : slmCd.profName;
  char name[48]; strlcpy(name, base, sizeof(name)); char* p = strstr(name, "_prof"); if (p) memmove(p, p + 5, strlen(p + 5) + 1);
  char path[64]; snprintf(path, sizeof(path), "/casts/%s", name);
  if (!sdLock(200)) return;
  if (!SD.exists("/casts")) SD.mkdir("/casts");
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    char a[24] = "", b[24] = "";
    auto iso = [](char* o, size_t n, int64_t ms) { if (ms <= 0) { o[0] = 0; return; } time_t s = (time_t)(ms / 1000); struct tm t; gmtime_r(&s, &t); strftime(o, n, "%Y-%m-%dT%H:%M:%SZ", &t); };
    iso(a, sizeof(a), slmCd.startUtc); iso(b, sizeof(b), slmCd.endUtc);
    f.printf("# SeaLabMOS cast %u (VMR %s), start %s, end %s, lat %.5f, lon %.5f\n", slmCd.castNum, FW_VERSION, a, b, slmCd.lat, slmCd.lon);
    if (slmCd.reason == 0) f.printf("# mean in-situ density %.4f kg/m3 over %.2f-%.2f m (%u bins)%s\n", slmCd.meanRho, slmCd.rangeTop, slmCd.rangeBottom, slmCd.nUsed, slmCd.toDraft ? ", to the deepest draft + 5 %" : "");
    else f.printf("# no mean density: %s\n", slmCd.reason == 1 ? "the cast didn't reach the deepest draft + 5 %" : "no salinity in the bins");
    f.println("p_dbar,depth_m,t_c,sp,rho_kgm3,n");
    for (uint16_t i = 0; i < slmCd.nBins && i < SLM_PROF_MAX; i++) {
      const BBin& B = slmProf[i]; char t[16] = "", s[16] = "", r[16] = "";
      if (!isnan(B.t)) snprintf(t, sizeof(t), "%.4f", B.t);
      if (!isnan(B.sp)) snprintf(s, sizeof(s), "%.4f", B.sp);
      if (!isnan(B.rho)) snprintf(r, sizeof(r), "%.4f", B.rho);
      f.printf("%.4f,%.3f,%s,%s,%s,%u\n", B.p, B.depth, t, s, r, B.n);
    }
    f.close(); slmCdSaved = true; strlcpy(slmVmrCastFile, name, sizeof(slmVmrCastFile));
  }
  sdUnlock();
}
void slmFrame(const SlmRx& f) {
  const LnkHdr* h = (const LnkHdr*)f.d;
  if (h->magic != LNK_MAGIC || h->ver != LNK_VER || sizeof(LnkHdr) + h->len > f.len) return;
  const uint8_t* b = f.d + sizeof(LnkHdr);
  if (h->type == T_HELLO) {                                       // pairing: only while "Pair WTB" is open
    if (!pairUntilMs || (int32_t)(millis() - pairUntilMs) > 0) return;
    memcpy(P.wtbMac, f.mac, 6); markPersistDirty(true);
    lnkPeer(P.wtbMac, false);
    lnkTx(P.wtbMac, T_PAIROK, nullptr, 0);
    delay(20);
    lnkPeer(P.wtbMac, true);
    pairUntilMs = 0;
    char t[64]; snprintf(t, sizeof(t), "Paired with WTB %02X:%02X:%02X:%02X:%02X:%02X", f.mac[0], f.mac[1], f.mac[2], f.mac[3], f.mac[4], f.mac[5]);
    slmEvent(0, t);
    return;
  }
  if (!slmPaired() || memcmp(f.mac, P.wtbMac, 6)) return;
  if (h->flags & F_ACKED) { BAck a = {h->seq, 0}; wtbTx(T_ACK, &a, sizeof(a)); }
  switch (h->type) {
    case T_ACK: { const BAck* a = (const BAck*)b; for (int i = 0; i < VPEND_N; i++) if (vpend[i].used && vpend[i].seq == a->seq) vpend[i].used = false; } break;
    case T_STATUS: {
      portENTER_CRITICAL(&slmMux); memcpy(&slmSt, b, min<size_t>(h->len, sizeof(slmSt))); slmStMs = millis(); portEXIT_CRITICAL(&slmMux);
      bool open = slmSt.castOpen;
      if (open != ctdCastOpen) {
        ctdCastOpen = open;
        if (open) { slmTraceN = 0; slmTraceCast = slmSt.castNum; if (!castOpenNow) { reqMarkBtn = 2; ctdOpenedMark = true; } }   // the VMR's cast mark opens with it
        else if (castOpenNow && ctdOpenedMark) { reqMarkBtn = 2; ctdOpenedMark = false; }
      }
    } break;
    case T_LIVE: {
      portENTER_CRITICAL(&slmMux); memcpy(&slmLv, b, min<size_t>(h->len, sizeof(slmLv))); slmLvMs = millis(); slmLvNew = true; portEXIT_CRITICAL(&slmMux);
      if (ctdCastOpen && slmTrace && slmTraceN < SLM_TRACE_MAX && !isnan(slmLv.pSea)) slmTrace[slmTraceN++] = {slmLv.pSea, slmLv.t, slmLv.sp, slmLv.rho, slmLv.depth};   // pressure, as the plot's axis (+ density, depth)
    } break;
    case T_CASTDONE: {
      const BCastDone* q = (const BCastDone*)b;
      if (slmCdValid && q->castNum == slmCd.castNum) break;          // a resend
      portENTER_CRITICAL(&slmMux); slmCd = *q; slmCdValid = true; slmCdSaved = false; portEXIT_CRITICAL(&slmMux);
      if (slmProfCast != q->castNum) { slmProfCast = q->castNum; slmProfGot = 0; if (slmProfHave) memset(slmProfHave, 0, SLM_PROF_MAX); }
      char t[64];
      if (q->reason == 0) snprintf(t, sizeof(t), "Cast %u: mean density %.3f kg/m3", q->castNum, q->meanRho);
      else snprintf(t, sizeof(t), "Cast %u: %s", q->castNum, q->reason == 1 ? "didn't reach the draft + 5 %" : "no salinity");
      slmEvent(0, t);
    } break;
    case T_PROFILE: {
      const BProfile* q = (const BProfile*)b;
      if (!slmProf) break;
      if (slmProfCast != q->castNum) { slmProfCast = q->castNum; slmProfGot = 0; memset(slmProfHave, 0, SLM_PROF_MAX); }
      for (uint8_t i = 0; i < q->n && i < 9; i++) {
        uint16_t k = q->first + i; if (k >= SLM_PROF_MAX) break;
        if (!slmProfHave[k]) { slmProfHave[k] = 1; slmProfGot++; }
        slmProf[k] = q->b[i];
      }
    } break;
    case T_EVENT: { const BEvent* q = (const BEvent*)b; char t[64]; strlcpy(t, q->text, sizeof(t)); slmEvent(q->code, t); } break;
    case T_BOTTLES: {
      portENTER_CRITICAL(&slmMux); memcpy(&slmBt, b, min<size_t>(h->len, sizeof(slmBt))); slmBtMs = millis(); portEXIT_CRITICAL(&slmMux);
    } break;
    case T_BOTTLE: {                                               // a bottle fired: the log, and a mark
      if (h->len < sizeof(BBottle)) break;
      const BBottle* q = (const BBottle*)b;
      if (q->bottle >= NK_N || slmBtSeq[q->bottle] == h->seq) break;   // a resend
      slmBtSeq[q->bottle] = h->seq;
      static const char* const SRC[] = {"", "dashboard", "button", "planned depth"};
      const char* src = q->src <= 3 ? SRC[q->src] : "";
      char t[64]; snprintf(t, sizeof(t), "Bottle %u fired at %.2f m (%s)", q->bottle + 1, q->depthM, src);
      slmEvent(E_BOTTLE, t);
      BottleMarkReq r = {}; r.bottle = q->bottle + 1; r.utcMs = q->utcMs; r.lat = q->lat; r.lon = q->lon;
      int k = snprintf(r.note, sizeof(r.note), "Niskin %u fired at %.2f m", q->bottle + 1, q->depthM);
      if (!isnan(q->t) && k < (int)sizeof(r.note)) k += snprintf(r.note + k, sizeof(r.note) - k, ", %.3f C", q->t);
      if (!isnan(q->sp) && k < (int)sizeof(r.note)) k += snprintf(r.note + k, sizeof(r.note) - k, ", S %.3f, rho %.3f", q->sp, q->rho);
      if (k < (int)sizeof(r.note)) snprintf(r.note + k, sizeof(r.note) - k, " (%s)", src);
      portENTER_CRITICAL(&bmqMux);
      if ((uint8_t)(bmqHead - bmqTail) < 8) { bmq[bmqHead & 7] = r; bmqHead = bmqHead + 1; }
      portEXIT_CRITICAL(&bmqMux);
    } break;
    case T_IWBREPLY: { const BText* q = (const BText*)b; portENTER_CRITICAL(&slmMux); strlcpy(slmIwbReply, q->text, sizeof(slmIwbReply)); portEXIT_CRITICAL(&slmMux); } break;
  }
  if (slmCdValid && !slmCdSaved && slmProfCast == slmCd.castNum && slmProfGot >= slmCd.nBins) slmSaveProfile();
}

// ----------------------------------------------------------------- the loop's part
void slmApplyRequests() {
  if (slmReqOn >= 0) {
    bool on = slmReqOn; slmReqOn = -1;
    if (on != (bool)P.slmOn) {
      if (!on && slmArmed) slmRelayOff("SeaLabMOS switched off: relay open");
      P.slmOn = on; markPersistDirty(true);
      ledsApplyPins();
      if (on) slmBoot();
      else lnkStop();
    }
  }
  if (slmReqBattType >= 0) { P.battType = slmReqBattType; slmReqBattType = -1; markPersistDirty(true); lastBattMs = 0; ina.ahUsed = NAN; }
  if (slmReqBattChem >= 0) { P.battChem = slmReqBattChem; slmReqBattChem = -1; markPersistDirty(true); ina.ahUsed = NAN; }
  if (slmReqEthOn >= 0) { P.ethOn = slmReqEthOn; slmReqEthOn = -1; markPersistDirty(true); ledsApplyPins(); }
  if (slmReqEthCs >= 0) { P.ethCsPin = slmReqEthCs; slmReqEthCs = -1; markPersistDirty(true); ledsApplyPins(); }
  if (slmReqEthInt >= 0) { P.ethIntPin = slmReqEthInt; slmReqEthInt = -1; markPersistDirty(true); ledsApplyPins(); }
  if (P.ethIntPin && P.ethIntPin == P.ethCsPin) { P.ethIntPin = 0; markPersistDirty(true); }   // one pin can't be both: polled
  if (slmReqNkOn >= 0) {                                           // Niskin bottles ticked / unticked (the Add-ons card)
    bool on = slmReqNkOn; slmReqNkOn = -1;
    if (on != nkEnabled()) { P.flags31 = (P.flags31 & ~2) | (on ? 2 : 0); markPersistDirty(true); if (P.slmOn && slmArmed) slmSendNk(NK_SET_ON); }
  }
  if (!P.slmOn) return;
  while (slmReqNkTail != slmReqNkHead) {                           // Fire / Arm / Safe from the Fire control card
    NkReq r = slmReqNk[slmReqNkTail & 7]; slmReqNkTail = slmReqNkTail + 1;
    if (!nkEnabled() || !slmArmed) continue;
    if (!nkFireId) nkFireId = esp_random() | 1;                    // the WTB skips a repeated id: don't start where a past one did
    BFire f = {++nkFireId, r.bottle, r.action};
    wtbQueue(T_FIRE, &f, sizeof(f));
  }
  if (slmReqNkPlanOn || !isnan(slmReqNkSpd)) {
    BNkCfg c = {}; c.on = nkEnabled(); c.set = 0; c.lowerMps = NAN; for (int i = 0; i < NK_N; i++) c.planM[i] = NAN;
    if (slmReqNkPlanOn) { memcpy(c.planM, slmReqNkPlan, sizeof(c.planM)); c.set |= NK_SET_PLAN; slmReqNkPlanOn = false; }
    if (!isnan(slmReqNkSpd)) { c.lowerMps = slmReqNkSpd; c.set |= NK_SET_SPEED; slmReqNkSpd = NAN; }
    if (slmArmed) wtbQueue(T_NKCFG, &c, sizeof(c));
  }
  bool cfgChanged = false;
  if (slmReqManual >= 0) { P.slmFlags = (P.slmFlags & ~1) | (slmReqManual ? 1 : 0); slmReqManual = -1; markPersistDirty(true); slmSendSession(0); }
  if (slmReqToDraft >= 0) { P.slmFlags = (P.slmFlags & ~2) | (slmReqToDraft ? 2 : 0); slmReqToDraft = -1; markPersistDirty(true); cfgChanged = true; }
  if (slmReqIdle >= 0) { P.slmFlags = (P.slmFlags & ~4) | (slmReqIdle ? 4 : 0); slmReqIdle = -1; markPersistDirty(true); cfgChanged = true; }
  if (cfgChanged || slmReqCfg) { slmReqCfg = false; if (slmArmed) slmSendCfg(); }
  if (slmReqArm >= 0) { bool a = slmReqArm; slmReqArm = -1; slmArm(a); }
  if (slmReqAct) { uint8_t a = slmReqAct; slmReqAct = 0; if (slmArmed) slmSendSession(a); }
  if (slmReqPair) { slmReqPair = false; pairUntilMs = millis() + 60000UL; if (!pairUntilMs) pairUntilMs = 1; slmEvent(0, "Pairing open for 60 s: power the WTB (arm, or USB)"); }
  if (slmReqUnpair) { slmReqUnpair = false; if (slmPaired() && lnkOn) esp_now_del_peer(P.wtbMac); memset(P.wtbMac, 0, 6); markPersistDirty(true); slmEvent(0, "WTB unpaired"); }
  if (slmReqIwbCmdOn) { BText t = {}; strlcpy(t.text, slmReqIwbCmd, sizeof(t.text)); slmReqIwbCmdOn = false; wtbTx(T_IWBCMD, &t, sizeof(t)); }
}
void slmService(uint32_t now) {
  slmApplyRequests();
  if (!P.slmOn) return;
  // ESP-NOW rides on the hotspot: on while it is, off while it isn't
  bool ap = (WiFi.getMode() & WIFI_AP) && !wifiJob;
  if (ap && !lnkOn) lnkStart(); else if (!ap && lnkOn && !wifiJob) lnkStop();
  if (!lnkOn) return;
  SlmRx f; while (xQueueReceive(slmRxQ, &f, 0) == pdTRUE) slmFrame(f);
  if (now - lastBeaconTx >= 500) {
    lastBeaconTx = now;
    BBeacon bc = {}; strlcpy(bc.fw, FW_VERSION, sizeof(bc.fw)); bc.ch = apChan; bc.pairOpen = pairUntilMs && (int32_t)(now - pairUntilMs) < 0; strlcpy(bc.name, apTag, sizeof(bc.name));
    lnkTx(BCAST, T_BEACON, &bc, sizeof(bc));
    if (pairUntilMs && !bc.pairOpen) { pairUntilMs = 0; slmEvent(0, "Pairing closed"); }
  }
  if (slmPaired() && now - lastVmrTx >= 1000) {
    lastVmrTx = now;
    BVmr s; char src; utcNow(src);
    s.utcMs = clockSynced ? (int64_t)clockMsAt(millis()) : 0; s.utcSrc = !clockSynced ? 0 : src == 'G' ? 2 : 1;
    bool fx = gpsFixValid(); s.lat = fx ? gps.latE7 / 1e7f : NAN; s.lon = fx ? gps.lonE7 / 1e7f : NAN;
    uint16_t dd = max(P.draftFwdCm, max(P.draftAftCm, P.draftMidCm));
    s.draftDeepM = dd ? dd / 100.0f : NAN;
    wtbTx(T_VMR, &s, sizeof(s));
  }
  for (int i = 0; i < VPEND_N; i++) {                           // acked frames: resent every 400 ms until answered
    VPend& p = vpend[i];
    if (!p.used || (p.lastMs && now - p.lastMs < 400)) continue;
    if (p.type == T_FIRE && ((const BFire*)p.d)->action == NK_A_FIRE && now - p.bornMs > NK_FIRE_TTL_MS) {   // never late
      p.used = false;
      char t[64]; snprintf(t, sizeof(t), "Bottle %u not fired: the WTB didn't answer", ((const BFire*)p.d)->bottle + 1); slmEvent(E_BOTTLE, t);
      continue;
    }
    p.lastMs = now ? now : 1;
    if (slmPaired()) lnkTx(P.wtbMac, p.type, p.d, p.len, F_ACKED, p.seq);
  }
  // disarming: wait for the WTB to finish (OFF), up to 30 s, or for it to be gone
  if (slmDisarming) {
    bool done = slmLinkFresh() && slmSt.state == S_OFF;
    if (done || now - slmDisarmMs > SLM_DISARM_WAIT_MS || (!slmLinkFresh() && now - slmDisarmMs > WTB_LOST_MS)) slmRelayOff("Cast session off: relay open");
  } else if (slmArmed && slmLinkFresh() && slmSt.state == S_OFF && now - slmArmMs > 10000) {
    slmRelayOff("The WTB asked to switch off (idle time): relay open");
  }
  if (slmCdValid && !slmCdSaved && slmProfCast == slmCd.castNum && slmProfGot >= slmCd.nBins) slmSaveProfile();
}

// ----------------------------------------------------------------- CSV columns (13 iwb_*), on the row after each second
void slmLogCols(char* o, size_t n) {
  if (!P.slmOn) { strlcpy(o, ",,,,,,,,,,,,", n); return; }
  static const char* const SN[] = {"OFF", "STARTING", "READY", "CAST", "PROC", "FAULT"};
  BLive L; BStatus S; bool fresh;
  portENTER_CRITICAL(&slmMux); L = slmLv; S = slmSt; fresh = slmLvNew; slmLvNew = false; portEXIT_CRITICAL(&slmMux);
  bool st = slmArmed && slmLinkFresh();
  const char* state = !slmArmed ? "OFF" : !st ? "STARTING" : SN[S.state < 6 ? S.state : 5];
  char c[11][14]; for (auto& x : c) x[0] = 0;
  auto fm = [](char* d, float v, int k) { if (!isnan(v)) snprintf(d, 14, "%.*f", k, v); };
  if (fresh && st) {
    fm(c[0], L.sp, 4); fm(c[1], L.t, 4); fm(c[2], L.pSea, 4); fm(c[3], S.iwbCpu, 1); fm(c[4], L.depth, 3); fm(c[5], L.ec, 1); fm(c[6], L.rho, 4);
    fm(c[7], S.tubeT, 2); fm(c[8], S.tubeRH, 1); fm(c[9], S.dewpt, 2); fm(c[10], S.iwbV, 2);
  }
  char cast[8] = ""; if (st && S.castOpen) snprintf(cast, sizeof(cast), "%u", S.castNum);
  snprintf(o, n, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], state, cast);
}

// ----------------------------------------------------------------- the 12 V battery: INA260 (0x40)
InaState ina;
bool inaRead(uint8_t reg, int16_t& v) {
  Wire.beginTransmission(0x40); Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x40, 2) != 2) return false;
  v = (int16_t)((Wire.read() << 8) | Wire.read()); return true;
}
// resting voltage -> % (rough, for a start without a count): LiFePO4 4S, Li-ion 3S, lead-acid
static const float OCV[3][6] = {{12.0f, 12.8f, 13.0f, 13.1f, 13.3f, 13.5f}, {9.9f, 10.8f, 11.1f, 11.4f, 11.9f, 12.5f}, {11.6f, 11.9f, 12.1f, 12.3f, 12.5f, 12.7f}};
static const float OCV_PCT[6] = {0, 10, 25, 50, 80, 100};
static const float CHG_FULL_V[3] = {14.0f, 12.4f, 13.6f};       // at or above while the charge current tapers: full
float ocvPct(float v) {
  const float* t = OCV[P.battChem < 3 ? P.battChem : 0];
  if (v <= t[0]) return 0; if (v >= t[5]) return 100;
  for (int i = 1; i < 6; i++) if (v <= t[i]) return OCV_PCT[i - 1] + (OCV_PCT[i] - OCV_PCT[i - 1]) * (v - t[i - 1]) / (t[i] - t[i - 1]);
  return 100;
}
void serviceBattery12(uint32_t now) {
  int16_t rv, ri;
  if (!inaRead(0x02, rv) || !inaRead(0x01, ri)) { battV = battPct = battRate = NAN; ina.a = NAN; battSrc = BS_UNKNOWN; return; }
  float v = (uint16_t)rv * 0.00125f, a = -ri * 0.00125f;        // positive while charging, negative while draining
  battV = v; ina.a = a;
  float cap = P.battAh10 / 10.0f;
  if (isnan(ina.ahUsed)) ina.ahUsed = cap > 0 ? cap * (1.0f - ocvPct(v) / 100.0f) : 0;
  if (ina.lastMs) ina.ahUsed = max(0.0f, ina.ahUsed - a * (now - ina.lastMs) / 3600000.0f);
  ina.lastMs = now;
  if (a > 0 && v >= CHG_FULL_V[P.battChem < 3 ? P.battChem : 0] && cap > 0 && a < cap * 0.05f) ina.ahUsed = 0;   // tapered: full
  battPct = cap > 0 ? constrain(100.0f * (1.0f - ina.ahUsed / cap), 0.0f, 100.0f) : ocvPct(v);
  ina.aAvg = isnan(ina.aAvg) ? a : ina.aAvg + (a - ina.aAvg) * 5.0f / 600.0f;   // ~10 min (a read every 5 s)
  battRate = cap > 0 ? 100.0f * ina.aAvg / cap : NAN;
  battSrc = ina.aAvg > 0.02f ? BS_USB : BS_BATT;                // "charging" / "on the battery"
  battRateAvg = battRate; if (!battAvgSince) battAvgSince = now ? now : 1;
}
// the System card's estimate from the measured current (the LiPo's: battEst, from the voltage-based %)
uint8_t battEst12(float& h, int& ma, bool& early) {
  h = NAN; ma = 0; early = false;
  if (isnan(battV) || isnan(ina.aAvg)) return BE_NONE;
  float cap = P.battAh10 / 10.0f;
  ma = (int)lroundf(fabsf(ina.aAvg) * 1000);
  if (ina.aAvg > 0.02f) { if (cap > 0 && ina.ahUsed > 0.001f) h = ina.ahUsed / ina.aAvg; return battPct >= 99.5f ? BE_FULL : BE_CHG; }
  if (cap > 0 && ina.aAvg < -0.005f) h = (cap - ina.ahUsed) / -ina.aAvg;
  return BE_BATT;
}
// low under 20 % (the count, or the resting-voltage guess without the Ah), as the LiPo, or at the chemistry's floor
// (about 10 % left under load) whatever the count says; not while charging. (11.8 V for every chemistry before:
// a Li-ion 3S pack is ~75 % full there.)
static const float BATT12_FLOOR_V[3] = {12.0f, 10.8f, 11.9f};   // LiFePO4 4S, Li-ion 3S, lead-acid
float batt12Floor() { return BATT12_FLOOR_V[P.battChem < 3 ? P.battChem : 0]; }
bool batt12Low(bool was) {
  if (P.battType != 1 || isnan(battV) || isnan(battPct)) return false;
  if (!isnan(ina.aAvg) && ina.aAvg > 0.02f) return false;
  return battPct < (was ? BATT_LOW_PCT + 1 : BATT_LOW_PCT) || battV <= batt12Floor() + (was ? 0.1f : 0.0f);
}

// ----------------------------------------------------------------- the laptop port (Wiz5500 breakout, 6348)
bool ethPortUp = false;
void ethPortStart() {
  if (!P.ethOn || !P.ethCsPin || P.ethCsPin > 4) return;
  int cs = LED_GPIO[P.ethCsPin], irq = (P.ethIntPin && P.ethIntPin <= 4) ? LED_GPIO[P.ethIntPin] : -1;
  if (irq < 0) ETH.setPollPeriod(5);
  if (!ETH.begin(ETH_PHY_W5500, 1, cs, irq, -1, SPI)) { Log.println("Laptop port: Wiz5500 not found"); return; }
  // no DHCP server on this core's Ethernet: a link-local address, which a laptop with no DHCP falls back to
  ETH.config(IPAddress(169, 254, 1, 1), IPAddress(0, 0, 0, 0), IPAddress(255, 255, 0, 0));
  ethPortUp = true;
  Log.println("Laptop port: http://169.254.1.1");
}

// ----------------------------------------------------------------- the knob's Cast session position
const char* slmHoldBig() { return slmArmed ? "DISARM" : "ARM CAST"; }
const char* slmHoldDet() { return slmArmed ? (ctdCastOpen ? "closes the open cast" : "WTB + IWB off") : "WTB + IWB on"; }

// ----------------------------------------------------------------- OLED: the CTD screen (while armed)
void drawCtd() {
  static const char* const SN[] = {"OFF", "STARTING", "READY", "IN A CAST", "PROCESSING", "FAULT"};
  BStatus S; BLive L; portENTER_CRITICAL(&slmMux); S = slmSt; L = slmLv; portEXIT_CRITICAL(&slmMux);
  char b[48];
  bool st = slmLinkFresh();
  snprintf(b, sizeof(b), "CTD  %s", !slmArmed ? "OFF" : !st ? "NO WTB" : SN[S.state < 6 ? S.state : 5]);
  printAt(0, 0, b);
  display.drawFastHLine(0, 9, 128, SH110X_WHITE);
  if (!st) { printCentered(28, slmArmed ? "Waiting for the WTB" : "Not armed"); return; }
  bool lv = slmLvMs && millis() - slmLvMs < 3000;
  if (lv && !isnan(L.depth)) { snprintf(b, sizeof(b), "%.1f m", L.depth); display.setTextSize(2); printCentered(13, b); display.setTextSize(1); }
  else printCentered(18, S.iwbAlive ? "--" : "IWB link down");
  char t[12] = "--", s[12] = "--", r[12] = "--";
  if (lv && !isnan(L.t)) snprintf(t, sizeof(t), "%.2fC", L.t);
  if (lv && !isnan(L.sp)) snprintf(s, sizeof(s), "%.2f", L.sp);
  if (lv && !isnan(L.rho)) snprintf(r, sizeof(r), "%.2f", L.rho);
  snprintf(b, sizeof(b), "T %s  S %s", t, s); printAt(0, 33, b);
  snprintf(b, sizeof(b), "rho %s kg/m3", r); printAt(0, 43, b);
  char v[10] = "--"; if (!isnan(S.iwbV)) snprintf(v, sizeof(v), "%.1fV", S.iwbV);
  snprintf(b, sizeof(b), "Cast %u  IWB %s%s", S.castNum, v, S.eth ? "" : " NO ETH"); printAt(0, 55, b);
}
// the Cast screen's CTD line, when a cast comes from the IWB
bool slmCastLine(char* b, size_t n) {
  if (!slmArmed || !ctdCastOpen || !slmLvMs || millis() - slmLvMs > 3000) return false;
  BLive L; portENTER_CRITICAL(&slmMux); L = slmLv; portEXIT_CRITICAL(&slmMux);
  char d[10] = "--", t[10] = "--", s[10] = "--";
  if (!isnan(L.depth)) snprintf(d, sizeof(d), "%.1fm", L.depth);
  if (!isnan(L.t)) snprintf(t, sizeof(t), "%.2fC", L.t);
  if (!isnan(L.sp)) snprintf(s, sizeof(s), "S%.2f", L.sp);
  snprintf(b, n, "%s %s %s", d, t, s);
  return true;
}

// ----------------------------------------------------------------- dashboard JSON
static void jf(String& j, const char* k, float v, int d) { j += '"'; j += k; j += "\":"; if (isnan(v) || isinf(v)) j += "null"; else j += String(v, d); }
// the small part, in /data (header, the Cast session card)
void slmJsonSmall(String& j) {
  j += "\"slm\":{\"on\":"; j += P.slmOn ? "true" : "false";
  j += ",\"nk\":"; nkJson(j);
  if (!P.slmOn) { j += "},"; return; }
  BStatus S; BLive L; portENTER_CRITICAL(&slmMux); S = slmSt; L = slmLv; portEXIT_CRITICAL(&slmMux);
  bool st = slmLinkFresh(), lv = st && slmLvMs && millis() - slmLvMs < 3000;
  j += ",\"armed\":"; j += slmArmed ? "true" : "false";
  j += ",\"dis\":"; j += slmDisarming ? "true" : "false";
  j += ",\"paired\":"; j += slmPaired() ? "true" : "false";
  j += ",\"pairing\":"; j += (pairUntilMs && (int32_t)(millis() - pairUntilMs) < 0) ? "true" : "false";
  j += ",\"wtb\":"; j += st ? "true" : "false";
  j += ",\"state\":"; j += st ? (int)S.state : -1;
  j += ",\"manual\":"; j += (P.slmFlags & 1) ? "true" : "false";
  j += ",\"toDraft\":"; j += (P.slmFlags & 2) ? "true" : "false";
  j += ",\"idle\":"; j += (P.slmFlags & 4) ? "true" : "false";
  if (st) {
    j += ",\"eth\":"; j += S.eth ? "true" : "false"; j += ",\"iwb\":"; j += S.iwbAlive ? "true" : "false";
    j += ",\"cast\":"; j += S.castOpen ? (int)S.castNum : 0; j += ",\"castN\":"; j += (int)S.castNum;
    j += ",\"sd\":"; j += S.sdOk ? "true" : "false"; j += ",\"sdMB\":"; j += (unsigned long)S.sdFreeMB;
    j += ",\"fw\":[\""; j += String(S.wtbFw).substring(0, 7); j += "\",\""; j += String(S.iwbFw).substring(0, 7); j += "\"],";
    jf(j, "v", S.iwbV, 2); j += ','; jf(j, "dew", S.dewpt, 1); j += ','; jf(j, "tubeT", S.tubeT, 1); j += ','; jf(j, "tubeRH", S.tubeRH, 0); j += ',';
    jf(j, "tare", S.tareDbar, 4); j += ','; jf(j, "cpu", S.iwbCpu, 0);
  }
  if (lv) { j += ",\"lv\":{"; jf(j, "p", L.pSea, 3); j += ','; jf(j, "d", L.depth, 2); j += ','; jf(j, "t", L.t, 3); j += ','; jf(j, "ec", L.ec, 0); j += ',';
            jf(j, "sp", L.sp, 3); j += ','; jf(j, "rho", L.rho, 3); j += '}'; }
  j += ",\"evRev\":"; j += (unsigned long)slmEvRev;
  j += ",\"eth12\":{\"on\":"; j += P.ethOn ? "true" : "false"; j += ",\"up\":"; j += ethPortUp ? "true" : "false";
  j += ",\"cs\":"; j += (int)P.ethCsPin; j += ",\"int\":"; j += (int)P.ethIntPin; j += "}";
  j += "},";
}
// the Fire control card (Fire Control). b: per bottle [state, source, planned m, depth m, t, sp, rho, utc s]
void nkJson(String& j) {
  j += "{\"en\":"; j += nkEnabled() ? "true" : "false";
  if (!nkEnabled() || !P.slmOn) { j += '}'; return; }
  bool f = slmBtFresh(); BBottles B; portENTER_CRITICAL(&slmMux); B = slmBt; portEXIT_CRITICAL(&slmMux);
  j += ",\"armed\":"; j += slmArmed ? "true" : "false";
  j += ",\"wtb\":"; j += f ? "true" : "false";
  if (f) {
    j += ",\"on\":"; j += B.on ? "true" : "false"; j += ",\"hw\":"; j += B.hw; j += ",\"rdy\":"; j += B.ready ? "true" : "false";
    j += ",\"held\":"; j += B.held; j += ",\"pend\":"; j += B.pend == 0xFF ? -1 : (int)B.pend; j += ",\"cast\":"; j += B.castOpen ? "true" : "false"; j += ',';
    jf(j, "cap", B.capV, 2); j += ','; jf(j, "vin", B.vin, 2); j += ','; jf(j, "rin", B.readyInS, 2); j += ','; jf(j, "rch", B.rechargeS, 2); j += ',';
    jf(j, "gap", B.minGapM, 2); j += ','; jf(j, "spd", B.lowerMps, 2);
    j += ",\"b\":[";
    for (int i = 0; i < NK_N; i++) {
      if (i) j += ',';
      j += '['; j += B.st[i]; j += ','; j += B.src[i]; j += ',';
      auto fv = [&](float v, int d) { if (isnan(v)) j += "null"; else j += String(v, d); j += ','; };
      fv(B.planM[i], 2); fv(B.depthM[i], 2); fv(B.t[i], 3); fv(B.sp[i], 3); fv(B.rho[i], 3);
      j += B.utcMs[i] > 0 ? String((double)(B.utcMs[i] / 1000), 0) : String("null"); j += ']';
    }
    j += ']';
  }
  j += ",\"ev\":[";                                              // the newest bottle events
  bool any = false;
  for (int i = 0, n = 0; i < slmEvN && n < 4; i++) {
    if (slmEv[i].code != E_BOTTLE) continue;
    if (any) j += ','; any = true; n++;
    j += "{\"u\":"; j += (unsigned long)slmEv[i].unix; j += ",\"t\":\""; for (const char* p = slmEv[i].text; *p; p++) { if (*p == '"' || *p == '\\') j += '\\'; j += *p; } j += "\"}";
  }
  j += "]}";
}
// the rest, for the CTD profile card (GET /slm)
void handleSlm() {
  String j; j.reserve(4096);
  j += '{'; slmJsonSmall(j); j.remove(j.length() - 1);           // (no trailing comma)
  BCastDone C; bool cv; char rep[120];
  portENTER_CRITICAL(&slmMux); C = slmCd; cv = slmCdValid; strlcpy(rep, slmIwbReply, sizeof(rep)); portEXIT_CRITICAL(&slmMux);
  j += ",\"ev\":[";
  for (int i = 0; i < slmEvN; i++) { if (i) j += ','; j += "{\"u\":"; j += (unsigned long)slmEv[i].unix; j += ",\"t\":\""; for (const char* p = slmEv[i].text; *p; p++) { if (*p == '"' || *p == '\\') j += '\\'; j += *p; } j += "\"}"; }
  j += "],\"reply\":\""; for (const char* p = rep; *p; p++) { if (*p == '"' || *p == '\\') j += '\\'; j += *p; } j += '"';
  if (cv) {
    j += ",\"cd\":{\"n\":"; j += C.castNum; j += ",\"start\":"; j += String((double)(C.startUtc / 1000), 0); j += ",\"end\":"; j += String((double)(C.endUtc / 1000), 0); j += ',';
    jf(j, "lat", C.lat, 5); j += ','; jf(j, "lon", C.lon, 5); j += ','; jf(j, "max", C.maxDepth, 2); j += ','; jf(j, "rho", C.meanRho, 4); j += ',';
    jf(j, "top", C.rangeTop, 2); j += ','; jf(j, "bot", C.rangeBottom, 2); j += ','; jf(j, "draft", C.draftUsed, 2);
    j += ",\"bins\":"; j += C.nBins; j += ",\"used\":"; j += C.nUsed; j += ",\"toDraft\":"; j += C.toDraft; j += ",\"reason\":"; j += C.reason;
    const char* pn = strrchr(C.profName, '/'); pn = pn ? pn + 1 : C.profName;
    String raw = pn; raw.replace("_prof", "_raw");
    j += ",\"raw\":\""; j += raw; j += "\",\"vmrFile\":\""; j += slmVmrCastFile; j += "\",\"got\":"; j += slmProfGot; j += '}';
    if (slmProf && slmProfCast == C.castNum) {
      j += ",\"prof\":[";
      for (uint16_t i = 0; i < C.nBins && i < SLM_PROF_MAX; i++) {
        if (!slmProfHave[i]) continue;
        const BBin& B = slmProf[i];
        if (j[j.length() - 1] != '[') j += ',';
        j += '['; j += String(B.p, 3); j += ','; j += String(B.depth, 3); j += ',';
        if (isnan(B.t)) j += "null"; else j += String(B.t, 4); j += ',';
        if (isnan(B.sp)) j += "null"; else j += String(B.sp, 4); j += ',';
        if (isnan(B.rho)) j += "null"; else j += String(B.rho, 4); j += ','; j += B.n; j += ']';
      }
      j += ']';
    }
  }
  if (ctdCastOpen && slmTrace) {
    j += ",\"trace\":[";
    uint16_t step = slmTraceN > 600 ? (slmTraceN + 599) / 600 : 1;   // at most ~600 points a poll
    for (uint16_t i = 0; i < slmTraceN; i += step) {
      if (i) j += ',';
      j += '['; j += String(slmTrace[i].d, 2); j += ',';
      if (isnan(slmTrace[i].t)) j += "null"; else j += String(slmTrace[i].t, 3); j += ',';
      if (isnan(slmTrace[i].sp)) j += "null"; else j += String(slmTrace[i].sp, 3); j += ',';
      if (isnan(slmTrace[i].rho)) j += "null"; else j += String(slmTrace[i].rho, 3); j += ',';
      if (isnan(slmTrace[i].z)) j += "null"; else j += String(slmTrace[i].z, 2); j += ']';
    }
    j += ']';
  }
  j += ",\"fwr\":{\"st\":"; j += (int)fwr.st; j += ",\"pct\":"; j += fwr.size ? (int)(100ULL * fwr.acked / fwr.size) : 0; j += ",\"t\":\""; j += fwr.text; j += "\"}";
  j += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}
void handleSlmSet() {
  auto has = [](const char* k) { return server.hasArg(k); };
  auto num = [](const char* k) { return (int)server.arg(k).toInt(); };
  if (has("on")) slmReqOn = num("on") ? 1 : 0;
  if (has("arm")) slmReqArm = num("arm") ? 1 : 0;
  if (has("act")) slmReqAct = constrain(num("act"), 0, 3);
  if (has("manual")) slmReqManual = num("manual") ? 1 : 0;
  if (has("todraft")) slmReqToDraft = num("todraft") ? 1 : 0;
  if (has("idle")) slmReqIdle = num("idle") ? 1 : 0;
  if (has("pair")) slmReqPair = true;
  if (has("unpair")) slmReqUnpair = true;
  if (has("btype")) slmReqBattType = num("btype") ? 1 : 0;
  if (has("bchem")) slmReqBattChem = constrain(num("bchem"), 0, 2);
  if (has("ethon")) slmReqEthOn = num("ethon") ? 1 : 0;
  if (has("ethcs")) slmReqEthCs = constrain(num("ethcs"), 1, 4);
  if (has("ethint")) slmReqEthInt = constrain(num("ethint"), 0, 4);
  if (has("ecgain")) {
    float g = server.arg("ecgain").toFloat();
    if (!(g > 0.2f && g < 5.0f)) { server.send(400, "text/plain", "EC_GAIN 0.2 to 5"); return; }
    snprintf(slmReqIwbCmd, sizeof(slmReqIwbCmd), "CFG,EC_GAIN,%.5f", g); slmReqIwbCmdOn = true;
  }
  if (has("ecask")) { strlcpy(slmReqIwbCmd, "CFG,EC_GAIN", sizeof(slmReqIwbCmd)); slmReqIwbCmdOn = true; }
  // Niskin bottles
  if (has("nkon")) slmReqNkOn = num("nkon") ? 1 : 0;
  bool nkAsk = has("nkspd") || has("nkplan") || has("nkfire") || has("nkarm") || has("nksafe") || has("nkarmall") || has("nksafeall") || has("nkreset");
  if (nkAsk && (!P.slmOn || !nkEnabled() || !slmArmed)) {            // said so, instead of dropping it
    server.send(409, "text/plain", !P.slmOn || !nkEnabled() ? "Niskin bottles are off (the Add-ons card)" : "Arm the cast session first (the Cast session card)"); return;
  }
  auto nkAct = [&](const char* k, uint8_t a, bool one) {
    if (!has(k)) return true;
    int b = one ? num(k) - 1 : 0;
    if (one && (b < 0 || b >= NK_N)) return false;
    if ((uint8_t)(slmReqNkHead - slmReqNkTail) >= 8) return false;
    slmReqNk[slmReqNkHead & 7] = {(uint8_t)b, a}; slmReqNkHead = slmReqNkHead + 1;
    return true;
  };
  if (has("nkspd") || has("nkplan")) {
    float spd = has("nkspd") ? server.arg("nkspd").toFloat() : NAN;
    if (has("nkspd") && !(spd >= 0.05f && spd <= 2.0f)) { server.send(400, "text/plain", "Lowering speed: 0.05 to 2 m/s"); return; }
    if (has("nkplan")) {                                          // 6 depths, comma separated, blank = none
      float pl[NK_N]; String a = server.arg("nkplan"); int from = 0;
      for (int i = 0; i < NK_N; i++) {
        int c = a.indexOf(',', from); String v = c < 0 ? a.substring(from) : a.substring(from, c); from = c < 0 ? a.length() : c + 1;
        v.trim(); pl[i] = v.length() ? v.toFloat() : NAN;
      }
      BBottles B; portENTER_CRITICAL(&slmMux); B = slmBt; portEXIT_CRITICAL(&slmMux);
      float s2 = !isnan(spd) ? spd : (slmBtMs && B.lowerMps > 0) ? B.lowerMps : 0.20f;
      float rch = (slmBtMs && B.rechargeS > 0 && B.rechargeS < 60) ? B.rechargeS : 1.0f;
      float gap = max(0.5f, s2 * rch * 2.0f);
      for (int i = 0; i < NK_N; i++) {
        if (isnan(pl[i])) continue;
        if (pl[i] < 0.3f || pl[i] > 300) { char t[64]; snprintf(t, sizeof(t), "Bottle %d: 0.3 to 300 m", i + 1); server.send(400, "text/plain", t); return; }
        for (int k = i + 1; k < NK_N; k++) if (!isnan(pl[k]) && fabsf(pl[i] - pl[k]) < gap - 0.001f) {
          char t[96]; snprintf(t, sizeof(t), "Bottles %d and %d are closer than %.1f m: the capacitor can't recharge between", i + 1, k + 1, gap);
          server.send(400, "text/plain", t); return;
        }
      }
      memcpy(slmReqNkPlan, pl, sizeof(pl)); slmReqNkPlanOn = true;
    }
    if (!isnan(spd)) slmReqNkSpd = spd;
  }
  if (!nkAct("nkfire", NK_A_FIRE, true) || !nkAct("nkarm", NK_A_ARM, true) || !nkAct("nksafe", NK_A_SAFE, true) ||
      !nkAct("nkarmall", NK_A_ARMALL, false) || !nkAct("nksafeall", NK_A_SAFEALL, false) || !nkAct("nkreset", NK_A_RESET, false)) {
    server.send(400, "text/plain", "Bottle 1 to 6"); return;
  }
  server.send(200, "text/plain", "ok");
}

// ----------------------------------------------------------------- WTB files through the VMR (web task)
uint32_t slmReqId = 0;
SemaphoreHandle_t slmFileMx = nullptr;
bool slmFileWait(SlmRx& f, uint32_t ms) { return slmFileQ && xQueueReceive(slmFileQ, &f, pdMS_TO_TICKS(ms)) == pdTRUE; }
void handleWtbFiles() {
  if (!P.slmOn || !slmLinkFresh()) { server.send(503, "application/json", "{\"err\":\"the WTB is not on\"}"); return; }
  if (!slmFileMx) slmFileMx = xSemaphoreCreateMutex();
  if (xSemaphoreTake(slmFileMx, pdMS_TO_TICKS(3000)) != pdTRUE) { server.send(503, "application/json", "{\"err\":\"busy\"}"); return; }
  uint8_t dir = server.arg("dir").toInt() ? 1 : 0;
  SlmRx f; while (slmFileQ && xQueueReceive(slmFileQ, &f, 0) == pdTRUE) {}
  String j = "{\"files\":["; uint16_t start = 0, total = 1; bool first = true; int tries = 0;
  while (start < total && start < 400 && tries < 4) {
    BFList q = {dir, start}; wtbTx(T_FLIST, &q, sizeof(q));
    if (!slmFileWait(f, 800)) { tries++; continue; }
    const LnkHdr* h = (const LnkHdr*)f.d; if (h->type != T_FLISTR) continue;
    const BFListR* r = (const BFListR*)(f.d + sizeof(LnkHdr));
    if (r->start != start) continue;
    total = r->total;
    for (int i = 0; i < r->n && i < 5; i++) {
      if (!first) j += ','; first = false;
      char nm[41]; strlcpy(nm, r->e[i].name, sizeof(nm));
      j += "{\"n\":\""; j += nm; j += "\",\"s\":"; j += (unsigned long)r->e[i].size; j += '}';
    }
    start += r->n; if (!r->n) break;
  }
  j += "],\"total\":"; j += total; j += '}';
  xSemaphoreGive(slmFileMx);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}
// one piece of a WTB file (off=, len=, as /dl): asked for in up to 16 KB requests, re-asked from any gap
void handleWtbDl() {
  if (!P.slmOn || !slmLinkFresh()) { server.send(503, "text/plain", "The WTB is not on"); return; }
  String n = server.arg("f"); uint8_t dir = server.arg("dir").toInt() ? 1 : 0;
  if (!n.length() || n.length() > 39 || n.indexOf('/') >= 0) { server.send(400, "text/plain", "bad name"); return; }
  if (!slmFileMx) slmFileMx = xSemaphoreCreateMutex();
  if (xSemaphoreTake(slmFileMx, pdMS_TO_TICKS(5000)) != pdTRUE) { server.send(503, "text/plain", "busy"); return; }
  uint32_t off = server.arg("off").toInt(), want = server.hasArg("len") ? server.arg("len").toInt() : 0x7FFFFFFF;
  SlmRx f; while (xQueueReceive(slmFileQ, &f, 0) == pdTRUE) {}
  uint32_t total = 0, got = 0; bool started = false; int fails = 0;
  NetworkClient& c = server.client();
  while (fails < 6) {
    uint32_t ask = min<uint32_t>(16384, want - got);
    if (started && !ask) break;
    BFGet q = {}; q.reqId = ++slmReqId; q.dir = dir; strlcpy(q.name, n.c_str(), sizeof(q.name)); q.off = off + got; q.len = started ? ask : min<uint32_t>(16384, want);
    wtbTx(T_FGET, &q, sizeof(q));
    uint32_t pieceEnd = q.off + q.len, t0 = millis(); bool gap = false;
    while (millis() - t0 < 1500) {
      if (!slmFileWait(f, 400)) { gap = true; break; }
      const LnkHdr* h = (const LnkHdr*)f.d; if (h->type != T_FDATA) continue;
      const BFData* d = (const BFData*)(f.d + sizeof(LnkHdr)); if (d->reqId != q.reqId) continue;
      if (d->status) { if (!started) { xSemaphoreGive(slmFileMx); server.send(404, "text/plain", "No such file on the WTB"); return; } gap = true; break; }
      if (!started) {
        total = d->total; if (off > total) off = total;
        want = min(want, total - off);
        server.sendHeader("X-Size", String(total)); server.sendHeader("Access-Control-Expose-Headers", "X-Size"); server.sendHeader("Cache-Control", "no-store");
        server.setContentLength(want); server.send(200, "text/csv", ""); started = true;
        if (!want) break;
      }
      uint16_t dn = h->len - 13;
      if (d->off != off + got) { gap = true; break; }             // out of order: ask again from here
      uint32_t use = min<uint32_t>(dn, want - got);
      if (c.write(d->d, use) != use) { xSemaphoreGive(slmFileMx); return; }
      got += use; t0 = millis();
      if (got >= want || d->off + dn >= pieceEnd) break;
    }
    if (started && got >= want) break;
    if (gap) fails++; else fails = 0;
    while (xQueueReceive(slmFileQ, &f, 0) == pdTRUE) {}
  }
  xSemaphoreGive(slmFileMx);
  if (!started) server.send(504, "text/plain", "No answer from the WTB");
  else if (got < want) c.stop();
}

// ----------------------------------------------------------------- firmware for the WTB or the IWB, through the VMR
// The phone uploads the .bin to this card (/fw/wtb.bin or /fw/iwb.bin); a task then sends it over ESP-NOW,
// 8 chunks in flight, the WTB answering where it wants the next byte from (go-back-N).
FwRelay fwr;
File fwUpF; bool fwUpBad = false; uint32_t fwUpSize = 0, fwUpCrc = 0;
void fwRelayTask(void*) {
  char path[20]; snprintf(path, sizeof(path), "/fw/%s.bin", fwr.target == 1 ? "wtb" : "iwb");
  File f; if (sdLock(2000)) { f = SD.open(path); sdUnlock(); }
  if (!f) { fwr.st = 3; strlcpy(fwr.text, "staged file missing", sizeof(fwr.text)); vTaskDelete(nullptr); return; }
  SlmRx m; while (xQueueReceive(slmFwQ, &m, 0) == pdTRUE) {}
  BFwBegin b = {}; b.target = fwr.target; b.size = fwr.size; b.crc = fwr.crc; strlcpy(b.ver, "", sizeof(b.ver));
  bool begun = false;
  for (int i = 0; i < 10 && !begun; i++) {
    wtbTx(T_FWBEGIN, &b, sizeof(b));
    if (xQueueReceive(slmFwQ, &m, pdMS_TO_TICKS(600)) == pdTRUE) { const BFwAck* a = (const BFwAck*)(m.d + sizeof(LnkHdr)); if (a->status == 2) { fwr.st = 3; strlcpy(fwr.text, a->text, sizeof(fwr.text)); goto out; } begun = true; }
  }
  if (!begun) { fwr.st = 3; strlcpy(fwr.text, "no answer from the WTB", sizeof(fwr.text)); goto out; }
  {
    uint32_t sent = 0; uint32_t lastProgress = millis(); BFwChunk c;
    fwr.acked = 0;
    while (fwr.acked < fwr.size) {
      while (sent < fwr.size && sent < fwr.acked + 8 * LNK_DATA) {     // fill the window
        uint16_t n = min<uint32_t>(LNK_DATA, fwr.size - sent);
        bool ok = false; if (sdLock(1000)) { ok = f.seek(sent) && f.read(c.d, n) == n; sdUnlock(); }
        if (!ok) { fwr.st = 3; strlcpy(fwr.text, "card read failed", sizeof(fwr.text)); goto out; }
        c.off = sent; wtbTx(T_FWCHUNK, &c, 4 + n); sent += n;
        vTaskDelay(1);
      }
      if (xQueueReceive(slmFwQ, &m, pdMS_TO_TICKS(250)) == pdTRUE) {
        const BFwAck* a = (const BFwAck*)(m.d + sizeof(LnkHdr));
        if (a->status == 2) { fwr.st = 3; strlcpy(fwr.text, a->text, sizeof(fwr.text)); goto out; }
        if (a->next > fwr.acked) { fwr.acked = a->next; lastProgress = millis(); }
      } else sent = fwr.acked;                                     // nothing back: go back to the last byte it has
      if (millis() - lastProgress > 15000) { fwr.st = 3; strlcpy(fwr.text, "the WTB stopped answering", sizeof(fwr.text)); goto out; }
    }
    BFwEnd e = {fwr.crc}; bool done = false;
    strlcpy(fwr.text, fwr.target == 1 ? "checking on the WTB" : "the WTB is sending it to the IWB", sizeof(fwr.text));
    uint32_t t0 = millis(); bool endSent = false;
    while (millis() - t0 < 90000 && !done) {
      if (!endSent) { wtbTx(T_FWEND, &e, sizeof(e)); endSent = true; }
      if (xQueueReceive(slmFwQ, &m, pdMS_TO_TICKS(1000)) == pdTRUE) {
        const BFwAck* a = (const BFwAck*)(m.d + sizeof(LnkHdr));
        if (a->status == 1) { fwr.st = 2; strlcpy(fwr.text, a->text, sizeof(fwr.text)); done = true; }
        else if (a->status == 2) { fwr.st = 3; strlcpy(fwr.text, a->text, sizeof(fwr.text)); done = true; }
        else if (a->text[0]) strlcpy(fwr.text, a->text, sizeof(fwr.text));
      } else if (millis() - t0 > 5000 && fwr.target == 1) endSent = false;   // resend the end if nothing came back
    }
    if (!done) { fwr.st = 3; strlcpy(fwr.text, "no final answer", sizeof(fwr.text)); }
  }
out:
  if (sdLock(1000)) { f.close(); sdUnlock(); } else f.close();
  vTaskDelete(nullptr);
}
void handleFwRelayUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    fwUpBad = false; fwUpSize = 0; fwUpCrc = 0;
    if (fwr.st == 1 || !fwPasswordOk(server.arg("pw"))) { fwUpBad = true; return; }   // the same password as the VMR's own update
    fwr.target = server.arg("t") == "iwb" ? 2 : 1;
    char path[20]; snprintf(path, sizeof(path), "/fw/%s.bin", fwr.target == 1 ? "wtb" : "iwb");
    if (!sdOK || !sdLock(3000)) { fwUpBad = true; return; }
    if (!SD.exists("/fw")) SD.mkdir("/fw");
    fwUpF = SD.open(path, FILE_WRITE); sdUnlock();
    if (!fwUpF) fwUpBad = true;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (fwUpBad) return;
    if (!fwUpSize && u.currentSize && u.buf[0] != 0xE9) { fwUpBad = true; return; }
    fwUpCrc = lnkCrc32(fwUpCrc, u.buf, u.currentSize);
    bool ok = false; if (sdLock(3000)) { ok = fwUpF.write(u.buf, u.currentSize) == u.currentSize; sdUnlock(); }
    if (!ok) fwUpBad = true; else fwUpSize += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
    if (sdLock(3000)) { if (fwUpF) fwUpF.close(); sdUnlock(); }
    if (u.status == UPLOAD_FILE_ABORTED) fwUpBad = true;
  }
}
void handleFwRelayDone() {
  if (fwUpBad || !fwUpSize) { server.send(400, "text/plain", fwr.st == 1 ? "An update is already being sent" : !fwPasswordOk(server.arg("pw")) ? "Wrong password: unlock again" : "Not an ESP32 image, or the card failed"); return; }
  if (!P.slmOn || !slmLinkFresh()) { server.send(503, "text/plain", "Saved on the card, but the WTB is not on: arm a session and send it again"); return; }
  fwr.size = fwUpSize; fwr.crc = fwUpCrc; fwr.acked = 0; fwr.st = 1; strlcpy(fwr.text, "sending", sizeof(fwr.text));
  if (xTaskCreate(fwRelayTask, "fwrelay", 6144, nullptr, 1, nullptr) != pdPASS) { fwr.st = 3; strlcpy(fwr.text, "no memory for the task", sizeof(fwr.text)); }
  server.send(200, "text/plain", "sending");
}
