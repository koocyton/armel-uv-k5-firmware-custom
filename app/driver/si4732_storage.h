/* Si4732 / FM related SPI flash addresses (PY25Q16 linear), separate from other settings. */
#ifndef DRIVER_SI4732_STORAGE_H
#define DRIVER_SI4732_STORAGE_H

#include <stdint.h>

/* FM cfg block 0x00A020..0x00A027: bytes 0-3 = stock packed cfg; bytes 4-7 = FM freq Hz (Si4732). */
#define FM_PY_FMCFG_ADDR           0x00A020U

/* Dedicated 8 bytes: AM kHz (2), BFO (2), LNA (1), … — does not overlap CPS VFO / menu / spectrum blocks. */
#define FM_PY_SI4732_AM_EXT_ADDR   0x00A170U

#ifndef SI4732_VIRT_EEPROM_PY_BASE
/* Linear 256 KiB window at end of 2 MiB external flash for SSB patch (UART 0x05F0/0x05F2). Override in CMake if needed. */
#define SI4732_VIRT_EEPROM_PY_BASE 0x001C0000U
#endif

#define SI4732_VIRT_EEPROM_SIZE   262144U

#endif
