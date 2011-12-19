#ifndef __MODEMGSM
#define __MODEMGSM
#include "WProgram.h"
#include "Timeout.h"

#define SIMULATION						false

#define RX_BUFFER_SIZE					163

#define SMS_TEXT_BUFFER_SIZE			161
#define PHONE_NUMBER_BUFFER_SIZE		21
#define MODEM_SERIAL_BAUD_RATE			115200
#define SMS_IN_QUEUE_MAX_ITEM_COUNT		10
#define SMS_OUT_QUEUE_MAX_ITEM_COUNT	10
#define URC_QUEUE_MAX_ITEM_COUNT		10
#define MODEM_POWERON_PULSE_MS			2000
#define NETWORK_LED_UPDATE_INTERVAL_MS	5000
#define NETWORK_REGISTRAION_DELAY_MS	15000
#define SMS_RETRY_COUNT					12
#define SMS_RETRY_DELAY_MS				15000 //Total Retry time = 12*15 seconds --> 3 minutes

#define REGISTRATION_DELAYED			//Enables a delay before assuming to be registered to network

typedef struct _SMS
{
    char phone[PHONE_NUMBER_BUFFER_SIZE];
    char body[SMS_TEXT_BUFFER_SIZE];
}TSMS;
typedef TSMS * TSMSPtr;

template <int i> 
class SMSIndexQueue
{
public:
    SMSIndexQueue() {FCount = 0;};

    boolean Enqueue(int pIndex);    
    boolean Dequeue(int *pIndex);
	boolean Peek(int *pIndex);

    inline byte Count() 
    {
        return FCount;
    };
    
    void Clear();
protected:
    int FSMSQueue[i];
    byte FCount;

	void Dispose(byte pIndex);
};

template <int i> 
class URCQueue
{
public:
    URCQueue() {FCount = 0;};

    boolean Enqueue(const char *pURCText);     
    boolean Dequeue(char *pURCText, size_t pURCTextSize);

    inline byte Count() 
    { 
        return FCount;
    };
    
    void Clear(); 

protected:
    char *FURCQueue[i];
    byte FCount;

	void Dispose(byte pIndex);
};


class ModemGSM
{
	typedef enum _StandardAnswer {saTimeout, saOk, saError, saUnknown} EStandardAnswer;
protected:
    boolean FRegisteredToNetwork;
    byte FNetworkLedPin;
    byte FPowerOnPin;

    char FRXBuff[RX_BUFFER_SIZE];     

    SMSIndexQueue <SMS_IN_QUEUE_MAX_ITEM_COUNT> FSMSInQueue;
    SMSIndexQueue <SMS_OUT_QUEUE_MAX_ITEM_COUNT> FSMSOutQueue;
    URCQueue <URC_QUEUE_MAX_ITEM_COUNT> FURCQueue; 
    HardwareSerial *FSerial;
    char FLastCommand[SMS_TEXT_BUFFER_SIZE];
	byte FSignalLevel;
	boolean FPBReady;
#ifdef REGISTRATION_DELAYED
	boolean FNetworkRegDelayActive;
    Timeout FNetworkRegDelayTS;    
#endif
	boolean FSMSResendPending;
	byte FSMSRetry;

    Timeout FLastKeepAliveTS;
    Timeout FLastBlinkTS;
	Timeout FSMSResendTS;
	boolean FError;
	byte FKeepAliveFailedCount;

    boolean InnerSetup();
    void DiscardSerialInput(unsigned int pTimeout);  
    void DiscardPrompt(unsigned int pTimeout);

	EStandardAnswer WaitAnswer(unsigned int pTimeoutMS, boolean pHandleURC=false);
	boolean HandleURC();

    int Readln(unsigned int pTimeout, boolean pIgnoreLeadingLF);    
    void SendCommand(const char *__fmt, ...);
	void EvalNetworkLedStatus();
	boolean WriteSMS(const char *pDestPhoneNumber, const char *pBody,int *pIndex);
	boolean SendSMSAtIndex(int pIndex);
	boolean SendKeepAlive();
public:
	typedef enum _Queue {qIn, qOut} EQueue;

    boolean Initialize(HardwareSerial *pSerial, byte pNetworkLedPin, byte pPowerOnPin);
    void PowerOn();
    int Dispatch();

    boolean SendSMS(const char *pDestPhoneNumber, const char *pBody);

    boolean SMSDequeue(EQueue pQueue, TSMSPtr pItem);    
    int SMSCount(EQueue pQueue);

	boolean GetPBEntryByName(const char *pName,char *pNumber, int *pIndex);
	boolean NumberExistsInPB(const char *pNumber, int *pIndex);
	boolean RegisterNumberInPB(char *pName, char *pNumber);
	boolean DeletePBEntryAtIndex(byte pIndex);

	boolean ClearSMSMemory();
	boolean ReadSMSAtIndex(int pIndex, TSMSPtr pSMS);
	boolean DeleteSMSAtIndex(int pIndex);

	inline boolean Error() { return FError;};
	inline boolean IsPBReady() { return FPBReady;};
	inline boolean IsRegisteredToNetwork() {return FRegisteredToNetwork; };	
	inline boolean IsSMSAvailable() {return FSMSInQueue.Count() != 0; };
};

#define UNKNOWN_LEVEL	99
#endif

