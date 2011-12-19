#include "Timeout.h"
#include "Utils.h"

void Timeout::Set(unsigned long pTimeout)
{
	FTimeout = pTimeout;
	Reset();
}

void Timeout::Reset()
{
	FTS = millis();
}


boolean Timeout::IsExpired()
{
	return SafeSub(millis(), FTS) > FTimeout;
}
