using System;
using Twilio;

public static void Run(string myIoTHubMessage, TraceWriter log)
{
    log.Info($"C# IoT Hub trigger function processed a message: {myIoTHubMessage}");
    string accountSid = "<Twilio Account ID>";
    string authToken = "<Twilio Auth Token>";

    var client = new TwilioRestClient(accountSid, authToken);

    client.SendMessage(
        "<Twilio Number>", // Insert your Twilio from SMS number here
        "<Recipient Cell Phone Number>", // Insert your verified (trial) to SMS number here
        DateTime.Now + " " + myIoTHubMessage
    );
}
