/*

 on Power1#State=1 do SerialSend5 A00101A2 endon
 on Power1#State=0 do SerialSend5 A00100A1 endon

 on Power2#State=1 do SerialSend5 A00201A3 endon
 on Power2#State=0 do SerialSend5 A00200A2 endon

 on Power3#State=1 do SerialSend5 A00301A4 endon
 on Power3#State=0 do SerialSend5 A00300A3 endon

 on Power4#State=1 do SerialSend5 A00401A5 endon
 on Power4#State=0 do SerialSend5 A00400A4 endon

*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

/* -------------------------------------------------CONFIG-------------------------------------------------------------*/

// uncomment for debugging; this will give debug output to Serial out instead of relay commands
//#define DEBUG

// uncomment to force the fact that only one relay can be active at the same time
// this has NO EFFECT on relay triggers!!
#define EXCLUSIVE

// messages that are send to the MQTT bus as 'on' and 'off' state
// must be of type char*
#define ON "1"
#define OFF "0"
#define TRIGGERED "triggered"

// length of a trigger in milliseconds (on->off or off-> on)
#define TRIGGER_LENGTH 200



// Secrets are defined in secrets.h
// Must include
// - const char *ssid = "";
// - const char *password = "";
// - const char *mqtt_server = "";


/* ---------------------------------------------------------------------------------------------------------------------*/


/*
Define global variables
*/

// WiFi client
WiFiClient espClient;

// MQTT client
PubSubClient client(espClient);

// WiFi event handlers
WiFiEventHandler gotIpEventHandler, disconnectedEventHandler, connectedEventHandler;

// Relay states
byte STATE = 0x00;

/*
Function definitions
*/

/**
 * id = value between 1 and # relays (max 8)
 */

void consoleLn(const char *s)
{
#ifdef DEBUG
    Serial.println(s);
#endif
}

void consoleLn(String s)
{
#ifdef DEBUG
    Serial.println(s);
#endif
}

void consoleLn(int s)
{
#ifdef DEBUG
    Serial.println(s);
#endif
}

void console(const char *s)
{
#ifdef DEBUG
    Serial.print(s);
#endif
}

void console(String s)
{
#ifdef DEBUG
    Serial.print(s);
#endif
}

void console(int s)
{
#ifdef DEBUG
    Serial.print(s);
#endif
}

void getRelayCommand(byte id, bool on, byte *cmd)
{
    cmd[0] = 0xA0;
    cmd[1] = id;
    if (on)
        cmd[2] = 0x01;
    else
        cmd[2] = 0x00;
    if (on)
        cmd[3] = 0xA1 + id;
    else
        cmd[3] = 0xA0 + id;
}

// Checks if it is ok to turn this relay on (depends on the EXCLUSIVE flag)
bool canActivateRelay(byte id, bool s) {
    // if not in exclusive mode, return true
    #ifndef EXCLUSIVE
        return true;
    #endif
    // if we are deactivating a relay, return true
    if (!s) return true;
    // if we have 0 active relays, return true
    if (STATE == 0) return true;
    // if we are turning off the one active relay, return true, otherwise return false
    byte m = 0x01 << (id - 1);
    return (m == STATE);
}

// Returns true if we sent out a relay command
bool setRelayState(byte id, bool s)
{
    if (id < 1 || id > 8)
        return false;
    if (!canActivateRelay(id, s))
        return false;
    // create a bit mask in the right position
    byte m = 0x01 << (id - 1);

    // switch the relay
    #ifndef DEBUG
        byte cmd[4];
        getRelayCommand(id, s, cmd);
        Serial.write(cmd, 4);
        delay(10);
    #endif

    // set the current state
    if (s)
    {
        STATE = STATE | m; // set bit to 1 at position id / m
    }
    else
    {
        STATE = STATE & ~m; // clear bit at position id / m
    }
    return true;
}

bool getRelayState(byte id)
{
    if (id < 1 || id > 8)
        return false;
    // create a bit mask in the right position
    byte m = 0x01 << (id - 1);
    // extract the current state
    return ((STATE & m) > 0);
}

bool triggerRelay(byte id, bool s)
{
    bool cs = getRelayState(id);
    // perform the trigger only if the current state is different from the requested trigger
    if (cs^s) {
        setRelayState(id, s);
        delay(TRIGGER_LENGTH);
        setRelayState(id, cs);
        return true;
    } else {
        return false;
    }
}

String getId()
{
    String id = WiFi.macAddress();
    id.replace(":", "");
    return id;
}

void publishMeta()
{
    client.publish("relay/status/id", getId().c_str());
    String t = "relay/status/ID/IP";
    t.replace("ID", getId());
    client.publish(t.c_str(), WiFi.localIP().toString().c_str());
}

/*
Establish the WiFi connection
*/
void initWiFi()
{
    WiFi.forceSleepWake();
    delay(1);
    // Disable the WiFi persistence.  The ESP8266 will not load and save WiFi settings in the flash memory.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    consoleLn("Connecting to WiFi...");

    while (WiFi.status() != WL_CONNECTED)
    {
        console(".");
        delay(1000);
    }
}

/*
Connect to the MQTT server
*/
void connectMQTT()
{

    // Loop until we're reconnected
    while (!client.connected())
    {

        consoleLn("Attempting MQTT connection...");

        // Attempt to connect
        if (client.connect(getId().c_str()))
        {
            consoleLn("MQTT connected");
            // Subscribe
            client.subscribe("relay/#");
            // Publish our presence
            publishMeta();
        }
        else
        {
            console("failed, rc=");
            console(client.state());
            consoleLn(", will try again in 30 seconds");
            // Wait 30 seconds before retrying
            delay(30000);
        }
    }
}

/*
Handle incoming MQTT messages
*/
void MQTTCallback(char *_topic, byte *message, unsigned int length)
{
    console("Message arrived on topic: ");
    console(_topic);
    console(". Message: ");
    String fullMessage;
    String numericMessage;

    // Get the topic and turn it into uppercase
    String topic = String(_topic);
    topic.toUpperCase();

    // Get the last character of _topic and turn it into a number
    // relay/set/ID/[0-9]
    byte relayId = _topic[topic.length() - 1] - '0';

    // replace the ID in the topic with MATCH if it matches our ID
    String ID = getId();
    topic.replace(ID, "MATCH");

    // Get String value of the message
    for (unsigned int i = 0; i < length; i++)
    {
        fullMessage += (char)message[i];
        // if this is a "digit" convert the incoming byte to a char and add it to the string
        if (isDigit(message[i]))
            numericMessage += (char)message[i];
    }
    fullMessage.toUpperCase();
    consoleLn(fullMessage);

    if (topic.equals("RELAY/GET/ID"))
    {
        publishMeta();
    }
    else if (topic.startsWith("RELAY/SET/MATCH/") && topic.length() == 17)
    {
        if (relayId < 0 || relayId > 8)
            return;
        bool on = (fullMessage.equals("ON") || fullMessage.equals("TRUE") || fullMessage.equals("1"));
        bool off = (fullMessage.equals("OFF") || fullMessage.equals("FALSE") || fullMessage.equals("0"));
        if (!on && !off)
            return;

        console("Set relay ");
        console(relayId);
        console(" to ");
        on ?  consoleLn("ON") : consoleLn("OFF");

        // switch the relay
        bool cmdSent = setRelayState(relayId, on);

        // respond with an MQTT message, if we we sent out a relay command
        if (cmdSent)
        {
            String t = "relay/status/ID/";
            t.replace("ID", ID);
            t.concat(relayId);
            client.publish(t.c_str(), on ? ON : OFF);
        }
    }
    else if (topic.startsWith("RELAY/TRIGGER/MATCH/") && topic.length() == 21)
    {
        if (relayId < 0 || relayId > 8)
            return;
        bool on = (fullMessage.equals("ON") || fullMessage.equals("TRUE") || fullMessage.equals("1"));
        bool off = (fullMessage.equals("OFF") || fullMessage.equals("FALSE") || fullMessage.equals("0"));
        if (!on && !off)
            return;

        console("Trigger relay ");
        console(relayId);
        console(" to ");
        on ?  consoleLn("ON") : consoleLn("OFF");

        // triiger the relay
        bool triggered = triggerRelay(relayId, on);

        // respond with 2 MQTT messages, if triggered
        if (triggered) {
            String t = "relay/status/ID/";
            t.replace("ID", ID);
            t.concat(relayId);
            // notify that we were triggered
            client.publish(t.c_str(), TRIGGERED);
            // notify the end state (which was the start state)
            client.publish(t.c_str(), on ? OFF : ON);
        }
    }
    else if (topic.startsWith("RELAY/GET/MATCH/") && topic.length() == 17)
    {
        if (relayId < 0 || relayId > 8)
            return;

        // respond with an MQTT message
        String t = "relay/status/ID/";
        t.replace("ID", ID);
        t.concat(relayId);
        client.publish(t.c_str(), getRelayState(relayId) ? ON : OFF);
    }
}

void setup()
{

    Serial.begin(115200);

    // Reset all the relays to off
    for (byte id = 0x01; id < 0x09; id++)
    {
        setRelayState(id, false);
    }

    // WiFi event handlers
    connectedEventHandler = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &event)
                                                        { consoleLn("Connected to AP successfully!"); });

    gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event)
                                                {
        consoleLn("WiFi connected");
        console("IP address: ");
        consoleLn(WiFi.localIP()); });

    disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event)
                                                              {
        consoleLn("Disconnected from WiFi access point");
        console("WiFi lost connection. Reason: ");
        consoleLn(event.reason);
        consoleLn("Trying to Reconnect");
        initWiFi(); });

    // Delete old WiFi config and start WiFi
    WiFi.disconnect(true);
    delay(1000);
    initWiFi();

    // Init MQTT
    client.setServer(mqtt_server, 1883);
    client.setCallback(MQTTCallback);
}

void loop()
{
    if (!client.connected())
    {
        connectMQTT();
    }
    client.loop();
}