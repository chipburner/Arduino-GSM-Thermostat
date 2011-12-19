#ifndef __TIMEOUT
#define __TIMEOUT

#include "WProgram.h"

class Timeout
{
public:
	void Set(unsigned long pTimeout);
	void Reset();
	boolean IsExpired();
protected:
	unsigned long FTS;
	unsigned long FTimeout;
};


#endif
