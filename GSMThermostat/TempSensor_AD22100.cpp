#include "TempSensor_AD22100.h"

void TempSensorAD22100::Initialize(byte pSensorPin)
{
    pinMode(pSensorPin , INPUT);
    FSensorPin = pSensorPin;
}

int IntSort(const void * p1, const void *p2)
{
     return *(int *)p1 - *(int *)p2;
}

double TempSensorAD22100::ReadTemperatureInCelsius()
{
    int values[NUM_SAMPLES];
    byte i;
    double sum;
    
    //Wait for a while to avoid commutation noise
    delay(NOISE_DELAY_MS);
    
    //Take X samples, discard the lower K and higher K the calculate the mean
    for(i = 0; i < NUM_SAMPLES; i++)
        values[i] = analogRead(FSensorPin);
        
    //Sort values so it's easier to discard fisrt K and last K
    qsort(values, NUM_SAMPLES, sizeof(int), IntSort);

    //sum central values and calculate the mean
    for(i = NUM_SAMPLES_DISCARDED, sum = 0.0; i < (NUM_SAMPLES - NUM_SAMPLES_DISCARDED); i++)
        sum += ((values[i] * (5.0 / 1024)) - 1.375) / 0.0225;
    
    return  sum / (double) (NUM_SAMPLES - NUM_SAMPLES_DISCARDED * 2);
}

