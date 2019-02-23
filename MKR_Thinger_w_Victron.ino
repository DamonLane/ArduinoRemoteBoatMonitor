#include "arduino_secrets.h"
// library name              // for...
#include <MKRGSM.h>          // GSM communication, from Arduino
#include <ThingerMKRGSM.h>   // thinger.io, from thinger.io
#include <DHT.h>             // temperature sensor, from Adafruit
#include <ArduinoLowPower.h> // sleep, from Arduino
#include <SPI.h>             // reading battery monitor over TTL/serial

#define GPRS_APN "h2g2" // Get onto Google Fi network

// Thinger.io credentials
#define USERNAME "SECRET_THINGER_USERNAME"
#define DEVICE_ID "SECRET_THINGER_DEVICE_ID"
#define DEVICE_CREDENTIAL "SECRET_THINGER_DEVICE_CREDENTIAL"
ThingerMKRGSM thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

// DHT config
#define DHTPIN 2 // digital pin sensor is connected to
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Water sensors
#define highBilge 5 // digital pin the sensor is connected to
#define lowBilge 7 // digital pin the sensor is connected to
#define engineRoom 8 // digital pin the sensor is connected to

// Setting up Victron battery monitor variables
char p_buffer[80];
#define P(str) (strcpy_P(p_buffer, PSTR(str)), p_buffer)

char c;
String V_buffer;

float Current;
float Voltage;
float SOC;
float TTG;
float CE;
int Alarm_low_voltage;
int Alarm_high_voltage;
int Alarm_low_soc;
String Alarm;
String Relay;

boolean endpointRateLimiter = 1;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup");

  // Victron Battery Monitor
  // DO NOT CONNECT POWER OR GROUND WIRES, the BMV is not isolated,
  // the official Victron cable is isolating for this reason
  // The Victron uses 3.3V TTL, so the MKR boards 3.3V circuit is perfect
  
  // initialize serial communication with the Victron BMV at 19200 bits per second (per jepefe):
  // Serial1 is a hardware serial port in pins 13 and 14 in the MKR series
  Serial1.begin(19200); // In jepefe's code, for Arduinos without hardware serial, this would be Victron.begin(19200) 
                        // and Victron was defined above by its pins and SoftwareSerial. Apparently it has been deprecated in favor of NewSoftSerial
                        // this website has plain and direct language about this, which is rarely discussed in discussions I have seen about 
                        // downstream use of the serials. https://www.pjrc.com/teensy/td_libs_SoftwareSerial.html

  thing.set_apn(GPRS_APN);

  // set builtin led to output and turn it off
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Thinger.io resources
  thing["led"] << [](pson & in) {
    digitalWrite(LED_BUILTIN, in ? HIGH : LOW);
  };

  // pin control example over internet (i.e. turning on/off a light, a relay, etc)
  //thing["relay"] << digitalPin(7); //change this to the right pin when connecting the relay

  //Temperature and RH sensor setup
  dht.begin();

  // Wake on water detected
  LowPower.attachInterruptWakeup(highBilge, NULL, LOW);
  LowPower.attachInterruptWakeup(engineRoom, NULL, LOW);
  LowPower.attachInterruptWakeup(lowBilge, NULL, LOW);

  delay(15000); //give a chance to reprogram
  Serial.println("end of setup");
}

void loop() {

  digitalWrite(LED_BUILTIN, LOW); // turn LED back off when returning to loop
  
  thing.handle();
  Serial.println("thing.handle");

  // check for high water, call endpoint to send email if detected (call once)
  if ((digitalRead(highBilge) == LOW
      || digitalRead(lowBilge) == LOW
      || digitalRead(engineRoom) == LOW)
      && endpointRateLimiter) {
    digitalWrite(LED_BUILTIN, HIGH);
    pson data;
    data["water high bilge"] = digitalRead(highBilge);
    data["water low bilge"] = digitalRead(lowBilge);
    data["water engine room"] = digitalRead(engineRoom);
    thing.call_endpoint("WaterDetectedEmail", data);
    endpointRateLimiter = 0;
  }
  else digitalWrite(LED_BUILTIN, LOW);

  // these declarations are not needed for DHT or thinger, but I'm
  // using them to get integers and avoid using data for precision
  // I don't need (and probably isn't accurate without sampling anyway)
  int humidity = dht.readHumidity();
  //int celsius = dht.readTemperature();
  int fahrenheit = dht.readTemperature(true);

    // Victron BMV code from: http://www.jw5zla.com/?p=7 (adapted for the MKR's hardware Serial1 which most Arduinos lack at pins (13,14))
    if (Serial1.available()) {
    c = Serial1.read();

    Serial.println(c);
    
    if (V_buffer.length() <80) {
      V_buffer += c;
    }

    if (c == '\n') {  // New line.

      if (V_buffer.startsWith("I")) {
        String temp_string = V_buffer.substring(V_buffer.indexOf("\t")+1);
        double temp_int = temp_string.toInt();
        Current = (float) temp_int/1000;
      }

      if (V_buffer.startsWith("V")) {
        String temp_string = V_buffer.substring(V_buffer.indexOf("\t")+1);
        int temp_int = temp_string.toInt();
        Voltage = (float) temp_int/1000;
      }

      if (V_buffer.startsWith("SOC")) {
        String temp_string = V_buffer.substring(V_buffer.indexOf("\t")+1);
        int temp_int = temp_string.toInt();
        SOC = (float) temp_int/10;
      }

      if (V_buffer.startsWith("TTG")) {
        String temp_string = V_buffer.substring(V_buffer.indexOf("\t")+1);
        double temp_int = temp_string.toInt();
        if (temp_int >0) {
          TTG = (float) temp_int/60;
        }
        else {
          TTG = 240;
        }
      }

      if (V_buffer.startsWith("CE")) {
        String temp_string = V_buffer.substring(V_buffer.indexOf("\t")+1);
        double temp_int = temp_string.toInt();
        CE = (float) temp_int/1000;
      }

      if (V_buffer.startsWith("Alarm")) {
        Alarm = V_buffer.substring(V_buffer.indexOf("\t")+1);
        Alarm.trim();
      }

      if (V_buffer.startsWith("Relay")) {
        Relay = V_buffer.substring(V_buffer.indexOf("\t")+1);
        Relay.trim();
      }

      if (V_buffer.startsWith("AR")) {
        String temp_string = V_buffer.substring(V_buffer.indexOf("\t")+1);
        int temp_int = temp_string.toInt();

        if (bitRead(temp_int,0)) {
          Alarm_low_voltage = 1;
        }
        else {
          Alarm_low_voltage = 0;
        }

        if (bitRead(temp_int,1)) {
          Alarm_high_voltage = 1;
        }
        else {
          Alarm_high_voltage = 0;
        }

        if (bitRead(temp_int,2)) {
          Alarm_low_soc = 1;
        }
        else {
          Alarm_low_soc = 0;
        }
      }
      Serial.println(Current);
      Serial.println(Voltage);
      Serial.println(SOC);
      V_buffer="";
    }
    }
  
  pson data;
  data["BMV:Current"] = Current;
  data["BMV:Voltage"] = Voltage;
  data["BMV:SoC"] = SOC;
  //data["BMV:TTG"] = TTG;
  //data["BMV:CE"] = CE;
  //data["BMV:int Alarm_low_voltage;
  //data["BMV:int Alarm_high_voltage;
  //data["BMV:int Alarm_low_soc;
  //data["BMV:Alarm"] = Alarm;
  //data["BMV:Relay"] = Relay;

  data["humidity"] = humidity;
  //data["celsius"] = celsius;
  data["fahrenheit"] = fahrenheit;
  data["water high bilge"] = digitalRead(highBilge);
  data["water low bilge"] = digitalRead(lowBilge);
  data["water engine room"] = digitalRead(engineRoom);
  thing.write_bucket("BoatMonitorDataBucket", data);
  Serial.println("wrote data to thinger bucket, now to sleep!");

  LowPower.sleep(2 * 60000); //sleep for this many minutes (minutes*milliseconds in a minute)
}
/*
void InterruptWake()
{
  digitalWrite(LED_BUILTIN, HIGH); //turn on the LED when woken. Can remove all the LED
  // parts of the RTC/Sleep system when the sketch is done to save that power, or no leave
  // it for confirming operation. But would I watch the LED for 20 minutes to see if it lights?

  //Keep Interrupt routine short, but this is an alarm. Either trigger it here or in Thinger
}*/
