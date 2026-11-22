// VMR demo: the voyage record (as VMR.ino)
#define VTXT 24
struct Voyage {
  uint8_t  vClass;
  uint8_t  towMode;
  uint8_t  towLen05;
  uint8_t  bargeType;
  uint8_t  lenUnit;
  uint8_t  tonUnit;
  uint8_t  pad[2];
  float    bLen, bWid, bDf, bDm, bDa, bTon;
  char     classTxt[VTXT], condTxt[VTXT], towTxt[VTXT], bargeTxt[VTXT], voyage[16];
};
uint32_t slewOffMs = 0;
uint64_t clockMsAt(uint32_t ms);
