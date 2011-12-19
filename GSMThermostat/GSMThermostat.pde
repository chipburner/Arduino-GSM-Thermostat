////////////////////////////////////////////////////////////////////////////////////
//	GSM Thermostat
//	Hardware: 
//		Arduino Uno
//		Shield Dualband Libellium (Modem GSG DavisComm DS3500)
//		Temperature Sensor AD22100
//		Relais Latched dual coil Omron G6CK-1117P-US 5DC
//
//	Recognized commands:
//
//	REGISTER <Pin 4 digits>
//	UNREGISTER <Pin 4 digits>
//	CHPIN <old Pin 4 digits> , <new Pin 4 digits>
//  ON <Pin 4 digits> , <Temperature>
//  OFF <Pin 4 digits>
//  MAINPHONE <Pin 4 digits>, <Y | N>
//  STATUS
//	RESET <Pin 4 digits>
//
////////////////////////////////////////////////////////////////////////////////////


#include <limits.h>
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>


#include "WProgram.h"
#include "ModemGSM.h"
#include "SerialDebug.h"
#include "TempSensor_AD22100.h"
#include "Utils.h"
#include "Timeout.h"
#include "LatchedRelais.h"
#include "PinConfig.h"

#include <EEPROM.h>

#define VERSION_STR							"GSM Thermostat 1.1"

#define HALF_DELTA_TEMP                     0.5			//Delta to calculate Trigger temperatures
#define STABLE_TEMPERATURE_INTERVAL_MS      15000		//Temperature must be stable for this time to trigger changes
#define TEMP_MIN                            5.0			//"ON" command min temperature
#define TEMP_MAX                            28.0		//"ON" command max temperature
#define MAX_ON_INTERVAL_DAYS				3			//Periodo massimo per cui il termostato resta attivo, alla fine del periodo passa automaticamente a OFF	

#define PIN_EEPROM_START_ADDRESS			1			//Pin EEPROM first address
#define DEFAULT_PIN							"0000"		//Default Pin if not programmed
#define MAX_COMMAND_LEN						80			//Max SMS Text Command length
#define MAX_RESET_MESSAGE_COUNT				4			//Number of Soft Reset Messages
#define DEBUG_INFO_INTERVAL_MS				30000		//Debug info interval

boolean Active;											//Thermostat function status
float TempSet;											//"ON" command temperature
float LastTemp;											//Last temperature reading
boolean SendOnceTempOK;									//Send a temperature OK SMS once
char ONCommandPhone[PHONE_NUMBER_BUFFER_SIZE];			//On Command Phone number source
char Pin[5];											//Pin 4 digits + \0

bool PowerOnMessageSent;								//Power On SMS already sent
bool ResetMessagePending;								//True if a Informational Soft Reset SMS is to be sent
bool ResetCommandMessagePending;						//True if a Informational User Reset SMS is to be sent
byte ResetMessageAvail;									//Counter for Soft Reset messages

Timeout TempDebug;
Timeout TempInterval;									
Timeout OnCommandTS;									//On Command timestamp to calculate auto off timeout

ModemGSM GSMModem;										//GSM Modem
TempSensorAD22100 TSense;								//Temperature Sensor AD22100
LatchedRelais Relais;									//Latched Relais (dual coil)  

#define MAX_ON_INTERVAL_MS ((unsigned long)1000*60*60*24*MAX_ON_INTERVAL_DAYS)		//Timeout in milliseconds
#define MAINPHONE_PB_ENTRY "MAINPHONE"
#define MAX_COMMAND_ANSWER_LEN (MAX_COMMAND_LEN + 61)

void InitPin()
{
	//After erasing EEPROM cells contain 0xFF
	if(EEPROM.read(PIN_EEPROM_START_ADDRESS) == 0xFF)
	{
		//Set default Pin
		strcpy_P(Pin, PSTR(DEFAULT_PIN));
		WritePinToEEPROM();
		DEBUG_P(PSTR("Pin Initialized to " DEFAULT_PIN LB));
	}
	else
	{
		//Read the Pin from EEPROM
		ReadPinFromEEPROM();
		DEBUG_P(PSTR("Pin --> %s"LB), Pin);
		//DEBUGLN(Pin);
	}
}


void SendInformationalSMS(const prog_char *pMessage)
{
	char number[PHONE_NUMBER_BUFFER_SIZE];
	char msg[61];

	if(GSMModem.GetPBEntryByName(MAINPHONE_PB_ENTRY, number, NULL))
	{
		strncpy_P(msg, pMessage, sizeof(msg));
		msg[sizeof(msg) - 1] = '\0';
		SendSMS(number, msg);
	}
    else
        DEBUG_P(PSTR("No phonebook entry available for Informational Message"LB));
}

void setup()
{
  //for(;;);

    //DEBUG serial initialization
    DebugSerialInitialize();

	//Maximum number of Soft Reset (avoid sending too many Soft Reset SMS)
	ResetMessageAvail = MAX_RESET_MESSAGE_COUNT;
	ResetMessagePending = false;
	PowerOnMessageSent = false;
	ResetCommandMessagePending = false;

    DEBUG_P(PSTR(VERSION_STR LB));

	//Led port initialization
    pinMode(PIN_LED_ACTIVE, OUTPUT);
    pinMode(PIN_LED_RELAIS_SET, OUTPUT);
    
	//Relais initialization
    Relais.Initialize(PIN_RELAIS_SET, PIN_RELAIS_RESET);
    //Make sure the relais is in the OFF state
	Relais.Reset();
    
    //Temperature sensor initialization
    TSense.Initialize(PIN_TEMP_SENSOR);
    
    //Wait for corrent GSM modem initialization
    for(;!GSMModem.Initialize(&Serial, PIN_MODEM_LED_NETWORK, PIN_MODEM_POWER););
    
	TempDebug.Set(DEBUG_INFO_INTERVAL_MS);
	TempInterval.Set(STABLE_TEMPERATURE_INTERVAL_MS);
	OnCommandTS.Set(MAX_ON_INTERVAL_MS);
		
	//PIN initialization
	InitPin();

    DEBUG_P(PSTR("Initialization DONE"LB));
 }

boolean SendSMS(const char *pPhone, const char *pBody)
{
    DEBUG_P(PSTR("Queuing SMS --> %s : "), pPhone);
    DEBUGLN(pBody);

    return GSMModem.SendSMS(pPhone, pBody);
}

void ReadPinFromEEPROM()
{
	for(int i = 0; i < (sizeof(Pin) - 1); i++)
		Pin[i] = EEPROM.read(i +  PIN_EEPROM_START_ADDRESS);

	//Make PIN a C string
	Pin[sizeof(Pin)-1] = '\0';
}

void WritePinToEEPROM()
{
	//Do not save string terminator
	for(int i = 0; i < (sizeof(Pin) - 1); i++)
	{
		EEPROM.write(i +  PIN_EEPROM_START_ADDRESS, Pin[i]);
	}
}

boolean CheckRelaisState(double pTemperature)
{
	//If the heater is on, power off the heater if room temperature is HALF_DELTA_TEMP above desired temperature
	//If the heater is off, power on the heater if room temperature is HALF_DELTA_TEMP below desired temperature
    if(Relais.IsSet())
        return (pTemperature <= (TempSet + HALF_DELTA_TEMP));
    else
        return (pTemperature <= (TempSet - HALF_DELTA_TEMP));
}

void HandleOff()
{
    Active = false;
            
    Relais.Reset();
    digitalWrite(PIN_LED_RELAIS_SET, LOW);
    digitalWrite(PIN_LED_ACTIVE, LOW);
}

boolean CheckPin(const char * pPinToCheck)
{
	return (strlen(pPinToCheck) == 4) && (strcasecmp(Pin, pPinToCheck) == 0);
}

void HandleReset()
{
	//Heater Power off
    HandleOff();
    
    //wait fo GSM Modem initialization
    for(;!GSMModem.Initialize(&Serial, PIN_MODEM_LED_NETWORK, PIN_MODEM_POWER););
}

void HandleCommand(TSMSPtr pItem)
{
    char tmpStr[MAX_COMMAND_ANSWER_LEN];
    char temp[10];
    char temp2[10];
	char pin[5];
    int intPart;
    int decPart;
	double newTemp;
	int pbIndex;
	bool trusted;

#if !(MAX_COMMAND_ANSWER_LEN <= SMS_TEXT_BUFFER_SIZE)
#error "Constant definition violates rule MAX_COMMAND_ANSWER_LEN <= SMS_TEXT_BUFFER_SIZE"	
#endif
    
	//Make command uppercase, so it's easier to parse
    strupr(pItem->body);
    
	//Enforce maximum command length 
	pItem->body[MAX_COMMAND_LEN] = '\0';

    DEBUG_P(PSTR("Handling Command --> "));
    DEBUGLN(pItem->body);

	if(!(trusted = GSMModem.NumberExistsInPB(pItem->phone, &pbIndex)))
	{
		DEBUG_P(PSTR("Command Received from an untrusted number --> %s"LB), pItem->phone);
	}
    
    ///////////////////////////////////////////////////////////////
    //Command "REGISTER <PIN>"
    //eg REGISTER XXXX
    
    if(sscanf_P(pItem->body, PSTR("REGISTER %4s"), pin) == 1)
    {
		if(CheckPin(pin))
		{
			DEBUG_P(PSTR("Handling REGISTER Command"LB));

			if(trusted)
			{
				sprintf_P(tmpStr,PSTR("\"%s\"\nALREADY REGISTERED"),pItem->body);
			}
			else
			{
				sprintf_P(tmpStr,PSTR("\"%s\"\n%s"),pItem->body, (GSMModem.RegisterNumberInPB("", pItem->phone) ? "OK" : "ERROR"));
			}
		}
		else
badPin:
			sprintf_P(tmpStr,PSTR("\"%s\"\nBad Pin"),pItem->body);
    }
	else 
	{	
		//All commands but "REGISTER" must be received from a trusted phone
		if(!trusted)
		{
			DEBUG_P(PSTR("** Command Execution Aborted"LB));
			return;
		}

		///////////////////////////////////////////////////////////////
		//Command "ON <PIN>,<Temperature>" 
		//eg ON XXXX,18.5
		//eg ON 1234,19
    
		//let's hack with scanf because %f is not supported
		byte sCount = sscanf(pItem->body, "ON %4s , %d.%d", pin, &intPart, &decPart);
		if(sCount > 1)
		{
			if(CheckPin(pin))
			{
				DEBUG_P(PSTR("Handling ON Command"LB));
				//if there are 2 field the temperature is and integer number
				if(sCount == 2)
					newTemp = intPart;
				else //if there are more than 2 field the temperature is and fp number
				{
						newTemp = decPart;
             
						//divide for 10 until < 1
						for(;newTemp >= 1; newTemp /= 10);
            
						newTemp += intPart;   
				}
        
				dtostrf(newTemp, 1, 1 ,temp);
				DEBUG_P(PSTR("Parse Temperature --> %s"LB), temp);
        
				//Temperature out of range
				if(newTemp > TEMP_MAX)
					newTemp = TEMP_MAX;
				else if(newTemp < TEMP_MIN)
					newTemp = TEMP_MIN;

				//if thermostat is off -> on
				else if(!Active)
				{        
					Active = true;
					digitalWrite(PIN_LED_ACTIVE, HIGH);

					//save the phone number for future SMS send
					strcpy(ONCommandPhone, pItem->phone);
            
					//Stable temperature timeout initialization
					TempInterval.Reset();

					//Auto heater power off timeout initialization
					OnCommandTS.Reset();

					TempSet = newTemp;
            
					//if the heater must be powered on rember to send a SMS when the temperature will be OK
					SendOnceTempOK = CheckRelaisState(LastTemp);

					dtostrf(TempSet, 1, 1 , temp);
					sprintf_P(tmpStr,PSTR("\"%s\"\nOK set to [%s C]"),pItem->body, temp);
				}
				//Thermostat is already on send a confirmation SMS
				else
				{
					dtostrf(TempSet, 1, 1 , temp);
					dtostrf(newTemp, 1, 1 , temp2);
					sprintf_P(tmpStr,PSTR("\"%s\"\nChanged [%s C] --> [%s C]"),pItem->body, temp, temp2);
				 
					TempSet = newTemp;
				}
			}
			else
				goto badPin;
		}

		///////////////////////////////////////////////////////////////
		//Command "UNREGISTER <PIN>"
		//eg UNREGISTER XXXX

		else if(sscanf_P(pItem->body, PSTR("UNREGISTER %4s"), pin) == 1)
		{
			if(CheckPin(pin))
			{
				DEBUG_P(PSTR("Handling UNREGISTER Command"LB));
				sprintf_P(tmpStr,PSTR("\"%s\"\n%s"),pItem->body, (GSMModem.DeletePBEntryAtIndex(pbIndex) ? "OK" : "ERROR"));
			}
			else
				goto badPin;
		}

		///////////////////////////////////////////////////////////////
		//Command "OFF <PIN>"
		//eg OFF XXXX
    
		else if(sscanf_P(pItem->body, PSTR("OFF %4s"), pin) == 1)
		{
			if(CheckPin(pin))
			{
				DEBUG_P(PSTR("Handling OFF Command"LB));

				if(Active)
				{
					sprintf_P(tmpStr,PSTR("\"%s\"\nOK"),pItem->body);
					HandleOff();
				}
				else
				{
					sprintf_P(tmpStr,PSTR("\"%s\"\nAlready OFF"),pItem->body);
				}
			}
			else
				goto badPin;
		}

		///////////////////////////////////////////////////////////////
		//Command "STATUS"
		//eg STATUS

		else if(strncmp_P(pItem->body, PSTR("STATUS"), 6) == 0)
		{
			DEBUG_P(PSTR("Handling STATUS Command"LB));
			
			dtostrf(TSense.ReadTemperatureInCelsius(), 1, 1 ,temp);

			sprintf_P(tmpStr,PSTR("\"%s\"\n%s  %s"),pItem->body, (Active ? "ON" : "OFF"), temp);
		}
    
		///////////////////////////////////////////////////////////////
		//Command "MAINPHONE <PIN,<Y|N>"
		//eg MAINPHONE XXXX, Y
    
		else if(sscanf_P(pItem->body, PSTR("MAINPHONE  %4s,%1s"), pin, temp) == 2)
		{
			if(CheckPin(pin))
			{
				//char *tmp;
				char number[20];
				int idx;
				bool res;

				DEBUG_P(PSTR("Handling MAINPHONE Command"LB));

				//Delete anyway, then Add if parameter is 'Y'
				//Lookup entry index for deletion
				if(GSMModem.GetPBEntryByName(MAINPHONE_PB_ENTRY, number, &idx))
				{
					//Delete entry at index idx
					DEBUG_P(PSTR("Deleting MAINPHONE at index --> %d"LB), (int)idx);

					if(res = GSMModem.DeletePBEntryAtIndex(idx))
						DEBUG_P(PSTR(" OK"));
					else
						DEBUG_P(PSTR("** FAIL"LB));
				}
				else
					res = true;

				if(temp[0] == 'Y')
					res = GSMModem.RegisterNumberInPB(MAINPHONE_PB_ENTRY, pItem->phone);

				sprintf_P(tmpStr,PSTR("\"%s\"\n%s"),pItem->body, (res ? "OK" : "ERROR"));
			}
			else
				goto badPin;
		}

		///////////////////////////////////////////////////////////////
		//Command "RESET <PIN>"
		//eg RESET XXXX
    
		else if(sscanf_P(pItem->body, PSTR("RESET %4s"), pin) == 1)
		{
			if(CheckPin(pin))
			{
				DEBUG_P(PSTR("Handling RESET Command"LB));
				ResetCommandMessagePending = true;
				HandleReset();
				return;
			}
			else
				goto badPin;

		}
    
		///////////////////////////////////////////////////////////////
		//Command "CHPIN"
		//eg CHPIN XXXX, YYYY
    
		else if(sscanf(pItem->body, "CHPIN %4s , %4s", temp, temp2) == 2)
		{
			if((strlen(temp) == 4) && (strlen(temp2) == 4))
			{
				ReadPinFromEEPROM();
				DEBUG_P(PSTR("Old Pin --> %s"LB), temp);

				if(strcasecmp(Pin, temp) == 0)
				{
					DEBUG_P(PSTR("New Pin --> %s"LB), temp2);

					strcpy(Pin, temp2);
					WritePinToEEPROM();
					sprintf_P(tmpStr,PSTR("\"%s\"\nOK"),pItem->body);
				}
				else
					sprintf_P(tmpStr,PSTR("\"%s\"\nInvalid Pin"),pItem->body);

			}
			else
				goto badPin;
		}
		else
		{
			sprintf_P(tmpStr,PSTR("\"%s\"Invalid Command"),pItem->body);
		}
	}

	SendSMS(pItem->phone, tmpStr);
}

void HandleThermostatLoop()
{    
    if(Active)
    {
        boolean newRelaisState;
        char tempStr[60];
        
		if(OnCommandTS.IsExpired())
		{
			char temp[10];

			DEBUG_P(PSTR("ON COMMAND TIMEOUT EXPIRED --> OFF"LB));

			HandleOff();
            dtostrf(LastTemp, 1, 1 , temp);

			sprintf_P(tempStr,PSTR("Timeout Expired now OFF [%s C]"), temp);

			SendSMS(ONCommandPhone, tempStr);
			
			return;
		}


        //Calculate the new heater expected state
        newRelaisState = CheckRelaisState(LastTemp);
        
        /*if(newRelaisState != Relais.IsSet())
        {
            if(newRelaisState)
            {
                DEBUG_P(PSTR("Relais Should be SET --> "));
            }
            else
            {
                DEBUG_P(PSTR("Relais Should be RESET -->"));
            }
        }
        else
        {
            if(Relais.IsSet())
            {
                DEBUG_P(PSTR("Relais is SET --> "));
            }
            else
            {
                DEBUG_P(PSTR("Relais is RESET -->"));
            }
        }
		
        
        dtostrf(LastTemp, 1, 1 ,tempStr);
        DEBUG(tempStr);
        DEBUGLN_P(PSTR(" C"));
        */
       
        //Se il Relais è in the wrong state
        if(newRelaisState != Relais.IsSet())
        {
			dtostrf(LastTemp, 1, 1 ,tempStr);

			DEBUG_P(PSTR("Relais Should change state --> %s C"LB), tempStr);

            //temperature has stable for enauogh time ?
			if(TempInterval.IsExpired())
            {
                //Temperature is stable change relais state
                dtostrf(LastTemp, 1, 1 ,tempStr);                   
                DEBUG_P(PSTR("State Changed and Temperature Now Stable  --> %s"LB), tempStr);
                
                if(newRelaisState)
                {
                    Relais.Set();
                    digitalWrite(PIN_LED_RELAIS_SET, HIGH);
                }
                else
                {
                    Relais.Reset();
                    digitalWrite(PIN_LED_RELAIS_SET, LOW);
                    //If this is the first time that the temperaure is OK the send a SMS
                    if(SendOnceTempOK)
                    {
						strcpy_P(tempStr, PSTR("Temperature OK"));
                        SendSMS(ONCommandPhone, tempStr);
                        SendOnceTempOK = false;
                    }
                }                         
            }
        }
        else
        {
			TempInterval.Reset();
        }        
    };

	if(TempDebug.IsExpired())
    {
        char tempStr[10];
            
		TempDebug.Reset();

        dtostrf(LastTemp, 1, 1 ,tempStr);
        DEBUG_P(PSTR("Temperature --> %s C"LB), tempStr);
        
 		if(Active)
		{
			dtostrf(TempSet, 1, 1 ,tempStr);
			DEBUG_P(PSTR("User Temperature --> %s C"LB), tempStr);
				
			DEBUG_P(PSTR("Relais --> "));		

			if(Relais.IsSet())
			{
				DEBUG_P(PSTR("SET"LB));
			}
			else
			{
				DEBUG_P(PSTR("RESET"LB));
			}
		}
    }
}

void loop()
{
	//Read current temperature
    LastTemp = TSense.ReadTemperatureInCelsius();            

	//Allow the modem to process events
    GSMModem.Dispatch();

	//Modem is not answering, let's try a soft reset
	if(GSMModem.Error())
	{
		if(ResetMessageAvail != 0)
			ResetMessagePending = true;

		DEBUG_P(PSTR("** Modem Error --> RESET"LB));
		HandleReset();
		return;
	}

	//Is Power On SMS has not been sent
	if(!PowerOnMessageSent)
	{
		//Modem ready ?
		if(GSMModem.IsPBReady() && GSMModem.IsRegisteredToNetwork())
		{
			//Send SMS to MAINPHONE command telephone number
			SendInformationalSMS(PSTR("Thermostat Powered On"));
			PowerOnMessageSent = true;				
		}
	}

	if(ResetMessagePending || ResetCommandMessagePending || !PowerOnMessageSent)
	{
		//Modem ready to send SMS ?
		if(GSMModem.IsPBReady() && GSMModem.IsRegisteredToNetwork())
		{
			if(ResetMessagePending)
			{
				//Send SMS to MAINPHONE command telephone number
				SendInformationalSMS(PSTR("Thermostat Soft Reset"));
				ResetMessagePending = false;
				ResetMessageAvail--;
			}

			if(ResetCommandMessagePending)
			{
				//Send SMS to MAINPHONE command telephone number
				SendInformationalSMS(PSTR("Thermostat User Reset"));
				ResetCommandMessagePending = false;
			}

			if(!PowerOnMessageSent)
			{
				//Send SMS to MAINPHONE command telephone number
				SendInformationalSMS(PSTR("Thermostat Powered On"));
				PowerOnMessageSent = true;				
			}
		}
	}

	//Is a SMS is available
	if(GSMModem.IsSMSAvailable())
    {
        TSMS item;
        
        if(GSMModem.SMSDequeue(ModemGSM::qIn, &item))
            HandleCommand(&item);
    }
    else
        HandleThermostatLoop();
}
