
#ifdef VERSION_STRING
	#define VER     " "VERSION_STRING
#else
	#define VER     ""
#endif


/* Version[] is sent to CPS. With ENABLE_2MBIT_EEPROM use "HS" suffix so CPS may show 2Mbit. */
#ifdef ENABLE_2MBIT_EEPROM
const char Version[]      = "KOO V1.0   HS";
#else
const char Version[]      = "KOO V1.0 ";
#endif
const char UART_Version[] = "UV-K5 Firmware, Open Edition, " AUTHOR_STRING VER "\r\n";

