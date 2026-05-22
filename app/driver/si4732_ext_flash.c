/* Virtual 256 KiB EEPROM for Si4732 SSB patch tools (0x05F0 / 0x05F2). */
#include "driver/si4732_ext_flash.h"
#include "driver/si4732_storage.h"
#include "driver/py25q16.h"
#include <string.h>

void SI4732_VirtEeprom_Read32(uint32_t addr, void *pBuffer, uint16_t size)
{
    if (!pBuffer || !size)
        return;
    if (addr >= SI4732_VIRT_EEPROM_SIZE)
        return;
    if ((uint32_t)size > SI4732_VIRT_EEPROM_SIZE - addr)
        size = (uint16_t)(SI4732_VIRT_EEPROM_SIZE - addr);
    PY25Q16_ReadBuffer(SI4732_VIRT_EEPROM_PY_BASE + addr, pBuffer, size);
}

void SI4732_VirtEeprom_Write32(uint32_t addr, const void *pBuffer, uint16_t size)
{
    if (!pBuffer || !size || (size % 8U) != 0U)
        return;
    if (addr >= SI4732_VIRT_EEPROM_SIZE)
        return;
    if ((uint32_t)size > SI4732_VIRT_EEPROM_SIZE - addr)
        return;
    const uint8_t *p = (const uint8_t *)pBuffer;
    for (uint16_t i = 0; i < size; i += 8U)
        PY25Q16_WriteBuffer(SI4732_VIRT_EEPROM_PY_BASE + addr + i, p + i, 8U, false);
}
