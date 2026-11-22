// VMR demo: the Datalogger peek, the boot list and the marks screen (as VMR.ino)

uint32_t bootLineMs = 0;
#define BOOT_LINES_MAX  16
#define BOOT_ROWS_SHOWN 6
char    bootTxt[BOOT_LINES_MAX][22];
uint8_t bootN = 0;



