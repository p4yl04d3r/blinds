/*
 * Garage door controller in arduino
 * Board ESP12-E NodeMCU 1.0
 */

#include <Servo.h>  
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

/* Setup number of servos */
#define SERVO1
//#define SERVO2
//#define SERVO3
/* ENABLE DEBUG */
int debug = 1;            //Set this to 1 for serial debug output
/* Configure GPIO servo pins*/
const int servoPin1=5;     //Pin for servo1
//const int servoPin2=12;     //Pin for servo2
//const int servoPin3=14;     //Pin for servo3
const int button_pin=4 ;  //Button or relay driven by x10 switch for manual control
/* WIFI setup */
const char* ssid     = "<ssid>";           // WIFI SSID
const char* password = "<password>";       // WIFI password   
/* MQTT setup */
const char* mqtt_server ="192.168.10.155";
const char* topic_setting = "OpenHab/blind1/setting";
const char* topic_status = "OpenHab/blind1/status";

/* Blind tilt settings */
const int OPEN=90;       //Global settings for open
const int HALF=45;       //Global settings for half way opened
const int CLOSE=0;       //Global settings for close in down position
const int CLOSEUP=180;   //Global setting to close in the up position
/***********************************************************************************/
WiFiClient espClient;
PubSubClient client(espClient);

Servo myservo;     // create servo object to control a servo 
int button_value;  //high or low button values
int next=1;

void setup(void) {
  Serial.begin(9600); //Comment out this line if you don't want debugging enabled
  pinMode(button_pin, INPUT_PULLUP);
  setup_wifi();
  delay(100);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  moveblinds(CLOSEUP);         // call function to close all blinds
  client.connect("ESPBlind1");
  client.subscribe(topic_setting);
  client.publish(topic_status, "Closed");   // publish current position at powerup
}

void setup_wifi() {
  // We start by connecting to a WiFi network
  debug and Serial.println();
  debug and Serial.print("Connecting to ");
  debug and Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  delay(20);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debug and Serial.print(".");
  }
  debug and Serial.println("");
  debug and Serial.println("WiFi connected");
  debug and Serial.print("IP address: ");
  debug and Serial.println(WiFi.localIP());
}

void moveblinds(int value) {
#ifdef SERVO1
        delay(60);
        debug and Serial.print("Moving blind1 to: ");
        debug and Serial.println(value);
        myservo.attach(servoPin1);
        yield();
        myservo.write(value);
        delay(600); 
        myservo.detach();
#endif
#ifdef SERVO2
        delay(60);
        debug and Serial.print("Moving blind2 to: ");
        debug and Serial.println(value);
        myservo.attach(servoPin2);
        myservo.write(value);
        delay(600); 
        myservo.detach();
#endif
#ifdef SERVO3
        delay(60);
        debug and Serial.print("Moving blind3 to: ");
        debug and Serial.println(value);
        myservo.attach(servoPin3);
        myservo.write(value);
        delay(600); 
        myservo.detach();
#endif     
}

void callback(char* topic, byte* payload, unsigned int length) {
  debug and Serial.print("Message arrived: [");
  debug and Serial.print(topic);
  debug and Serial.print(" ]");
  char *payload_string = (char *) payload; // convert mqtt byte array into char array
  payload_string[length] ='\0';            // Must delimit with 0
  int payload_int=atoi(payload_string);    // convert char array into int
  debug and Serial.println(payload_int);

  //Adjust blind to setting of payload if between the full sweep of the servo.
  if ((payload_int >= 0) && (payload_int <= 180)) {
     moveblinds(payload_int);   // Call function to move the blinds
     /* MQTT status ranges */
     if ((payload_int >= 70) && (payload_int <= 120)) {  //Open range
        client.publish(topic_status, "Open");
     } else if ((payload_int >= 25) && (payload_int <= 69)) {  //Half range
        client.publish(topic_status, "Half");
     } else if ((payload_int >= 0) && (payload_int <= 24)) {  //Close range 
        client.publish(topic_status, "Closed");
     } else if (payload_int >= 140) {
         client.publish(topic_status, "Closedup");
     } else {
        client.publish(topic_status, "error");
        debug and Serial.println("Unknown setting in payload");
     }
  }
}

void reconnect() {
  // Loop until we're reconnected
  //while (!client.connected()) {   //comment out so we go through main loop for manual control
    debug and Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESPBlind1")) {
      debug and Serial.println("connected");
      client.subscribe(topic_setting);
    } else {
      debug and Serial.print("failed, rc=");
      debug and Serial.print(client.state());
      debug and Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(50);
    }
  //} 
}

//MAIN LOOP
void loop(void) { 
    if (!client.connected()) {
       delay(10);
       reconnect();
    }
    client.loop();
    button_value = digitalRead(button_pin); //Query button or relay switch
    //Define the modes based on button press. Cycle through options
    if(button_value==LOW){  // button press
      debug and Serial.println("button pressed: "); 
      if (next > 4) {
        next=1;
      }
      if (next == 1) { //First pull is OPEN
        moveblinds(OPEN);  //Move to next position
        client.publish(topic_status, "Open");   // publish current position
      } 
      if (next == 2) { //Second pull is HALF
        moveblinds(HALF);
        client.publish(topic_status, "Half");   // publish current position
      } 
      if (next == 3) { //Third pull is CLOSE 
        moveblinds(CLOSE);          
        client.publish(topic_status, "Closed");   // publish current position          
      } 
      if (next == 4) { //Forth pull is CLOSEUP 
        moveblinds(CLOSEUP);          
        client.publish(topic_status, "Closedup");   // publish current position          
      }
      next++;
   }
   delay(60);
}
