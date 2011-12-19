#ifndef __LATCHED_RELAIS
#define __LATCHED_RELAIS
#include "WProgram.h"

#define RELAIS_PULSE_DURATION_MS            300

class LatchedRelais
{
public:
    void Initialize(byte pSetPin, byte pResetPin);

    void Set();
    void Reset();

    boolean IsSet();
private:
    typedef struct _PackedVars
    {
        byte pinSet : 4;  
        byte pinReset : 4; 
    } 
    PackedVars;    
private:
    boolean FStatus; 
    PackedVars FPackedVars;
};

#endif

