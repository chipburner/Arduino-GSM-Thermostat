#include <limits.h>
#include <SoftwareSerial.h>

#include "SerialDebug.h"
#include "Utils.h"
#include "ModemGSM.h"
#include <pins_arduino.h>

#define CR 13
#define LF 10
#define TIMEOUT 0
#define KEEPALIVE_INTERVAL_MS 15000

#define INIT_SEQUENCE "ATE0 ; +CREG=1; +CMGF=1; +CSCS=\"IRA\"; +CNMI=1,1; +CMER=2,0,0,1,1; +CMEE=2"

// ATE1 ; +CREG=1; +CMGF=1; +CSCS="IRA"; +CNMI=1,1; +CMER=2,0,0,1,1


byte resetCount=0;

void ModemGSM::DiscardSerialInput(unsigned int pTimeout)
{
	unsigned long ts = millis();

	for(;SafeSub(millis(), ts) < pTimeout;)
	{
		if(FSerial->available() > 0)
			FSerial->read();
	}    
}

void ModemGSM::DiscardPrompt(unsigned int pTimeout)
{
	unsigned long ts = millis();

	for(;SafeSub(millis(), ts) < pTimeout;)
	{
		if(FSerial->available() > 0)
		{
			if(FSerial->read() == '>')
				break;
		}
	}    
}

int ModemGSM::Dispatch()
{
	int res = 0;
	int level;
	int ind;
	static int state=0;

	EvalNetworkLedStatus();

#ifdef REGISTRATION_DELAYED
	if(FNetworkRegDelayActive && FNetworkRegDelayTS.IsExpired())
	{
		DEBUG_P(PSTR("Network Registration Delay Expired --> Now Registered To Network"LB));                    

		FNetworkRegDelayActive = false;
		FRegisteredToNetwork = true;
	}
#endif
	if(FLastKeepAliveTS.IsExpired())
	{
#if SIMULATION
		if(FPBReady)
		{
			int idx;
			char temp[30];

//			switch(state % 3) //repeat commands
			switch(state)
			{
				case 0:
				{
					DEBUG_P(PSTR("---- Posting Fake SMS"LB));
					WriteSMS("+390000000000", "ON 0000,22", &idx);
					sprintf_P(temp, PSTR("+CMTI: \"SM\",%d"), idx);
					break;
				}
				case 1:
				{
					DEBUG_P(PSTR("---- Posting Fake SMS"LB));
					WriteSMS("+390000000000", "ON 0000,22", &idx);
					sprintf_P(temp, PSTR("+CMTI: \"SM\",%d"), idx);
					break;
				}

				default:
				{
					temp[0] = 0;

					if(!SendKeepAlive())
					{
						FKeepAliveFailedCount++;
						//if the modem is not answering try the hard way ...
						if(FKeepAliveFailedCount > 5)
						{
							//Try to power off the modem
							SendCommand("AT+CPWROFF");
							delay(1000);

							//Signal Error condition
							FError = true;
							return res;
						}
					}
					else
					{
						FError = false;
						FKeepAliveFailedCount = 0;
					}				
				}
			}

			state++;
			if(temp[0])
			{
				FURCQueue.Enqueue(temp);			
				DEBUG_P(PSTR("---- Posted Fake SMS"LB));
			}
		}
		else
#endif
			if(!SendKeepAlive())
			{
				FKeepAliveFailedCount++;
				//if the modem is not answering try the hard way ...
				if(FKeepAliveFailedCount > 5)
				{
					//Try to power off the modem
					SendCommand("AT+CPWROFF");
					delay(1000);

					//Signal Error condition
					FError = true;
					return res;
				}
			}
			else
			{
				FError = false;
				FKeepAliveFailedCount = 0;
			}

		FLastKeepAliveTS.Reset();
	};
	
	int idx;
	//Check for Queued URC or Data from Modem SerialLine
	if((FURCQueue.Count() ? FURCQueue.Dequeue(FRXBuff, sizeof(FRXBuff)) : false) || (Readln(100, false) != TIMEOUT))
	{
		if(sscanf_P(FRXBuff, PSTR("+CMTI: \"%*[^\"]\",%d"), &idx) == 1)
		{                
			DEBUG_P(PSTR("SMS Received at -> %d"LB), idx);

			//Save SMS position in SM Memory, the SMS is stored by the Modem
			FSMSInQueue.Enqueue(idx);
		}
		else if((sscanf_P(FRXBuff,PSTR("+CMGS: %d"), &level) == 1) || (sscanf_P(FRXBuff,PSTR("+CMSS: %d"), &level) == 1))
		{
			DEBUG_P(PSTR("Last Message Sent Successfully"LB));
		}
		else if(sscanf_P(FRXBuff,PSTR("+CIEV: %d,%d"), &ind, &level) == 2)
		{
			if(ind == 2)
			{
				if((level >= 1) && (level <= 5))
				{
					FSignalLevel = level;
					DEBUG_P(PSTR("Signal Strength --> %d"LB), level);                    
				}
				else
				{
					FSignalLevel = UNKNOWN_LEVEL;
					DEBUG_P(PSTR("**BAD Signal Strength --> %d"LB), level);                    
				}
			}
		}
		else if(sscanf_P(FRXBuff,PSTR("+CREG: %d"), &level) == 1)
		{
			if((level == 1) || (level == 5))
			{
#ifdef REGISTRATION_DELAYED
				DEBUG_P(PSTR("Registered to Network Indication --> Starting Delay"LB));                    
				FNetworkRegDelayActive = true;
				//Wait for a while to be sure that SMS will work
				FNetworkRegDelayTS.Reset(); 
#else
				FRegisteredToNetwork = true;
				DEBUGLN_P(PSTR("Registered to Network"LB)); 
#endif
			}
			else
			{
				DEBUG_P(PSTR("NOT Registered to Network"LB));
				FRegisteredToNetwork = false;
#ifdef REGISTRATION_DELAYED
				FNetworkRegDelayActive = false;
#endif
			}
			FLastBlinkTS.Reset();
			EvalNetworkLedStatus();
		}
		else if(strncmp(FRXBuff,"+XDRVI: ", 8) == 0)
		{        
			resetCount++;
			DEBUG_P(PSTR("Module RESET"LB));
			DiscardSerialInput(5000);
			InnerSetup();
		}
		else if(strcmp_P(FRXBuff,PSTR("+PBREADY")) == 0)
		{        
			FPBReady = true;
			for(int i = 0; i < 10; i++)
				if(ClearSMSMemory())
					break;
				else
				{
					delay(1000);
					DEBUG_P(PSTR("Retrying .... "LB));
				}
				
			FLastKeepAliveTS.Reset();
		}
		else
		{
			DEBUG_P(PSTR("** Unhandled -> "));
			DEBUGLN(FRXBuff);
		}    
	}

	//Do not fire SMS retries if the network is not available
	if((FSMSOutQueue.Count() && FRegisteredToNetwork) && !(FSMSResendPending && !FSMSResendTS.IsExpired()))    
	{
		int idx;

		FSMSOutQueue.Peek(&idx);
		
		if(SendSMSAtIndex(idx))
		{
			SMSDequeue(qOut, NULL);
			FSMSResendPending = false;
		}
		else
		{
			//A retry failed ?
			if(FSMSResendPending)
			{
				//some other retry available ?
				if(FSMSRetry)
				{
					FSMSRetry--;
					FSMSResendTS.Reset();
					DEBUG_P(PSTR("Retrying SMS Send. Retry Count --> %d"LB), (int)FSMSRetry);
				}
				else
				{
					//discard SMS
					FSMSResendPending = false;
					FSMSOutQueue.Dequeue(NULL);
					DEBUG_P(PSTR("** Too Many Retries SMS Send Aborted"LB));
				}
			}
			else
			{
				//Start a delay and retry sequence
				FSMSRetry = SMS_RETRY_COUNT;
				FSMSResendPending = true;
				FSMSResendTS.Reset();
				DEBUG_P(PSTR("Retrying SMS Send. Retry Count --> %d"LB), (int)FSMSRetry);
			}		
		}
	}

	return res;
}

void ModemGSM::EvalNetworkLedStatus()
{
	if(FRegisteredToNetwork)
	{
		if(FSignalLevel == UNKNOWN_LEVEL)
		{
			digitalWrite(FNetworkLedPin, HIGH);	
		}
		else
		{
			if(FLastBlinkTS.IsExpired())
			{
				FLastBlinkTS.Reset();

				for(int i = 0; i < FSignalLevel; i++)
				{
					digitalWrite(FNetworkLedPin, HIGH);	
					delay(150);
					digitalWrite(FNetworkLedPin, LOW);	
					delay(100);
				}
			}		
		}
	}
	else
	{
		digitalWrite(FNetworkLedPin, LOW);	
	}
}


boolean ModemGSM::Initialize(HardwareSerial *pSerial, byte pNetworkLedPin, byte pPowerOnPin)
{
	pinMode(pNetworkLedPin, OUTPUT);
	pinMode(pPowerOnPin, OUTPUT);

	FSerial = pSerial;
	FNetworkLedPin = pNetworkLedPin;
	FPowerOnPin = pPowerOnPin;

	FLastKeepAliveTS.Set(KEEPALIVE_INTERVAL_MS);
	FLastBlinkTS.Set(NETWORK_LED_UPDATE_INTERVAL_MS);
#ifdef REGISTRATION_DELAYED
	FNetworkRegDelayTS.Set(NETWORK_REGISTRAION_DELAY_MS);
#endif
	FSMSResendTS.Set(SMS_RETRY_DELAY_MS);

	PowerOn();

	return InnerSetup();
}

boolean ModemGSM::HandleURC()
{
	FURCQueue.Enqueue(FRXBuff);
}

boolean ModemGSM::SendSMSAtIndex(int pIndex)
{
	boolean res = false;

	DEBUG_P(PSTR("Sending SMS at index --> %d"LB), pIndex);

#if SIMULATION

	delay(5000);

	if(1 || (FSMSResendPending && (FSMSRetry==10)))
	{
		FURCQueue.Enqueue("+CMSS: 99");		
		res = true;
	}
	else
		FURCQueue.Enqueue("ERROR");
#else
	SendCommand(PSTR("AT+CMSS=%d"), pIndex);

	for(;;)
	{
		switch(WaitAnswer(45000, true))
		{
			case saOk:
			{
				DEBUG_P(PSTR("  SMS sent"LB));
				return true;
			}
			case saError:
			case saTimeout:
				return false;    
		}
	}
#endif

	return res;
}

boolean ModemGSM::WriteSMS(const char *pDestPhoneNumber, const char *pBody,int *pIndex)
{
	boolean res = false;

	DEBUG_P(PSTR("Writing SMS --> %s : "), pDestPhoneNumber);
	DEBUGLN(pBody);

	if(pIndex)
		*pIndex = 0;
	SendCommand(PSTR("AT+CMGW=\"%s\""), pDestPhoneNumber);

	DiscardPrompt(1500);            //Discard modem prompt

	FSerial->print(pBody);

	delay(500);
	FSerial->print(0x1A,BYTE);      //CTRL+Z End of Message


	for(;;)
	{
		//AT+CMGW can take a long time to answer
		switch(WaitAnswer(20000))
		{
			case saOk:
				return res;    
			case saError:
			case saTimeout:
				return false;    
			case saUnknown:
			{
				int idx;

				if(sscanf_P(FRXBuff, PSTR("+CMGW: %d"), &idx) == 1)
				{             
					DEBUG_P(PSTR("SMS Written at index -> %d"LB), idx);

					res = true;

					if(pIndex)
						*pIndex = idx;
				}
				else
					HandleURC();
			}		
		}
	}
}


boolean ModemGSM::ReadSMSAtIndex(int pIndex, TSMSPtr pSMS)
{
	boolean hasEntry = false;

	DEBUG_P(PSTR("Reading SMS entry at --> %d"LB), pIndex);

	SendCommand(PSTR("AT+CMGR=%d"), pIndex);

	for(;;)
	{
		switch(WaitAnswer(5000))
		{
			case saOk:
				return hasEntry;    
			case saError:
			case saTimeout:
				return false;    
			case saUnknown:
			{
				int idx;

				if(sscanf_P(FRXBuff, PSTR("+CMGR: \"%*[^\"]\",\"%20[^\"]\""), pSMS->phone) == 1)
				{             
					DEBUG_P(PSTR("  SMS Number -> %s"LB), pSMS->phone);

					if(Readln(500, true) != 0)
					{ 
						DEBUG_P(PSTR("  SMS Text -> "));
						DEBUGLN(FRXBuff);

						strncpy(pSMS->body, FRXBuff, sizeof(pSMS->body) - 1); 
						pSMS->body[sizeof(pSMS->body) - 1] = '\0';

						hasEntry = true;
					}
					else
					{
						DEBUG_P(PSTR("** Timeout reading SMS text"LB));  
						return false;    
					}    
				}
				else
					HandleURC();
			}		
		}
	}
}

boolean ModemGSM::DeleteSMSAtIndex(int pIndex)
{
	boolean res = false;

	DEBUG_P(PSTR("Deleting SMS entry at --> %d"LB), pIndex);
	
	SendCommand(PSTR("AT+CMGD=%d"), pIndex);

	return WaitAnswer(10000, true) == saOk;
}

boolean ModemGSM::SendKeepAlive()
{
	boolean res = false;

	DEBUG_P(PSTR("Sending KeepAlive"LB));

	SendCommand(PSTR("AT"));

	return WaitAnswer(1000, true) == saOk;
}


boolean ModemGSM::NumberExistsInPB(const char *pNumber, int *pIndex)
{
	boolean hasEntry = false;

	DEBUG_P(PSTR("Searching Number in PB [1..20] --> %s"LB), pNumber);

	SendCommand(PSTR("AT+CPBR=1,20"));

	for(int foundIdx = 0;;)
	{
		switch(WaitAnswer(5000))
		{
			case saOk:
			{
				if(hasEntry)
				{
					DEBUG_P(PSTR("  Number Found at position --> %d"LB), foundIdx);

					if(pIndex)
						*pIndex = foundIdx;
				}
				else
				{
					DEBUG_P(PSTR("  Number Not Found"LB));
				}
				
				return hasEntry;

				break;
			}
			case saError:
			case saTimeout:
				return false;    
			case saUnknown:
			{
				char number[PHONE_NUMBER_BUFFER_SIZE];
				int idx;

				if((sscanf_P(FRXBuff,PSTR("+CPBR: %d,\"%20[^\"]\""), &idx, number) == 2))
				{
					//DEBUG_P("  Checking number --> ");
					//DEBUG(idx);
					//DEBUG_P(" : [");
					//DEBUG(number);
					//DEBUGLN_P("]");
					if(!hasEntry)
					{
						foundIdx = idx;
						hasEntry = strcmp(number, pNumber) == 0;
					}
				}
				else
					HandleURC();
			}		
		}
	}

	return false;
}

boolean ModemGSM::RegisterNumberInPB(char *pName, char *pNumber)
{
	DEBUG_P(PSTR("Writing PB entry --> "));

	if(pName)
	{
		DEBUG(pName);
		DEBUG_P(PSTR(" : "));
	}
	DEBUGLN(pNumber);

	if(pName)
	{	
		SendCommand(PSTR("AT+CPBW=,\"%s\",,\"%s\""), pNumber, pName);
	}
	else
		SendCommand(PSTR("AT+CPBW=,\"%s\""), pNumber);

	boolean res = WaitAnswer(5000, true) == saOk;

	//Wait a while. It seems that issuing a command immediately after this the modem hangs
	delay(2000);
	
	return res;
}


boolean ModemGSM::GetPBEntryByName(const char *pName, char *pNumber, int *pIndex)
{
	boolean hasEntry = false;

	DEBUG_P(PSTR("Reading PB entry --> %s"LB), pName);

	SendCommand(PSTR("AT+CPBF=\"%s\""), pName);

	for(;;)
	{
		switch(WaitAnswer(10000))
		{
			case saOk:
				return hasEntry;    
			case saError:
			case saTimeout:
				return false;    
			case saUnknown:
			{
				int idx;

				//Stop on first entry. 
				if(!hasEntry && (sscanf_P(FRXBuff,PSTR("+CPBF: %d,\"%20[^\"]\""), &idx, pNumber) == 2))
				{
					DEBUGLN(pNumber);
					hasEntry = true;

					if(pIndex)
						*pIndex = idx; 
				}
				else
					HandleURC();
			}
		}
	}
}


boolean ModemGSM::ClearSMSMemory()
{
	DEBUG_P(PSTR("Deleting All SMS from SM Memory"LB));

	SendCommand(PSTR("AT+CMGD=0,4"));

	boolean res = WaitAnswer(20000, true) == saOk;
	delay(2000);
	return res;
}

boolean ModemGSM::DeletePBEntryAtIndex(byte pIndex)
{
	DEBUG_P(PSTR("Deleting PB entry at --> %d"LB), pIndex);

	SendCommand(PSTR("AT+CPBW=%d"), (int) pIndex);

	return WaitAnswer(1000, true) == saOk;
}

boolean ModemGSM::InnerSetup()
{
	boolean res;

	FSMSResendPending = false;

	FSMSOutQueue.Clear();
	FSMSInQueue.Clear();

	FRegisteredToNetwork = false;
	FSignalLevel = UNKNOWN_LEVEL;
	FPBReady = false;
	FError = false;
	FKeepAliveFailedCount = 0;


#ifdef REGISTRATION_DELAYED
	FNetworkRegDelayActive = false;
#endif

	DiscardSerialInput(2000);               //Discard serial line junk

	//Inizializziamo la seriale Hardware con il Baud rate imposto dal Modem GSM    
	FSerial->end();
	FSerial->begin(MODEM_SERIAL_BAUD_RATE);          
	
	SendCommand(PSTR("AT+IPR=9600"));

	delay(500);
	FSerial->end();
	FSerial->begin(9600);          
	DiscardSerialInput(500);                //discard the command ECHO (initialization will disable command echo)
	DEBUG_P(PSTR("Serial Speed 9600"LB));
	
	FSerial->print(INIT_SEQUENCE);			//Send initialization sequence to modem
	DiscardSerialInput(500);                //discard the command ECHO (initialization will disable command echo)
	FSerial->print("\r");                   //Commit

	res = WaitAnswer(1000, true) == saOk;

	DEBUG_P(PSTR("Init Command Sequence --> " INIT_SEQUENCE LB));

	if(res)
		DEBUG_P(PSTR("OK"LB));
	else
		DEBUG_P(PSTR("FAIL"LB));

	return res;
}

void ModemGSM::PowerOn()
{
	DEBUG_P(PSTR("Modem Powering ..."LB));
	PulseOut(FPowerOnPin, MODEM_POWERON_PULSE_MS);   
	DEBUG_P(PSTR("Modem Power is ON"LB));
}

int ModemGSM::Readln(unsigned int pTimeout, boolean pIgnoreLeadingLF)
{
	unsigned long ts = millis();
	byte count = 0;

	////////////////////////////////////////////////////////////////////////////////////
	//	Answer from modem are in the format:
	//
	//	<CR><LF>Text can contain " or <LF> <CR><LF>
	//	<CR><LF>OK<CR><LF>
	//
	//	Reading read a SMS Text we must not expect the leading <CR><LF>.
	//	Setting the pIgnoreLeadingLF parameter to true no leading <CR><LF> is expected
	//	Moreover SMS Text can contain <LF> chars without the leading <CR>.
	//	Standalone <LF> chars must be stored because are used to start a new line
	//	in SMS text
	////////////////////////////////////////////////////////////////////////////////////


	boolean hasLeadingCRLF = pIgnoreLeadingLF;
	boolean hasLeadingCR = false;
	
	for(;;)
	{
		if(FSerial->available() > 0)
		{
			char c;

			c =  FSerial->read();
			ts = millis();

			if(count == (sizeof(FRXBuff) - 1))
			{
				DEBUG_P(PSTR("** Line too long --> %d"LB), (int)count);
				FRXBuff[count] = '\0';
				return count;
			}

			if(c == CR)
				hasLeadingCR = true;
			else if((c == LF) && hasLeadingCR)
			{
				if(hasLeadingCRLF)
				{
					FRXBuff[count] = '\0';
					//DEBUG_P(PSTR("ReadLine ["));
					//DEBUG(FRXBuff);
					//DEBUG_P(PSTR("]"LB));
					return count;
				}
				else
					hasLeadingCRLF = true;
			}
			//Discard chars before first <CR><LF>
			else if(hasLeadingCRLF) 
				FRXBuff[count++] = c;
		}
		else
		{
			if(SafeSub(millis(), ts) > pTimeout)
				return count;
		}
	}
}

int ModemGSM::SMSCount(EQueue pQueue) 
{
	return (pQueue == qIn ? FSMSInQueue.Count() : FSMSOutQueue.Count()); 
};

boolean ModemGSM::SMSDequeue(EQueue pQueue, TSMSPtr pItem) 
{ 
	int idx;
	boolean res = false;

	DEBUG_P(PSTR("SMS Dequeue From --> "));

	if(pQueue == qIn )
		DEBUG_P(PSTR("IN"LB));
	else
		DEBUG_P(PSTR("OUT"LB));

	if(res = (pQueue == qIn ? FSMSInQueue.Dequeue(&idx) : FSMSOutQueue.Dequeue(&idx)))
	{
		if(pItem)
			if(!(res = ReadSMSAtIndex(idx, pItem)))
				DEBUG_P(PSTR("ReadSMSAtIndex FAIL"LB));
		if(res)
			if(!(res = DeleteSMSAtIndex(idx)))
				DEBUG_P(PSTR("DeleteSMSAtIndex FAIL"LB));
	}
	else
		DEBUG_P(PSTR("Dequeue FAIL"LB));

	if(res)
		DEBUG_P(PSTR("SMS Dequeue OK"LB));
	else
		DEBUG_P(PSTR("SMS Dequeue FAIL"LB));

	return res;
}

void ModemGSM::SendCommand(const char *__fmt, ...)
{
	char buffer[101];
	va_list arglist;
	
	va_start( arglist, __fmt );
    vsprintf_P(buffer, __fmt, arglist );
    va_end( arglist );	

	FSerial->println(buffer);
}

boolean ModemGSM::SendSMS(const char *pDestPhoneNumber, const char *pBody)
{   
	boolean res = false;
	int idx;

	if(res = WriteSMS(pDestPhoneNumber, pBody, &idx))
	{
		res = FSMSOutQueue.Enqueue(idx);
		if(res)
		{
			DEBUG_P(PSTR("SMS Enqueue OK index --> %d"LB), idx);
		}
		else
			DEBUG_P(PSTR("SMS Enqueue FAIL"LB));
	}

	return res;
}

ModemGSM::EStandardAnswer ModemGSM::WaitAnswer(unsigned int pTimeoutMS, boolean pHandleURC)
{
	//DEBUG_P("WaitAnswer --> ");
	for(;;)
	{
		if(Readln(pTimeoutMS, false) == TIMEOUT)
		{
			DEBUG_P(PSTR("** TIMEOUT"LB));
			return saTimeout;
		}
		
		//DEBUGLN(FRXBuff);

		if(strcmp_P(FRXBuff,PSTR("OK")) == 0)
		{
			//DEBUGLN_P("OK");
			return saOk;    
		}
		else if(	(strcmp_P(FRXBuff,PSTR("ERROR")) == 0) ||
					(strncmp_P(FRXBuff,PSTR("+CME ERROR:"), 11) == 0) ||
					(strncmp_P(FRXBuff,PSTR("+CMS ERROR:"), 11) == 0))
		{
			DEBUG_P(PSTR("** "));
			DEBUGLN(FRXBuff);
			return saError;    
		}
		else if(pHandleURC)
			HandleURC();
		else
			return saUnknown;    
	}
}


template <int i>
boolean URCQueue<i>::Enqueue(const char *pURCText)
{
	if(FCount >= i)
	{
		DEBUG_P(PSTR("** URC Queue is Full"LB));
		return false;
	}

	DEBUG_P(PSTR("Queuing URC --> "));
	DEBUGLN(pURCText);

	char *item = strdup(pURCText);
	if(item)
	{
		FURCQueue[FCount] = item;

		FCount++;    
		return true;
	}
	else
	{
		DEBUG_P(PSTR("**URC Not enough memory"LB));
		return false;
	}
};

template <int i>
boolean URCQueue<i>::Dequeue(char *pURCText, size_t pURCTextSize)
{
	if(FCount == 0)
	{
		DEBUG_P(PSTR("** URC Queue is Empty"LB));
		return false;
	}

	DEBUG_P(PSTR("Dequeuing URC --> "));

	if(pURCText)
	{
		strncpy(pURCText, FURCQueue[0], pURCTextSize - 1);
		pURCText[pURCTextSize - 1] = '\0';
		DEBUGLN(FURCQueue[0]);
	}
	else	
		DEBUG_P(PSTR("Value Discarded"LB));

	Dispose(0);

	FCount--;

	memmove(FURCQueue, FURCQueue+1, sizeof(char *) * FCount);

	return true;      
};

template <int i>
void URCQueue<i>::Dispose(byte pIndex)
{ 
	free(FURCQueue[pIndex]);
	FURCQueue[pIndex] = NULL;
};

template <int i>
void URCQueue<i>::Clear()
{ 
	for(byte j = 0; j < FCount; j++)
		Dispose(j);

	FCount = 0;
};


template <int i>
boolean SMSIndexQueue<i>::Enqueue(int pIndex)
{
	if(FCount >= i)
	{
		DEBUG_P(PSTR("SMS Queue is Full"LB));
		return false;
	}

	FSMSQueue[FCount] = pIndex;

	FCount++;    
};

template <int i>
boolean SMSIndexQueue<i>::Dequeue(int *pIndex)
{
	if(FCount == 0)
	{
		DEBUG_P(PSTR("** Dequeue: SMS Queue is Empty"LB));
		return false;
	}

	if(pIndex)
	{
		*pIndex = FSMSQueue[0];
	}

	FCount--;

	memmove(FSMSQueue, FSMSQueue+1, sizeof(int) * FCount);

	return true;      
};

template <int i>
void SMSIndexQueue<i>::Clear()
{ 
	FCount = 0;
};

template <int i>
boolean SMSIndexQueue<i>::Peek(int *pIndex)
{ 
	if(FCount == 0)
	{
		DEBUG_P(PSTR("** Peek: SMS Queue is Empty"LB));
		return false;
	}

	if(pIndex)
	{
		*pIndex = FSMSQueue[0];
	}

	return true;    
};
