#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiType.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>

#include <AzureIoTUtility.h>
#include <AzureIoTHub.h>
#include <AzureIoTProtocol_MQTT.h>

#include <DHT.h>

String ssid = "<Your Network SSID>"; // your network SSID (name)
String pass = "<Your Network Password>";  // your network password (use for WPA, or use as key for WEP)
static const char* connectionString = "<Device Connection String from IoTHub"; //device connection string for the iot hub.
#define DHTPIN D2 // what digital pin we're connected to
#define DHTTYPE DHT22 // DHT11 or DHT22

#define DRYER_RUNNING_SLEEP_INTERVAL 60000 //1 minute
#define DRYER_NOT_RUNNING_SLEEP_INTERVAL 60000//1 minute

#define DRYER_STARTED_TEMP_DIFF 2 //2 Degrees
#define DRYER_STOPPED_TEMP_DIFF 2 //2 Degrees

IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle;
IOTHUB_CLIENT_STATUS status;

WiFiClientSecure espClient;

DHT dht(DHTPIN, DHTTYPE);

float lastTemp;
bool dryerRunning;
float dryerRunningTemp;
int sleepInterval;

int timeInterval;

void initWifi() {
	//If the wifi is not connected, set it up, and reinitialize the time and iothub connection.
    if (WiFi.status() != WL_CONNECTED) 
    {
        WiFi.stopSmartConfig();
        WiFi.enableAP(false);

        // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
        WiFi.begin(ssid.c_str(), pass.c_str());
    
        Serial.print("Waiting for Wifi connection.");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            delay(500);
        }
    
        Serial.println("Connected to wifi");

        initTime();
        initIoTHub();
    }
}

void initTime() {
    time_t epochTime;

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    while (true) {
        epochTime = time(NULL);

        if (epochTime == 0) {
            Serial.println("Fetching NTP epoch time failed! Waiting 2 seconds to retry.");
            delay(2000);
        } else {
            Serial.print("Fetched NTP epoch time is: ");
            Serial.println(epochTime);
            break;
        }
    }
}

static void sendMessage(const char* message)
{
    static unsigned int messageTrackingId;
    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(message);

    if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, messageHandle, sendMessageCallback, (void*)(uintptr_t)messageTrackingId) != IOTHUB_CLIENT_OK)
    {
        Serial.println(" ERROR: Failed to hand over the message to IoTHubClient");
    }
    else
    {
      (void)printf(" Message Id: %u Sent.\r\n", messageTrackingId);
    }

    IoTHubMessage_Destroy(messageHandle);
    messageTrackingId++;
}

void sendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    unsigned int messageTrackingId = (unsigned int)(uintptr_t)userContextCallback;

    (void)printf(" Message Id: %u Received.\r\n", messageTrackingId);
}

static IOTHUBMESSAGE_DISPOSITION_RESULT IoTHubMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* userContextCallback)
{
    IOTHUBMESSAGE_DISPOSITION_RESULT result = IOTHUBMESSAGE_ACCEPTED;
    
    const char* messageId = "UNKNOWN";      // in case there is not a messageId associated with the message -- not required
    messageId = IoTHubMessage_GetMessageId(message);

    const unsigned char* buffer;
    size_t size;
    if (IoTHubMessage_GetByteArray(message, &buffer, &size) != IOTHUB_MESSAGE_OK)
    {
        Serial.println(" Error: Unable to IoTHubMessage_GetByteArray");
        result = IOTHUBMESSAGE_ABANDONED;
    }
    else
    {
        char* tempBuffer = (char*)malloc(size + 1);
        if (tempBuffer == NULL)
        {
            Serial.println(" Error: failed to malloc");
            result = IOTHUBMESSAGE_ABANDONED;
        }
        else
        {
            result = IOTHUBMESSAGE_ACCEPTED;
            (void)memcpy(tempBuffer, buffer, size);
            
            String messageStringFull((char*)tempBuffer);
            String messageString = "UNKNOWN";
            messageString = messageStringFull.substring(0,size);

/*            if (messageString.startsWith("OTA")) {
                  String fullURL = messageString.substring(messageString.indexOf("://") - 4);;
                  // t_httpUpdate_return OTAStatus = OTA.update(fullURL.c_str());
                  // if we do OTA, then we never return the IOTHUBMESSAGE_ACCEPTED and we have issues
            }*/
            
            String messageProperties = "";
            MAP_HANDLE mapProperties = IoTHubMessage_Properties(message);
            if (mapProperties != NULL)
            {
            const char*const* keys;
            const char*const* values;
            size_t propertyCount = 0;
            if (Map_GetInternals(mapProperties, &keys, &values, &propertyCount) == MAP_OK)
                {
                if (propertyCount > 0)
                    {
                    size_t index;
                    for (index = 0; index < propertyCount; index++)
                        {
                            messageProperties += keys[index];
                            messageProperties += "=";
                            messageProperties += values[index];
                            messageProperties += ",";
                        }
                    }
                }
            }

            Serial.print(" Message Id: ");
            Serial.print(messageId);
            Serial.print(" Received. Message: \"");
            Serial.print(messageString);
            Serial.print("\", Properties: \"");
            Serial.print(messageProperties);
            Serial.println("\"");
        }
        free(tempBuffer);
    }
    return result;
}

void initIoTHub() {
  iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(connectionString, MQTT_Protocol);
  if (iotHubClientHandle == NULL)
  {
      (void)printf("ERROR: Failed on IoTHubClient_LL_Create\r\n");
  } else {
    IoTHubClient_LL_SetMessageCallback(iotHubClientHandle, IoTHubMessageCallback, NULL);
  }
}

void LEDOn() {
  digitalWrite(LED_BUILTIN, LOW);
}

void LEDOff() {
  digitalWrite(LED_BUILTIN, HIGH);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Dryer Tester Initializing");

  //Start the dht sensor.
  dht.begin();

  //Initialize the core variables.
  dryerRunning = false;
  sleepInterval = DRYER_NOT_RUNNING_SLEEP_INTERVAL;

  timeInterval = 0;

  //Get the current temp upon startup so we have a baseline to work from.
  lastTemp = dht.readTemperature(true);
  printTemp(lastTemp);


  //Initialize the wifi which will subsequently get the current time and also create the connection to the iot hub.
  initWifi();
  pinMode(LED_BUILTIN, OUTPUT);
  LEDOff();
}

void loop() {
  //increase our time interval.
  timeInterval++;

  //If we surpassed 100, reset it to 1 just to keep things simple.
  if (timeInterval > 100)
  {
	  timeInterval = 1;
  }

  // always checking the WiFi connection
  initWifi(); 

  //Turn the LED on.
  LEDOn();

  // we will process every message in the Hub
  while ((IoTHubClient_LL_GetSendStatus(iotHubClientHandle, &status) == IOTHUB_CLIENT_OK) && (status == IOTHUB_CLIENT_SEND_STATUS_BUSY))
  {
      IoTHubClient_LL_DoWork(iotHubClientHandle);
      ThreadAPI_Sleep(1000);
  }

  //Get the current temperature.
  float currentTemp = dht.readTemperature(true);
  printTemp(currentTemp);

  //If the dryer is not running.
  if (dryerRunning == false)
  {
	  //Check to see if the temperature differential has changed enough that the dryer should be running.
	  if (currentTemp - lastTemp > DRYER_STARTED_TEMP_DIFF)
	  {
		  Serial.println("Dryer is running!");
		  //Set the dryer running to true.
		  dryerRunning = true;

		  //Capture the temperature when we detected the change.
		  dryerRunningTemp = currentTemp;
		  //Change the sleep timer to read more often.
		  sleepInterval = DRYER_RUNNING_SLEEP_INTERVAL;
	  }
  }
  else //dryer is running
  {
	  //Check to see if our current temperature is within 2 degree of when we determined the dryer was
	  //running.
	  if (currentTemp - dryerRunningTemp <= DRYER_STOPPED_TEMP_DIFF)
	  {
		  Serial.println("Dryer has stopped!");
		  sendMessage("Dryer has stopped!!");
		  dryerRunning = false;
		  sleepInterval = DRYER_NOT_RUNNING_SLEEP_INTERVAL;
	  }
  }

  //Store the current temperature so we have it for the next cycle.
  lastTemp = currentTemp;


  //Turn the LED off.
  LEDOff();
  
  //Sleep for the designed interval.
  delay(sleepInterval);
}

void printTemp(float temp)
{
	Serial.print(timeInterval);
	Serial.print(" Current Temp: ");
	Serial.print(temp);
	Serial.print(" Dryer Running: ");
	Serial.println(dryerRunning);
}
