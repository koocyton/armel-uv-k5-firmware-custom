#ifndef DRIVER_SI4732_EXT_FLASH_H
#define DRIVER_SI4732_EXT_FLASH_H

#include <stdint.h>

void SI4732_VirtEeprom_Read32(uint32_t addr, void *pBuffer, uint16_t size);
void SI4732_VirtEeprom_Write32(uint32_t addr, const void *pBuffer, uint16_t size);

#endif
