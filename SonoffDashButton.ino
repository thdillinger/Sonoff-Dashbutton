/* Th.Dillinger, www.dillinger-engineering.de 
 * Diese Vorlage beinhaltet die folgenden Optionen 
 * Captiv Portal für die Anmeldung an einen lokalen WLAN Router wenn die automatische Verbindung scheitert
 
   1MB/64k SPIFFS flash sizee Memory replaced

   Programming interface
   Pin Function
   1 - vcc    (3v3)
   2 - rx     (D3)
   3 - tx     (D1)
   4 - gnd
   5 - gpio0  (D0 10K pullup)
   6 - RST    (direct 10K pullup)

   esp8266 connections
   RST    - button by capacitor and 10K pullup
   ADC  int./ext optional
   gpio16 - solder bridge to RST for wakeup deepsleep by timer
   gpio14 - gren LED / V1.0 - ADC on over Mosfet
   gpio12 - button by 10K pullup
   gpio13 - red LED (active high)

   gpio1  - TxD direct connected to Prog interface
   gpio3  - RxD direct connected to Prog interface 
   gpio5  - not connected (optional LED)
   gpio0  - not connected (optional LED)
   gpio2  - 22K pullup
   gpio15 - 10K pulldown
*/

//#define SER_DEBUG             // Uncomment this to disable prints and save space

#define APPassword       "Remote"
#define SV               100  // Current Sofware Version 100 = 1.00
#define HV               100  // Current HArdware Revision 100 = 1.00
#define APTIMEOUT        240  // Timeout time in sec. for AP Mode
#define CLIENTTIMEOUT    5000 // Timeout time in msec. for Client connection
#define BATTLOWVOLTAGE   3.00 // Voltage for Battlow indikation
#define EEPROM_SALT      100  // Version of Dataset
#define NC1              4    // SDA Option for future use
#define NC2              5    // SCL Option for future use
#define BUTTON           12   // Button for aktion
#define RED_LED          13   // Status LED red
#define GREN_LED         14   // Status LED green
#define GPIO16           16   // option to Wakup ESP from DeepSleep Mode

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <SimpleTimer.h>
#include <Ticker.h>
#include <EEPROM.h>
//#include <RemoteDebug.h>      //https://github.com/JoaoLopesF/ESP8266-RemoteDebug-Telnet

ADC_MODE(ADC_VCC);              //  internal VDD messurment

/* MDNSResponder is neded for
   For Mac OSX support is built in through Bonjour already.
   For Linux, install Avahi.
   For Windows, install Bonjour. */
MDNSResponder mdns;    
ESP8266WebServer server(80);

char  *Hostname   = "New Dashbutton";
String webPage    = "";
String Reqest     = "";
int    EspVdd     = -1;
bool   DashConfig = false;

typedef struct {
  int   salt = EEPROM_SALT;
  char  deviceName[33]  = "New Dashbutton"; // default Modulname
  char  DashServer[33]  = ""; 
  char  DashCommand[33]     = ""; 
  char  DashPort[6]     = "80";
} WMSettings;

WMSettings settings;

//needed for OTA Update
#ifdef OTA_UPDATE
  #include <WiFiUdp.h>
  #include <ArduinoOTA.h>
#endif

Ticker ticker;       // Global Instans for Ticker calls ticker LED status

/*float getEspVdd(bool limit, float VDD0, float VDD100, float OUT0, float OUT100)
// Gets the chip intern VDD, the ADC pin must by not connected
//ADC_MODE(ADC_VCC); to measure the chip internal VDD, include behind the defines
{
  float v = ESP.getVcc()/1000.0;
  float x =(((OUT100-OUT0)*(v-VDD0))/(VDD100-VDD0))+OUT0; // Scal the Value
  x = (x<OUT0)?OUT0:x;      // limit the low value
  x = (x>OUT100)?OUT100:x;  // limit the high value

  #ifdef SER_DEBUG
    Serial.print("Voltage: ");
    Serial.print(v);   
    Serial.print(" V, Battery capacity: ");
    Serial.print(x);   
    Serial.println(" %");   
  #endif
  return x;
}*/

void SaveConfig(){ //save the custom parameters to FS
  EEPROM.begin(512);  // save user parameter
  EEPROM.put(0, settings);
  EEPROM.end();
}

void reset() {  // reset WIFI settings to defaults
  // wifiManager.resetSettings();
  //reset settings to defaults 
    WMSettings defaults;
    settings = defaults;
    // SaveConfig(); // Comment if old Setting should be saved
  //reset wifi credentials
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
  delay(1000);
}

int find_text(String Text, String Searchtext) {
  int foundpos = -1;
  if (Searchtext.length() <= Text.length()){
    for (int i = 0; i <= Text.length() - Searchtext.length(); i++) {
      if (Text.substring(i,Searchtext.length()+i) == Searchtext) {
        foundpos = i;
        return foundpos;
      }
    }
  }
  return foundpos;
}

int SendDashToken(){  // Use WiFiClient class to create TCP connections
  /* SendDashToken Respons */
  int port = atoi(settings.DashPort);
  WiFiClient client;
  #ifdef SER_DEBUG
    Serial.print("try to connecting to: ");
    Serial.println(settings.DashServer);
    Serial.print("current port: ");
    Serial.println(port);
  #endif
  if (!client.connect(settings.DashServer, port)) {
    #ifdef SER_DEBUG
      Serial.println(">>> Connection failed !!!");
    #endif
    return(1);  // connection failed
  }
  
  // We now create a URI for the request
  String url = "/";
  url += settings.DashCommand;
  #ifdef SER_DEBUG
    Serial.print("Requesting URL: ");
    Serial.println(url);
  #endif 
  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + settings.DashServer + "\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > CLIENTTIMEOUT) {
      #ifdef SER_DEBUG
        Serial.println(">>> Client timeout !!!");
      #endif
      client.stop();
      return(2);  // timeout
    }
  }
  // Read all the lines of the reply from server and print them to Serial
  Reqest = "";
  while(client.available()){
    String ReqStr = client.readStringUntil('\r');
    Reqest += ReqStr;
  }
  #ifdef SER_DEBUG
    Serial.println(Reqest);  
  #endif

  int FindeOK = find_text(Reqest, "200 OK");  
  if (FindeOK == -1){
      #ifdef SER_DEBUG
        Serial.println(">>> Client sends no '200 OK' !!!");
      #endif
      client.stop();
      return(3);  // not acknolaged    
  }

  #ifdef SER_DEBUG
    Serial.println("closing connection properly");
  #endif
  return(0);  
 }

void tick()  // toggle LED state function
{
  int state = digitalRead(RED_LED);  // get the current state of GPIO1 pin
  digitalWrite(RED_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  #ifdef SER_DEBUG
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it 
    Serial.println(myWiFiManager->getConfigPortalSSID());
    //entered config mode, make led toggle faster
  #endif
  ticker.attach(0.15, tick);  // fast blinking LED
}

// Sets the ESP to deep sleep mode
void shutdown(){
  #ifdef SER_DEBUG
    Serial.println("Shutting down");
    Serial.println("Going to sleep ...");
  #endif
  ESP.deepSleep(0);
    //ESP.deepSleep(30000000, WAKE_RF_DISABLED); // 30 secs.
    //ESP.deepSleep(30000000, WAKE_RF_DEFAULT); // 30 secs.
  delay(100);
  #ifdef SER_DEBUG
    Serial.println("Sleep failed!");
  #endif
    ticker.attach(0.1, tick);  // extrem Fast blinking LED
  while(1) {} // infinity loop
}

void SetLedOutput(int Status){
  switch(Status) {
    case 0:  okled();      // Call ok and go to slepp
      break;
    default: errorled(10); // Call Error and go to slepp
      break;
  } // end switch
}

void errorled(int duration){
  ticker.attach(0.1, tick);  // extrem Fast LED blinking for one second
  for (int i = 0; i < duration; i++) {
    delay(100);
  }
  shutdown();
}

void okled(){
  ticker.detach();
  digitalWrite(RED_LED, HIGH);
  for (int i = 0; i < 5; i++) {
    delay(100);
  }
  shutdown();
}

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  #ifdef SER_DEBUG
    Serial.println("Should save config");
  #endif
  shouldSaveConfig = true;
}

/************************************* SETUP ****************************************************************/
void setup()
{
  #ifdef SER_DEBUG
    Serial.begin(115200);
    Serial.println(" Startup ...");
  #endif

  pinMode(GPIO16, INPUT);    // set GPIO16 to input for optional deepsleep wakeup
  pinMode(RED_LED, OUTPUT);  // set led pin as output

  // For accu operation, check the voltage and if necessary prevent debris from being discharged
  float V = ESP.getVcc()/1000.0;
  #ifdef SER_DEBUG
    Serial.print(V);
    Serial.println("V Battery Voltage");
  #endif
  if(V <= BATTLOWVOLTAGE){
  #ifdef SER_DEBUG
    Serial.print(BATTLOWVOLTAGE);
    Serial.println("Threshold, Batt low error !!!");
  #endif
    errorled(10); // 1 Sec. then go to deep sleep
  }

  ticker.attach(0.5, tick);  // start ticker with 0.5 because we start in AP mode and try to connect
  WiFiManager wifiManager;
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //timeout - this will quit WiFiManager if it's not configured in 2 minutes, causing a restart
  wifiManager.setConfigPortalTimeout(APTIMEOUT); // minutes for timeout

  //custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  if (settings.salt != EEPROM_SALT) {
    #ifdef SER_DEBUG
      Serial.println("Invalid settings in EEPROM, trying with defaults");
    #endif
    WMSettings defaults;
    settings = defaults;
  } 
  Hostname = settings.deviceName; 
  #ifdef SER_DEBUG
    Serial.println(WiFi.localIP());
    Serial.println(settings.deviceName);
    Serial.println(settings.DashServer);
    Serial.println(settings.DashCommand);
    Serial.println(settings.DashPort);
  #endif
 
  WiFiManagerParameter custom_devicename_text("Device Name");
  wifiManager.addParameter(&custom_devicename_text);

  WiFiManagerParameter custom_devicename("device-name", "device name", settings.deviceName, 33);
  wifiManager.addParameter(&custom_devicename);

  WiFiManagerParameter custom_comment_text1("<br/>WIFI Settings");
  wifiManager.addParameter(&custom_comment_text1);

  WiFiManagerParameter custom_dash_server("Dash-server", "DashButton Server", settings.DashServer, 33);
  wifiManager.addParameter(&custom_dash_server);

  WiFiManagerParameter custom_dash_command("dash-command", "DashButton Command", settings.DashCommand, 33);
  wifiManager.addParameter(&custom_dash_command);

  WiFiManagerParameter custom_dash_port("dash-port", "DashButton Port", settings.DashPort, 6);
  wifiManager.addParameter(&custom_dash_port);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(Hostname,APPassword)) {
    #ifdef SER_DEBUG
      Serial.println("failed to connect and hit timeout");
    #endif
    errorled(30);  // 3 Sec. then go to deep sleep
  }
  if (shouldSaveConfig) { 
    strcpy(settings.deviceName, custom_devicename.getValue());
    strcpy(settings.DashServer, custom_dash_server.getValue());
    strcpy(settings.DashCommand, custom_dash_command.getValue());
    strcpy(settings.DashPort, custom_dash_port.getValue());
    #ifdef SER_DEBUG 
      Serial.println("Saving config");
      Serial.println(settings.deviceName);
      Serial.println(settings.DashServer);
      Serial.println(settings.DashCommand);
      Serial.println(settings.DashPort);
    #endif
    SaveConfig(); //save the custom parameters to FS
    DashConfig = true;
  }

  //if you get here you have connected to the WiFi
  #ifdef SER_DEBUG
    Serial.println("connected... :)");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  #endif  
  if (mdns.begin(Hostname, WiFi.localIP())) {
    #ifdef SER_DEBUG
      Serial.println("MDNS responder started");
    #endif 
  }else{
    #ifdef SER_DEBUG
      Serial.println("MDNS responder not started!");
    #endif 
  }
  pinMode(BUTTON, INPUT);  //setup button
  // start ticker with 1.0 because we start in AP mode and try to connect
  ticker.attach(1.0, tick); // Led is blinking in on second interval for conecting to the server 
} // end Setup


/************************************* LOOP ****************************************************************/     
void loop(){
  ticker.detach();    
  digitalWrite(RED_LED, HIGH); // LED on for signe Testmodus
  int ButtonState = digitalRead(BUTTON);
  for (int i = 0; i < 100; i++) {
    delay(100);
    int ButtonState = digitalRead(BUTTON);
    if(ButtonState == 1){
      SetLedOutput(SendDashToken()); // Send message
    }
  }
  reset();  // reset WIFI settings to defaults   
} // end loop
