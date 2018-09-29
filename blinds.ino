/******************************************************************************************************
 * Wifi blind controller for ESP8266.
 * 
 * This project was created to open and close blinds.  
 * The design of this sketch is to control blinds (up to 3 in a group) locally or via Openhab.
 * Local control will give you 4 modes of movment (CloseUp(180), Open(90), Half(45), Close(0)).
 * 
 * This version of the sketch adds the ESP WIFI Configuration manager.  You no longer need to hardcode 
 * settings into the code (Although defaluts can be definied).
 * The configuration portal is started with SSID "Wblind-AP" at IP address "192.168.4.1" (default). 
 * The settings are stored on the ESP in non-volitile flash FS. 
 * If you need to enter back into the WIFI Configuration manager, you can press the "flash" button while 
 * the device is powered on and the ESP will reset and start the configuration portal. You will have to save
 * the settings in order to have the ESP restart normally as a hidden value is set in the flash filesystem.
 * If the ESP does not restart after saving settings, you will have to press the reset button. There seems 
 * to be a bug in the "ESP.reset()" function which hangs the ESP watchdog timer.
 * 
 * Also added in the sketch is the ability to tweek the tilt in software so that server setting at 90 
 * degrees will have the blind open at a perfect 90.
 * Also enable/disable blinds if you only have 1 or 2 blinds in a group.
 * 
 * I am using Masquito as the MQTT broker (any will work) and OpenHab as the backend.
 * See the readme.md for more info on the project and source files for Openhab and the blind server mounts.
 * 
 * Arduino IDE settings:
 *   Arduino Core ESP8266 - 2.3.0
 *   Wifi Manager         - 0.12.0
 *   Board: NodeMCU 1.0
 *          Flash: 4mbit (1M SPIFFS)
 *          Freq:  80mhz
 *          Baud:  115200
 *          
 *   Needed Pins on NodeMCU board vs bare ESP:
 *   ESP      NodeMCU   Usage:
 *   ----------------------------------------------------
 *   Reset      RST     Reset pin
 *    0         D3      Trigger Configuration manager
 *    4         D2      Pull cord blind toggle switch (Local control)
 *    12        D6      Servo data pin for Blind2
 *    13        D7      Servo data pin for Blind1
 *    14        D5      Servo data pin for Blind3
 *    
 * Updates:
 * 07/05/2016 - Inital public release
 * 01/22/2018 - Added Wifi manager
 *              A way to adjust the open tilt angle when working with blind clusters
 *              Code cleanup
 * 03/31/2018 - Renamed "mqtt_token" to "mqtt_client" per feedback from community. 
 *              Fixed spelling errors in comments.
 *              
 * 09/29/2018 - Added debounce to config pin and toggle pin.
 *              
 ******************************************************************************************************/
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Servo.h>
#include <PubSubClient.h>
#include <TimeLib.h>
#include <TimeAlarms.h>
#include <Bounce2.h>

/* Define ESP8266 PIN assignments */
//ADC_MODE(ADC_VCC);          /* Setup ADC pin to read VCC voltage (broken in current CC */
#define CONFIG_PIN  0         /* Pin used to put the esp into configuration portal */
#define BUTTON_PIN  4         /* Pin used as the pull cord to move blinds locally */
/* Configure GPIO servo pins */
#define SERVOPIN1 13          /* Pin for servo1 */
#define SERVOPIN2 12          /* Pin for servo2 */
#define SERVOPIN3 14          /* Pin for servo3 */

/* ENABLE DEBUG */
int debug = 1;            //Set this to 1 for serial debug output

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40]    = "192.168.10.155"; // MQTT Server address
char Custom_SSID[12]    = "Wblind-AP";      // SSID for Management portal when in config mode.
char mqtt_port[6]       = "1883";           // MQTT Port number
char mqtt_client[34]     = "MQTT_Client";   // MQTT client name
char topic_setting[40]  = "setting";        // MQTT SUBSCRIBE: Blind callback for control from openhab.
char topic_status[40]   = "status";         // MQTT PUBLISH: Blind sends current position (local control). 
char enable_blind1[2]   = "1";              // 1=Enable 0=Disable blind1
char enable_blind2[2]   = "1";              // 1=Enable 0=Disable blind2
char enable_blind3[2]   = "1";              // 1=Enable 0=Disable blind3
char blind1_0_offset[3] = "90";             // +-90 degrees for blind1. Valid values 87-93.
char blind2_0_offset[3] = "90";             // +-90 degrees for blind2. Valid values 87-93.
char blind3_0_offset[3] = "90";             // +-90 degrees for blind3. Valid values 87-93.
char configure_flag[2]  = "0";              // 0=Used to start configuration portal. Do not change.

//flag for saving data
bool shouldSaveConfig = false;

/* Blind tilt settings */
const int OPEN = 90;                     //Global settings for open. Can change based on offset value.
const int HALF = 45;                     //Global settings for half way opened. Can change based on offset value.
const int CLOSE = 0;                     //Global settings for close in down position
const int CLOSEUP = 180;                 //Global setting to close in the up position
/* Initial Blind status */
String blind1 = "unknown";   /* Initial state of blind1 */

int button_value;                       //high or low button values
int next = 1;

//callback notifying us of the need to save config
void saveConfigCallback () {
  debug and Serial.println("Should save config");
  shouldSaveConfig = true;
}

Servo myservo1;                       // create servo object for servo1
Servo myservo2;                       // create servo object for servo2
Servo myservo3;                       // create servo object for servo3

WiFiClient espClient1;
PubSubClient client(espClient1);

// Instantiate a Bounce object
Bounce debouncer1 = Bounce(); //Config button
Bounce debouncer2 = Bounce(); //Blind toggle switch
 /*--------------------------------------------Setup-----------------------------------*/
void setup() {
  pinMode(CONFIG_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Setup the Bounce instance :
  debouncer1.attach(CONFIG_PIN);
  debouncer1.interval(5); // interval in ms
  debouncer2.attach(BUTTON_PIN);
  debouncer2.interval(10); // interval in ms
  
  Serial.begin(115200);
  debug and Serial.println();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  debug and Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      debug and Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        debug and Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          debug and Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_client, json["mqtt_client"]);
          strcpy(topic_setting, json["topic_setting"]);
          strcpy(topic_status, json["topic_status"]);
          strcpy(enable_blind1, json["enable_blind1"]);
          strcpy(enable_blind2, json["enable_blind2"]);
          strcpy(enable_blind3, json["enable_blind3"]);
          strcpy(blind1_0_offset, json["blind1_0_offset"]);
          strcpy(blind2_0_offset, json["blind2_0_offset"]);
          strcpy(blind3_0_offset, json["blind3_0_offset"]);
          strcpy(configure_flag, json["configure_flag"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_client("mqtt", "mqtt client", mqtt_client, 32);
  WiFiManagerParameter custom_topic_setting("blind1_sub", "mqtt blind1 sub", topic_setting, 40);
  WiFiManagerParameter custom_topic_status("blind1_pub", "mqtt blind1 pub", topic_status, 40);
  WiFiManagerParameter custom_enable_blind1("enable_blind1", "Enable blind 1", enable_blind1, 2);
  WiFiManagerParameter custom_enable_blind2("enable_blind2", "Enable blind 2", enable_blind2, 2);
  WiFiManagerParameter custom_enable_blind3("enable_blind3", "Enable blind 3", enable_blind3, 2);
  WiFiManagerParameter custom_blind1_0_offset("blind1_0_offset", "blind1 offset", blind1_0_offset, 3);
  WiFiManagerParameter custom_blind2_0_offset("blind2_0_offset", "blind2 offset", blind2_0_offset, 3);
  WiFiManagerParameter custom_blind3_0_offset("blind3_0_offset", "blind3 offset", blind3_0_offset, 3);
  WiFiManagerParameter custom_configure_flag("configure_flag", "configure flag", configure_flag, 2);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here for web portal
  WiFiManagerParameter custom_text1("<p>MQTT setup</p>");
  wifiManager.addParameter(&custom_text1);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_client);
  WiFiManagerParameter custom_text2("<p>Blind setup</p>");
  wifiManager.addParameter(&custom_text2);
  wifiManager.addParameter(&custom_topic_setting);
  wifiManager.addParameter(&custom_topic_status);
  WiFiManagerParameter custom_text3("<p>To enable a blind set value to 1.</p>");
  wifiManager.addParameter(&custom_text3);
  wifiManager.addParameter(&custom_enable_blind1);
  wifiManager.addParameter(&custom_enable_blind2);
  wifiManager.addParameter(&custom_enable_blind3);
  WiFiManagerParameter custom_text4("<p>Blind midpoint between 87 and 93. 90(Default) is midpoint</p>");
  wifiManager.addParameter(&custom_text4);
  wifiManager.addParameter(&custom_blind1_0_offset);
  wifiManager.addParameter(&custom_blind2_0_offset);
  wifiManager.addParameter(&custom_blind3_0_offset);
//  wifiManager.addParameter(&custom_configure_flag);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 15%
  wifiManager.setMinimumSignalQuality(15);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "Wblind-AP"
  //and goes into a blocking loop awaiting configuration
  debug and Serial.print("Configuration flag: ");
  debug and Serial.println(configure_flag);
  if (strcmp(configure_flag, "0") == 0 ) {  // If config flag is 0 then enter wifi setup.
    Serial.println("\nEntering Configurations portal");
    if (!wifiManager.startConfigPortal("Wblind-AP", "password")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }
  }
  //Sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(30);
  //We need to run autoconnect to actually connect to WIFI. 
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "Wblind-AP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(Custom_SSID, "password")) {
    Serial.println("Failed to connect timeout reached. Resetting ESP.");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  
  //if you get here you have connected to the WiFi
  debug and Serial.println("Connected.");
  debug and Serial.print("Local IP: ");
  debug and Serial.println(WiFi.localIP());
  
  //Print out retrieved/modified parameters to be stored to flash.
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_client, custom_mqtt_client.getValue());
  strcpy(topic_setting, custom_topic_setting.getValue());
  strcpy(topic_status, custom_topic_status.getValue());
  strcpy(enable_blind1, custom_enable_blind1.getValue());
  strcpy(enable_blind2, custom_enable_blind2.getValue());
  strcpy(enable_blind3, custom_enable_blind3.getValue());
  strcpy(blind1_0_offset, custom_blind1_0_offset.getValue());
  strcpy(blind2_0_offset, custom_blind2_0_offset.getValue());
  strcpy(blind3_0_offset, custom_blind3_0_offset.getValue());
  strcpy(configure_flag, custom_configure_flag.getValue());

  //Save the custom parameters to flash.
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_client"] = mqtt_client;
    json["topic_setting"] = topic_setting;
    json["topic_status"] = topic_status;
    json["enable_blind1"] = enable_blind1;
    json["enable_blind2"] = enable_blind2;
    json["enable_blind3"] = enable_blind3;
    json["blind1_0_offset"] = blind1_0_offset;
    json["blind2_0_offset"] = blind2_0_offset;
    json["blind3_0_offset"] = blind3_0_offset;
    json["configure_flag"] = "1";  //1=configured

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    debug and Serial.println("");
    //end save
  }

  /* print out our voltage */
  //for bare esp module: float vdd = ESP.getVcc() / 1000.0;  /* Read VCC voltage */
  //Nodemcu esp module. has resister pullup.
  //float vdd = ((float)ESP.getVcc())/1024;
  
  //debug and Serial.print("Voltage: ");
  //debug and Serial.println(vdd);

  moveblinds(CLOSEUP);         // call function to close all blinds on power up.
  blind1="CloseUP";
  Alarm.timerOnce(10, MQTT_RECONNECT);              // Connect to MQTT broker 10 seconds after startup.
  Alarm.timerRepeat(300, MQTT_RECONNECT);           // Check connection to MQTT broker every 5 minutes.
  Alarm.timerRepeat(900, MQTT_PUBLISH);             // Send Blind state every 15 minutes to broker.

  /* Print out variables */
  debug and Serial.println(mqtt_server);
  debug and Serial.println(mqtt_port);
  debug and Serial.println(mqtt_client);
  debug and Serial.println(topic_setting);
  debug and Serial.println(topic_status);
  debug and Serial.println(enable_blind1);
  debug and Serial.println(enable_blind2);
  debug and Serial.println(enable_blind3);
  debug and Serial.println(blind1_0_offset);
  debug and Serial.println(blind2_0_offset);
  debug and Serial.println(blind3_0_offset);

  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(callback);

}  //End setup


/*------------------------------------Functions------------------------------------------*/
/* Need to move to header */
void moveblinds(int value) {
  if (atoi(enable_blind1)) {
    delay(60);
    int b1_value=value;
    if (atoi(blind1_0_offset) != 90 && value == OPEN) {  // Set new open value. 
       b1_value = atoi(blind1_0_offset);
       debug and Serial.println("B1 here1");
    }
    if (atoi(blind1_0_offset) > 90 && value == HALF) {  // Calculate new half open value if offset is > 90.
       b1_value = value+(atoi(blind1_0_offset)-90);
       debug and Serial.println("B1 here2");
    }
    if (atoi(blind1_0_offset) < 90 && value == HALF) {  // Calculate new half open value if offset is < 90.
       b1_value = value-(90-atoi(blind1_0_offset));
       debug and Serial.println("B1 here3");
    }
    debug and Serial.print("Moving blind1 to: ");
    debug and Serial.println(b1_value);
    myservo1.attach(SERVOPIN1);
    myservo1.write(b1_value);
    delay(600);
    myservo1.detach();
  }
  if (atoi(enable_blind2)) {
    delay(60);
    int b2_value=value;
    if (atoi(blind2_0_offset) != 90 && value == OPEN) {  // Set new open value. 
       b2_value = atoi(blind2_0_offset);
       debug and Serial.println("B2 here1");
    }
    if (atoi(blind2_0_offset) > 90 && value == HALF) {  // Calculate new half open value if offset is > 90.
       b2_value = value+(atoi(blind2_0_offset)-90);
       debug and Serial.println("B2 here2");
    }
    if (atoi(blind2_0_offset) < 90 && value == HALF) {  // Calculate new half open value if offset is < 90.
       b2_value = value-(90-atoi(blind2_0_offset));
       debug and Serial.println("B2 here3");
    }
    debug and Serial.print("Moving blind2 to: ");
    debug and Serial.println(b2_value);
    myservo2.attach(SERVOPIN2);
    myservo2.write(b2_value);
    delay(600);
    myservo2.detach();
  }
  if (atoi(enable_blind3)) {
    delay(60);
    int b3_value=value;
    if (atoi(blind3_0_offset) != 90 && value == OPEN) {  // Set new open value. 
       b3_value = atoi(blind3_0_offset);
       debug and Serial.println("B3 here1");
    }
    if (atoi(blind3_0_offset) > 90 && value == HALF) {  // Calculate new half open value if offset is > 90.
       b3_value = value+(atoi(blind3_0_offset)-90);
       debug and Serial.println("B3 here2");
    }
    if (atoi(blind3_0_offset) < 90 && value == HALF) {  // Calculate new half open value if offset is < 90.
       b3_value = value-(90-atoi(blind3_0_offset));
       debug and Serial.println("B3 here3");
    }
    debug and Serial.print("Moving blind3 to: ");
    debug and Serial.println(b3_value);
    myservo3.attach(SERVOPIN3);
    myservo3.write(b3_value);
    delay(600);
    myservo3.detach();
  }
}

/* Messages from MQTT server */
void callback(char* topic, byte* payload, unsigned int length) {
  debug and Serial.print("Message arrived: [");
  debug and Serial.print(topic);
  debug and Serial.print(" ]");
  char *payload_string = (char *) payload; // convert mqtt byte array into char array
  payload_string[length] = '\0';           // Must delimit with 0
  int payload_int = atoi(payload_string);  // convert char array into int
  debug and Serial.println(payload_int);

  /* Take value from MQTT/OPENHAB and break up into open/half/close/closeup */
  if ((payload_int >= 70) && (payload_int <= 120)) {  //Open range
     moveblinds(OPEN);   // Call function to move the blinds
     client.publish(topic_status, "Open");
     blind1 = "Open";
  } else if ((payload_int >= 25) && (payload_int <= 69)) {  //Half range
     moveblinds(HALF);   // Call function to move the blinds
     client.publish(topic_status, "Half");
     blind1 = "Half";
  } else if ((payload_int >= 0) && (payload_int <= 24)) {  //Close range
     moveblinds(CLOSE);   // Call function to move the blinds 
     client.publish(topic_status, "Closed");
     blind1 = "Closed";
  } else if (payload_int >= 140) {
     moveblinds(CLOSEUP);   // Call function to move the blinds
     client.publish(topic_status, "Closedup");
     blind1 = "Closedup";
  } else {
     client.publish(topic_status, "error");
     debug and Serial.println("Unknown setting in payload");
  }
}

void MQTT_PUBLISH() {
  client.publish(topic_status, (char *) blind1.c_str());
}

void MQTT_RECONNECT() {
  debug and Serial.println("Check MQTT connection...");
  if (!client.connected()) {
    if (client.connect(mqtt_client)) {
      debug and Serial.println(" Connected");
      client.subscribe(topic_setting);   // Attempt to subscribe to topic
      MQTT_PUBLISH();                   // Publish blind status if we have to reconnect because server may not maintain state.
    } else {
      debug and Serial.print("failed, rc=");
      debug and Serial.println(client.state());
    }
  } else {
    debug and Serial.println(" Already Connected");
  }
}


/*---------------------------------main loop---------------------------------------*/
void loop() {
  client.loop();  //refresh mqtt
  debouncer1.update(); //Update Debounce1 for Config pin
  debouncer2.update(); //Update Debounce2 for blind toggle switch
  //If we press config button clear out wifi settings so we enter config mode
  if ( debouncer1.read() == LOW ) {
    Serial.println("\n Entering WIFI setup.\n");
    delay(2000);
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_client"] = mqtt_client;
    json["topic_setting"] = topic_setting;
    json["topic_status"] = topic_status;
    json["enable_blind1"] = enable_blind1;
    json["enable_blind2"] = enable_blind2;
    json["enable_blind3"] = enable_blind3;
    json["blind1_0_offset"] = blind1_0_offset;
    json["blind2_0_offset"] = blind2_0_offset;
    json["blind3_0_offset"] = blind3_0_offset;
    json["configure_flag"] = "0";
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
    delay(500);
    ESP.reset();
  }  /* End setup check */
  
  button_value = debouncer2.read(); //Query blind toggle switch
  //Define the modes based on button press. Cycle through options
  if (button_value == LOW) { // button press
    debug and Serial.println("button pressed: ");
    if (next > 4) {
      next = 1;
    }
    if (next == 1) { //First pull is OPEN
      moveblinds(OPEN);  //Move to next position
      blind1 = "Open";
      client.publish(topic_status, "Open");   // publish current position
      //client.publish(topic_status, blind1.c_str());   // publish current position
      //MQTT_PUBLISH();     // publish current position
    }
    if (next == 2) { //Second pull is HALF
      moveblinds(HALF);
      blind1 = "Half";
      client.publish(topic_status, "Half");   // publish current position
      //client.publish(topic_status, blind1.c_str());   // publish current position
      //MQTT_PUBLISH();      // publish current position
    }
    if (next == 3) { //Third pull is CLOSE
      moveblinds(CLOSE);
      blind1 = "Close";
      client.publish(topic_status, "Closed");   // publish current position
      //client.publish(topic_status, blind1.c_str());   // publish current position
      //MQTT_PUBLISH();     // publish current position
    }
    if (next == 4) { //Forth pull is CLOSEUP
      moveblinds(CLOSEUP);
      blind1 = "Closeup";
      client.publish(topic_status, "Closedup");   // publish current position
      //client.publish(topic_status, blind1.c_str());   // publish current position
      //MQTT_PUBLISH();    // publish current position
    }
    next++;
  }
  Alarm.delay(20);
  ESP.wdtFeed();
}
