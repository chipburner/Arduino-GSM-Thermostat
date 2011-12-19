#include <SoftwareSerial.h>
#include "LatchedRelais.h"   
#include "Utils.h"    
#include "SerialDebug.h"    

#define LATCHED_RELAIS	true

void LatchedRelais::Initialize(byte pSetPin, byte pResetPin)
{
    pinMode(pSetPin, OUTPUT);   
    pinMode(pResetPin, OUTPUT);    
    FPackedVars.pinSet = pSetPin;
    FPackedVars.pinReset = pResetPin;
}

void LatchedRelais::Set()
{
#if LATCHED_RELAIS
    PulseOut(FPackedVars.pinSet, RELAIS_PULSE_DURATION_MS);
#else
	digitalWrite(FPackedVars.pinSet, HIGH);
#endif
    FStatus = true;
    DEBUG_P(PSTR("Relais --> SET"LB));
}

void LatchedRelais::Reset()
{
#if LATCHED_RELAIS
    PulseOut(FPackedVars.pinReset, RELAIS_PULSE_DURATION_MS);
#else
	digitalWrite(FPackedVars.pinSet, LOW);
#endif
	FStatus = false;
    DEBUG_P(PSTR("Relais --> RESET"LB));
}

boolean LatchedRelais::IsSet()
{
    return FStatus;
}

