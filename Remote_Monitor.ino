//#define DEBUG //thinger.io verbose serial debug
// library name              // for...
#include <MKRGSM.h>        // GSM communication, from Arduino (Library Manager)
#include <ThingerMKRGSM.h>   // thinger.io, from thinger.io (Library Manager)
#include <DHT.h>            // temperature sensor, from Adafruit (Library Manager)
#include <ArduinoLowPower.h>  // sleep for SAMD MCUs, from Arduino (Library Manager)
// #include <WDTZero.h>       // WatchDog functionality for Arduino Zero, MKRZero and MKR1000 only, allow MINUTES of watchdog time https://github.com/javos65/WDTZero/tree/master/WDTZero
// even with 5 minutes of sleep, and everything working, as soon as I turn on the WDT, I get data every minute or two. Why is it resetting the process before the time is us?
// confirmed above problem with 2022.8.7 service of script, https://github.com/javos65/WDTZero/issues/13
#include <mcp_can.h>        // read CANbus data with mcp_can library
#include <SPI.h>            // maybe this is needed for CANbus?

#define sleep_minutes 15 // CHANGE THIS BEFORE INSTALLING ON THE BOAT

// Thinger.io and phone network credentials
#define USERNAME "damon"
#define DEVICE_ID "MKRBoatMonitor"
#define DEVICE_CREDENTIAL "cv3MTSMY&XjY"
#define GPRS_APN "TM"  // Get onto Things Mobile network
ThingerMKRGSM thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

// CAN Shield
#define CAN0_INT 7 // Set INT to pin 2
MCP_CAN CAN0(3); // Set CS to pin 10

// Temp/RH initiliazation
#define DHTPIN 0  // digital pin sensor is connected to
#define DHTTYPE DHT22  // which DHT sensor? 11 or 22?
DHT dht(DHTPIN, DHTTYPE);

// Water sensors digital wet or dry type
#define highBilge 3  // CHANGE THIS ONE because the CAN shield uses it // digital pin the sensor is connected to
#define lowBilge 4   // digital pin the sensor is connected to
#define engineRoom 5 // digital pin the sensor is connected to

// Bilge depth ultrasonic sensor
#include <NewPing.h>
#define TRIGGER_PIN  1  // Arduino pin tied to trigger pin on the ultrasonic sensor
#define ECHO_PIN     2  // Arduino pin tied to echo pin on the ultrasonic sensor
#define MAX_DISTANCE 150 // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.
#define measurements_to_average 10
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE); // NewPing setup of pins and maximum distance.
    
// Bilge pump sensor sensor
//static const char BILGE_SWITCH = 1; // digital pin connected to bilge pump switch through voltage divider 

boolean endpointRateLimiter = 1;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup");
  
  // Initialize CAN shield running at 16MHz with a baudrate of 500kb/s and the masks and filters disabled.
  if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK)
    Serial.println(F("MCP2515 Initialized Successfully!"));
  else
    Serial.println(F("Error Initializing MCP2515..."));
  CAN0.setMode(MCP_NORMAL); // Set operation mode to normal so the MCP2515 sends acks to received data.
 // CAN0.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode
  pinMode(CAN0_INT, INPUT); // Configuring pin for /INT input
  // Enable interrupts for the CAN0_INT pin (should be pin 2 or 3 for Uno and other ATmega328P based boards)
  attachInterrupt(digitalPinToInterrupt(CAN0_INT), ISR_CAN, FALLING);

  thing.set_apn(GPRS_APN);

  // set builtin led to output and turn it off
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // setup pins for ultrasonic sensor: use internal pullup resistor for echo pin
  pinMode(TRIGGER_PIN,OUTPUT);
  pinMode(ECHO_PIN,INPUT_PULLUP);

  // Thinger.io resources 
  //thing["led"] << digitalPin(LED_BUILTIN);
  // pin control example over internet (i.e. turning on/off a light, a relay, etc)
  //thing["relay"] << digitalPin(7); //change this to the right pin when connecting the relay

  //Temperature and RH sensor setup
  dht.begin();

  // Wake from sleep on water detected
        // add one for the depth from the prox sensor if/when it operates reliably
  LowPower.attachInterruptWakeup(highBilge, NULL, LOW);
  LowPower.attachInterruptWakeup(engineRoom, NULL, LOW);
  LowPower.attachInterruptWakeup(lowBilge, NULL, LOW);

  //pinMode(BILGE_SWITCH, INPUT_PULLUP);
 
  delay(10000);  //give a chance to reprogram

  // Set up a watchdog timer, long to allow communications with thinger, which can take more than 16 seconds, but sometimes do hang and need a reset
  // this issue was mostly solved with thing.stop(). But a watchdog is still probably useful, especially in winter, but 16 minutes is more frequent sample than I need
  // Glad that seemed to work in 2021. In 2022, I had misplaced this sketch and basically redid it,
  // without thing.stop() and data kept coming every 5 minutes for a least 3 days - but did stop repeatedly from a hang or freeze or something
//WatchDoggy.setup(WDT_SOFTCYCLE16M); // initialize WDT-softcounter refesh cycle on 16 minutes interval
  
  Serial.println("Setup Complete.");
}

void loop() {

  Serial.println("Entering main loop, starting thing.handle.");
  digitalWrite(LED_BUILTIN, LOW); // turn LED back off when returning to loop

  digitalWrite(LED_BUILTIN, HIGH); // LED comes on for thinger.handle 
  thing.handle();         // LED is on 7-8 seconds, then off for ~15 seconds for the rest of the loop
  digitalWrite(LED_BUILTIN, LOW); // LED comes off when finished with thinger.handle
 
Serial.println("thing.handle done.");
 
  // these declarations are not needed for DHT or thinger, but I have
  // them to get integers and avoid using overly precise data
  byte humidity = dht.readHumidity();
  int8_t fahrenheit = dht.readTemperature(true);

  byte bilge_depth = get_bilge_depth();

// CAN data variables defined up here in loop() so they are availabe outside if{CAN read}
// intialized as zero so no old values are available
  byte relay_state = {0};     //ID 0x001, len = 2 boolean
  byte pack_health = {0};        // 2, len = 1        byte
  byte cell_voltage_avg  = {0};  // 3, len = 2        byte
  byte amphours  = {0};           // 4, len = 2        byte
  byte pack_cycles  = {0}; // 5, len = 2        byte
  byte cell_voltage_high  = {0}; // 6, len = 2        byte
  byte cell_voltage_low  = {0};  // 7, len = 2        byte
  byte SoC = {0};                // 8, len = 1        char
  byte current = {0};            // 9, len = 2        byte

  // loop here to get more than one message? Move data into 1-2 messages?
  // looping 100 times didn't change the all-zero outputs
  // clear buffer or values each time through?
  for(byte j = 0; j<100; j++){
  // blank these variables read from CAN each time to avoid out of date data
  long unsigned int rxId = 0;
  unsigned char len = 0;
  unsigned char rxBuf[8];
  char msgString[16] = "";

  if(!digitalRead(CAN0_INT)) // If CAN0_INT pin is low, read receive buffer
  {  
  CAN0.readMsgBuf(&rxId, &len, rxBuf);   // Read data: len = data length, buf = data byte(s)
  switch(rxId) {
    case 0x001: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(relay_state == 0) relay_state = byte(1*strtoul(msgString, 0, 16)); // strtoul() converts string to integer, stopped 'invalid type conversion' errors
    } break;

    case 0x002: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(pack_health == 0) pack_health = byte(1*strtoul(msgString, 0, 16));
    } break;
    
    case 0x003: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(cell_voltage_avg == 0) cell_voltage_avg = byte(1*strtoul(msgString, 0, 16));
    } break;

    case 0x004: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(amphours == 0) amphours = byte(10 * strtoul(msgString, 0, 16));
    } break;

    case 0x005: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(pack_cycles == 0) pack_cycles = byte(1*strtoul(msgString, 0, 16));
    } break;

    case 0x006: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(cell_voltage_high == 0) cell_voltage_high = byte(1 * strtoul(msgString, 0, 16));
    } break;

    case 0x007: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        if(cell_voltage_low == 0) cell_voltage_low = byte(1*strtoul(msgString, 0, 16));
    } break;
 
    case 0x008: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(SoC == 0) SoC = byte(1*strtoul(msgString, 0, 16));
    } break;
    
    case 0x009: for(byte i = 0; i<len; i++){
        sprintf(msgString, " 0x%.2X", rxBuf[i]);
        Serial.println(rxId);
        Serial.print(msgString);
        String(msgString, HEX);
        if(current == 0) current = byte(10 * strtol(msgString, 0, 16));
    } break;
   }
  }
 }
    
  // check for high water, call endpoint to send email if detected (call once per boot)
  // add battery based criteria
  if ((digitalRead(highBilge) == LOW
      || digitalRead(lowBilge) == LOW
      || digitalRead(engineRoom) == LOW
      || bilge_depth > 12)
      && endpointRateLimiter) {
    digitalWrite(LED_BUILTIN, HIGH); //LED comes on when water detected
    Serial.println("water sensor check positive!");
    pson data;
    data["water high bilge"] = digitalRead(highBilge);
    data["water low bilge"] = digitalRead(lowBilge);
    data["water engine room"] = digitalRead(engineRoom);
    data["bilge water depth"] = bilge_depth;
    thing.call_endpoint("WaterDetectedEmail", data);
    endpointRateLimiter = 0;
    }  
  else {digitalWrite(LED_BUILTIN, LOW);
  Serial.println("water sensor check clear.");}

  // bilge pump run timer
 /* static unsigned long bilge_timer = millis();
  static unsigned long bilgeTimeHigh = 0;       //[seconds]
  // check bilge pump
  if (diintgitalRead(BILGE_SWITCH) == LOW)   // Bilge switch activated by high water
  {
    if (millis() - bilge_timer > 1000)  // Count every second water is high
    { bilge_timer = millis();
      bilgeTimeHigh++; }
  }
  else {bilge_timer = millis();}
 */

 /* Control Fans
  *  
    If the out­side dew point is high­er than in­side, do not vent.
    Oth­er­wise, if the out­side tem­per­a­ture is at least 15 ℃, ven­ti­late per­ma­nent­ly.
    Oth­er­wise, ven­ti­late for 20 min­utes, then make a break de­pend­ing on out­side tem­per­a­ture: the cold­er it is, the longer the break.
*/
 
  pson data;
  //data["bilge pump time"] = bilgeTimeHigh;
  data["humidity"] = humidity;
  //data["celsius"] = celsius;
  data["fahrenheit"] = fahrenheit;
  data["water high bilge"] = digitalRead(highBilge);
  data["water low bilge"] = digitalRead(lowBilge);
  data["water engine room"] = digitalRead(engineRoom);
  data["bilge water depth"] = bilge_depth;
  data["relay state"] = relay_state;
  data["pack health"] = pack_health;
  data["SoC"] = SoC;
  data["avg cell V"] = cell_voltage_avg;
  data["high cell V"] = cell_voltage_high;
  data["low cell V"] = cell_voltage_low;
  data["current"] = current;
  data["amp-hours"] = amphours;
  data["pack cycles"] = pack_cycles;
 
  // Print the data to the Serial monitor when debugging
  Serial.println("about to write data to thinger bucket");
  Serial.println("Current:");
  Serial.println(current);
  Serial.println("Relay State:");
  Serial.println(relay_state);
  Serial.println("Pack Health:");
  Serial.println(pack_health);
  Serial.println("SOC:");
  Serial.println(SoC);
  Serial.println("Avg cell V:");
  Serial.println(cell_voltage_avg);
  Serial.println("High cell V:");
  Serial.println(cell_voltage_high);
  Serial.println("Low cell V:");
  Serial.println(cell_voltage_low);
  Serial.println("Humidity:");
  Serial.println(humidity);
  Serial.println("Temperature (F):");
  Serial.println(fahrenheit);
  Serial.println("Water in bilge (cm):");
  Serial.println(bilge_depth);
 
  thing.write_bucket("BoatMonitorDataBucket", data);              //died here a couple times
  Serial.println("Wrote data to thinger bucket, now to sleep!");
  // Sleep
  // Shut stuff off // test the power savings of these and uncomment or remove them
  USBDevice.detach();        // Is this just communcations? What is power is coming in through USB?
 // shut off the modem
  Serial.end();
  digitalWrite(LED_BUILTIN, LOW);
  thing.stop(); //this seems to prevent thinger.handle hanging. https://community.thinger.io/t/problem-with-gsm-900a/1874
  CAN0.setMode(MCP_SLEEP); //CAN shield controller
  Serial.flush(); 
  //sleep for this many minutes (minutes * milliseconds in a minute) 
  LowPower.deepSleep(sleep_minutes * 60000);
  
  // Turn stuff back on
  CAN0.setMode(MCP_NORMAL); // When the MCP2515 wakes up it will be in LISTENONLY mode, here we put it into NORMAL mode
  //USBDevice.attach();         // remove this when finished testing to save a little power?
  //Serial.begin(115200);
  
  Serial.println("Woke up!");
}

// Ultrasonic proxmity sensor function
byte get_bilge_depth() {
    // I considered a DHT provide the temp and RH in the bilge. While it wouldn't change quickly, it would change with lake temp over the summer 
    // and drastically when hauled out - but that would only cause a ~2cm difference in the result so I'll save the pins and complication
    float celsius = 21;  // average conditions in the bilge? dht.readTemperature();
    float humidity = 70; // average conditions in the bilge? dht.readHumidity();
    float sound_speed = (331.4 + (0.606 * celsius) + (0.0124* humidity))/10000; // speed of sound in cm/ms adjusted for temperature and humidity
    float duration = sonar.ping_median(measurements_to_average, MAX_DISTANCE);  
    byte distance = (duration / 2) * sound_speed; 
    byte bilge_depth = 74 - distance; // Sensor is mounted 74 cm above the bilge floor
    return bilge_depth;
}

/*
void InterruptWake() // Interrupt routine, not currently called by the interrupts
{
  digitalWrite(LED_BUILTIN, HIGH); //turn on the LED when woken. Can remove all the LED
  // parts of the RTC/Sleep system when the sketch is done to save that power, or no leave
  // it for confirming operation. But would I watch the LED for 20 minutes to see if it lights?

  //Keep Interrupt routine short, but this is an alarm. Either trigger it here or in Thinger
}*/

static void ISR_CAN()
{
  // We don't do anything here, this is just for waking up the microcontroller
}

/*
void dummy() {
  // This function will be called once on device wakeup
  boolean endpointRateLimiter = 1; // The copy of this at the top means one email per cold start, this one means one per wake
  // Remember to avoid calling delay() and long running functions since this functions executes in interrupt context
}
*/

/*
void myshutdown() /runs before a watchdog triggered softcycle (hard cycles just cut the power and max out at 16 seconds)
{
 // Proper Modem Shutdown
  MODEM.sendf("AT+CFUN=15");
  MODEM.waitForResponse(10000); 
}*/
