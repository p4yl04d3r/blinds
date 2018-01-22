# blinds
 Wifi blind controller for ESP8266.

  This project was created to open and close blinds.  
  The design of this sketch is to control blinds (up to 3 in a group) locally or via Openhab.
  Local control will give you 4 modes of movment (CloseUp(180), Open(90), Half(45), Close(0)).
  
  This version of the sketch adds the ESP WIFI Configuration manager.  You no longer need to hardcode 
  settings into the code (Although defaluts can be definied).
  The configuration portal is started with SSID "Wblind-AP" at IP address "192.168.4.1" (default). 
  The settings are stored on the ESP in non-volitile flash FS. 
  If you need to enter back into the WIFI Configuration manager, you can press the "flash" button while 
  the device is powered on and the ESP will reset and start the configuration portal. You will have to save
  the settings in order to have the ESP restart normally as a hidden value is set in the flash filesystem.
  If the ESP does not restart after saving settings, you will have to press the reset button. There seems 
  to be a bug in the "ESP.reset()" function which hangs the ESP watchdog timer.
  
  Also added in the sketch is the ability to tweek the tilt in software so that server setting at 90 
  degrees will have the blind open at a perfect 90.
  Also enable/disable blinds if you only have 1 or 2 blinds in a group.
  
  I am using Masquito as the MQTT broker (any will work) and OpenHab as the backend.

The Servo mount is designed to fit inside of 2" venetian blinds.
  Thingivers Blind server mount: https://www.thingiverse.com/thing:1573867
The gears work with 6mm square rod.
  Thingivers Blind gears: https://www.thingiverse.com/thing:2766199

You must remove the orginal tilt or twist mechanisim to allow the blinds to be controlled via a servo.
