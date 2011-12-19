#ifndef __SERIAL_DEBUG
#define __SERIAL_DEBUG
#include "WProgram.h"
#include <avr/pgmspace.h>

//-------------------------------- Config Begin

//Uncomment to disable serial logging
#define MODEM_DEBUG 

//-------------------------------- Config End

extern void DebugSerialInitialize();
extern SoftwareSerial DebugSerial;
extern void Debug_P(PGM_P pStr, boolean pLN);

#ifdef MODEM_DEBUG
  #define DEBUG(msg)  DebugSerial.print(msg) 
  #define DEBUGLN(msg)  DebugSerial.println(msg)
  void DEBUG_P(PGM_P __fmt, ...);
#else
  #define DEBUG_P // 
  #define DEBUG(msg) 
  #define DEBUGLN(msg) 
#endif

#endif
