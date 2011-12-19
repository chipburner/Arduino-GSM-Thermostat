#include "WProgram.h"
#include <limits.h>
#include "Utils.h"

unsigned long SafeSub(unsigned long p1, unsigned long p2)
{
    if(p1 >= p2)
        return p1 - p2;
    else
        return ULONG_MAX - (p2 - p1) + 1;
}

void PulseOut(byte pPin, unsigned int pDelayMS)
{
    digitalWrite(pPin, HIGH);
    delay(pDelayMS);
    digitalWrite(pPin, LOW);
}
