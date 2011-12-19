#include <SoftwareSerial.h>

#include "SerialDebug.h"
#include "PinConfig.h"

SoftwareSerial DebugSerial = SoftwareSerial(PIN_DEBUG_SERIAL_RX, PIN_DEBUG_SERIAL_TX);

void DebugSerialInitialize()  
{
    pinMode(PIN_DEBUG_SERIAL_RX, INPUT);
    pinMode(PIN_DEBUG_SERIAL_TX, OUTPUT);
    
    //9600 è il massimo per la Seriale via Software
    DebugSerial.begin(9600);
}

#ifdef MODEM_DEBUG

void Debug_P(PGM_P pStr, boolean pLN)
{
    char *str = (char *)malloc(strlen_P(pStr) + 1);
    if(str)
    {
        strcpy_P(str, pStr);
        if(pLN)
            DebugSerial.println(str);
        else
            DebugSerial.print(str);
        free(str);
    }
}

 void DEBUG_P(PGM_P __fmt, ...)
 {
 	char buffer[161];
	va_list arglist;
	
	va_start( arglist, __fmt );
    vsprintf_P(buffer, __fmt, arglist );
    va_end( arglist );	

	DebugSerial.print(buffer);
 }
#endif