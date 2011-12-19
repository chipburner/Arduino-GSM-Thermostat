#ifndef __TEMP_SENSOR
#define __TEMP_SENSOR
#include "WProgram.h"

class TempSensorAD22100
{
public:
    void Initialize(byte pSensorPin);
    double ReadTemperatureInCelsius();
    
protected:
    byte FSensorPin;
};

#define NUM_SAMPLES                20
#define NUM_SAMPLES_DISCARDED      4
#define NOISE_DELAY_MS             5

#endif

