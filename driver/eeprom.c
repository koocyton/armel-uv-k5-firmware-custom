/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <stddef.h>
#include <string.h>

#include "driver/eeprom.h"
#include "driver/i2c.h"
#include "driver/system.h"

void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size)
{
	I2C_Start();

	I2C_Write(0xA0);

	I2C_Write((Address >> 8) & 0xFF);
	I2C_Write((Address >> 0) & 0xFF);

	I2C_Start();

	I2C_Write(0xA1);

	I2C_ReadBuffer(pBuffer, Size);

	I2C_Stop();
}

void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer)
{
	if (pBuffer == NULL || Address >= 0x2000)
		return;


	uint8_t buffer[8];
	EEPROM_ReadBuffer(Address, buffer, 8);
	if (memcmp(pBuffer, buffer, 8) == 0) {
		return;
	}

	I2C_Start();
	I2C_Write(0xA0);
	I2C_Write((Address >> 8) & 0xFF);
	I2C_Write((Address >> 0) & 0xFF);
	I2C_WriteBuffer(pBuffer, 8);
	I2C_Stop();

	// give the EEPROM time to burn the data in (apparently takes 5ms)
	SYSTEM_DelayMs(8);
}

void EEPROM_ReadBuffer32(uint32_t Address, void *pBuffer, uint16_t Size)
{
	uint8_t *dst = (uint8_t *)pBuffer;
	for (uint16_t offset = 0; offset < Size; ) {
		uint16_t chunk = Size - offset;
		if (chunk > 248)
			chunk = 248;
		uint32_t a = Address + offset;
		uint8_t dev = (uint8_t)(0xA0U | ((a / 65536U) << 1));
		uint16_t addr16 = (uint16_t)(a & 0xFFFFU);

		I2C_Start();
		I2C_Write(dev);
		I2C_Write((uint8_t)(addr16 >> 8));
		I2C_Write((uint8_t)(addr16 & 0xFF));
		I2C_Start();
		I2C_Write(dev | 1);
		I2C_ReadBuffer(dst + offset, (uint8_t)chunk);
		I2C_Stop();
		offset += chunk;
	}
}

void EEPROM_WriteBuffer32(uint32_t Address, const void *pBuffer, uint16_t Size)
{
	const uint8_t *src = (const uint8_t *)pBuffer;
	uint16_t offset = 0;
	while (offset < Size) {
		uint16_t chunk = Size - offset;
		if (chunk > 8)
			chunk = 8;
		uint32_t a = Address + offset;
		uint8_t dev = (uint8_t)(0xA0U | ((a / 65536U) << 1));
		uint16_t addr16 = (uint16_t)(a & 0xFFFFU);

		I2C_Start();
		I2C_Write(dev);
		I2C_Write((uint8_t)(addr16 >> 8));
		I2C_Write((uint8_t)(addr16 & 0xFF));
		I2C_WriteBuffer(src + offset, (uint8_t)chunk);
		I2C_Stop();
		SYSTEM_DelayMs(8);
		offset += chunk;
	}
}
