// the log rows' ring buffer (VMR.ino owns the storage). One writer (loop(): pendPut) and one reader at a time
// (whoever holds sdMutex). Free-running byte counts; pendCap is a power of two, so they wrap cleanly at 2^32.
// In a header so the Arduino builder makes no prototypes for these ahead of the sketch's own types.
#pragma once
#include <Arduino.h>
extern char* pend;
extern size_t pendCap;
extern volatile uint32_t pendIn, pendOut;
extern volatile bool pendDiscard;
inline size_t pendCount() { return pendIn - pendOut; }
inline bool pendPut(const char* s, size_t L) {
  if (!pendCap || pendCount() + L > pendCap) return false;
  size_t at = pendIn & (pendCap - 1), a = L < pendCap - at ? L : pendCap - at;
  memcpy(pend + at, s, a);
  if (L > a) memcpy(pend, s + a, L - a);
  __sync_synchronize();
  pendIn += L;
  return true;
}
inline size_t pendPeek(const char** p, size_t max) {      // the next contiguous piece (reader, sdMutex held)
  if (pendDiscard) { pendOut = pendIn; pendDiscard = false; }
  size_t n = pendCount();
  if (!n || !pendCap) return 0;
  size_t at = pendOut & (pendCap - 1), c = n < pendCap - at ? n : pendCap - at;
  if (c > max) c = max;
  *p = pend + at;
  return c;
}
inline void pendTake(size_t n) { __sync_synchronize(); pendOut += n; }
inline void pendClear() { pendOut = pendIn; }      // reader side only
