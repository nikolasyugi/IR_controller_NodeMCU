#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <sstream>

const int RECV_PIN = 0;
IRrecv irrecv(RECV_PIN);
decode_results results;
String signal;
String brand;

const uint16_t emitter = 4;
IRsend irsend(emitter);

const int mode_switch = 5;
bool mode = false; //false = normal operational mode, true = setup mode

String networks_list;
String access_point_name = "IR_Controller";
ESP8266WebServer server(80); // Create a webserver object that listens for HTTP request on port 80

// declare functions
void handleRoot();
void handleToggle();
void handleNotFound();
void handleNetworks();
void handleConnect();
bool testConnection();
void setupAP();
void launchWebServer();
void clearEEPROM(int, int);

/*   SETUP   */
void setup(void)
{
  Serial.begin(115200); // Start the Serial communication
  irsend.begin();
  irrecv.enableIRIn();
  delay(10);
  Serial.println('\n');

  //Start EEPROM
  EEPROM.begin(512);

  //Setup pins
  pinMode(emitter, OUTPUT);
  pinMode(mode_switch, INPUT);

  if (digitalRead(mode_switch))
  {
    mode = true;
    Serial.println("Entered setup mode");
  }
  else
  {

    //Disconnect to previous network
    WiFi.disconnect();

    //Get SSID from EEPROM
    String esid;
    for (int i = 0; i < 32; ++i)
    {
      esid += char(EEPROM.read(i));
    }
    Serial.println();
    Serial.print("SSID: ");
    Serial.println(esid);

    //Get password from EEPROM
    String epass;
    for (int i = 32; i < 96; ++i)
    {
      epass += char(EEPROM.read(i));
    }
    Serial.print("Password: ");
    Serial.println(epass);

    //Try to connect to the Wi-Fi
    WiFi.begin(esid.c_str(), epass.c_str());

    //If there was an SSID and password in the EEPROM
    if (testConnection())
    {
      Serial.println('\n');
      Serial.print("Connected to ");
      Serial.println(WiFi.SSID()); // Tell us what network we're connected to
      Serial.print("IP address:\t");
      Serial.println(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer

      if (MDNS.begin("esp8266", WiFi.localIP()))
      { //Start the mDNS responder for esp8266.local
        Serial.println("mDNS responder started");
        MDNS.addService("http", "tcp", 80);
      }
      else
      {
        Serial.println("Error setting up MDNS responder!");
      }

      server.on("/root", HTTP_GET, handleRoot);      // Call the 'handleRoot' function when a client requests URI "/root"
      server.on("/toggle", HTTP_POST, handleToggle); // Call the 'handleToggle' function when a POST request is made to URI "/toggle"
      server.onNotFound(handleNotFound);             // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"

      server.begin(); // Actually start the server
      Serial.println("HTTP server started");
    }
    else //If no connection was made
    {
      //Start AP
      setupAP();
      server.handleClient();
    }
  }
}
/*   LOOP  */
void loop(void)
{
  if (mode)
  {
    if (irrecv.decode(&results))
    {
      Serial.println("");
      Serial.println("");
      // Serial.print(String((unsigned long)results.value, HEX));
      // serialPrintUint64(results.value, HEX);
      signal = String((unsigned long)results.value, HEX);
      brand = String((unsigned long)results.decode_type, HEX);

      if (signal != "ffffffff" && brand != "ffffffff")
      {
        clearEEPROM(96, 168);
        Serial.print("Saved signal: ");
        Serial.println(signal);
        Serial.print("And brand: ");
        Serial.println(brand);

        for (unsigned int i = 0; i < signal.length(); ++i) //address 96 to 159 (64)
        {
          EEPROM.write(96 + i, signal[i]);
        }
        for (unsigned int i = 0; i < brand.length(); ++i) //address 160 to 168 (8)
        {
          EEPROM.write(160 + i, brand[i]);
        }
        EEPROM.commit();
      }
      // switch (results.decode_type)
      // {
      // case NEC:
      //   Serial.println("NEC");
      //   break;
      // case SONY:
      //   Serial.println("SONY");
      //   break;
      // case RC5:
      //   Serial.println("RC5");
      //   break;
      // case RC6:
      //   Serial.println("RC6");
      //   break;
      // case DISH:
      //   Serial.println("DISH");
      //   break;
      // case SHARP:
      //   Serial.println("SHARP");
      //   break;
      // case JVC:
      //   Serial.println("JVC");
      //   break;
      // case SANYO:
      //   Serial.println("SANYO");
      //   break;
      // case MITSUBISHI:
      //   Serial.println("MITSUBISHI");
      //   break;
      // case SAMSUNG:
      //   Serial.println("SAMSUNG");
      //   break;
      // case LG:
      //   Serial.println("LG");
      //   break;
      // case WHYNTER:
      //   Serial.println("WHYNTER");
      //   break;
      // case AIWA_RC_T501:
      //   Serial.println("AIWA_RC_T501");
      //   break;
      // case PANASONIC:
      //   Serial.println("PANASONIC");
      //   break;
      // case DENON:
      //   Serial.println("DENON");
      //   break;
      // default:
      // case UNKNOWN:
      //   Serial.println("UNKNOWN");
      //   break;
      // }
      irrecv.resume();
    }
  }
  else
  {
    server.handleClient(); // Listen for HTTP requests from clients
  }
}

/*   FUNCTIONS  */
bool testConnection()
{
  int c = 0;
  Serial.println("Connecting to Wi-fi");

  //Try connection for sometime
  while (c < 20)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    Serial.print("*");
    c++;
  }

  //Timeout
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}

void setupAP(void)
/*
Setup access point, so user can connect to his home network
*/
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("Scanning networks");
  int n = WiFi.scanNetworks();
  if (n == 0)
    Serial.println("No networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      delay(10);
    }
  }
  Serial.println("");

  //Create list of networks that will be displayed on the webpage
  networks_list = "<ol>";
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    networks_list += "<li>";
    networks_list += WiFi.SSID(i);
    networks_list += " (";
    networks_list += WiFi.RSSI(i);

    networks_list += ")";
    networks_list += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    networks_list += "</li>";
  }
  networks_list += "</ol>";
  delay(100);

  //Start AP
  WiFi.softAP(access_point_name, "");
  Serial.print("Created Access Point ");
  Serial.println(access_point_name);

  //Launch Web Server to choose network
  launchWebServer();
}

void launchWebServer()
{
  Serial.println("");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());

  // Start the server
  server.begin();
  Serial.println("AP Server started");

  //Endpoints
  server.on("/networks", HTTP_GET, handleNetworks);
  server.on("/connect", HTTP_POST, handleConnect);
  server.onNotFound(handleNotFound);
}

void clearEEPROM(int begin, int end)
{
  Serial.println("Clearing EEPROM");
  for (int i = begin; i < end; ++i)
  {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

/*   ENDPOINTS   */

/* /networks */
void handleNetworks()
{
  String content;
  IPAddress ip = WiFi.softAPIP();
  String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

  content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 at ";
  // content += "<form action=\"/scan\" method=\"POST\"><input type=\"submit\" value=\"scan\"></form>";
  content += ipStr;
  content += "<p>";
  content += networks_list;
  content += "</p><form method='post' action='connect'><label>SSID: </label><input name='ssid' length=32><input name='password' length=64><input type='submit'></form>";
  content += "</html>";
  server.send(200, "text/html", content);
}

/* /connect */
void handleConnect()
{
  String content;
  int statusCode;

  //Get params from url
  String query_ssid = server.arg("ssid");
  String query_password = server.arg("password");

  if (query_ssid.length() > 0 && query_password.length() > 0)
  {
    clearEEPROM(0, 96);
    Serial.println(query_ssid);
    Serial.println("");
    Serial.println(query_password);
    Serial.println("");

    Serial.println("Writing SSID to EEPROM:");
    for (unsigned int i = 0; i < query_ssid.length(); ++i)
    {
      EEPROM.write(i, query_ssid[i]);
      Serial.print("Wrote: ");
      Serial.println(query_ssid[i]);
    }
    Serial.println("Writing Password to EEPROM:");
    for (unsigned int i = 0; i < query_password.length(); ++i)
    {
      EEPROM.write(32 + i, query_password[i]);
      Serial.print("Wrote: ");
      Serial.println(query_password[i]);
    }
    EEPROM.commit();

    content = "{\"Success\":\"saved to EEPROM... reset to boot into new Wi-fi\"}";
    statusCode = 200;
    ESP.reset();
  }
  else
  {
    content = "{\"Error\":\"404 not found\"}";
    statusCode = 404;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(statusCode, "application/json", content);
}

/* /root */
void handleRoot()
{ // When URI / is requested, send a web page with a button to toggle the device
  // server.send(200, "text/html", "<form action=\"/toggle\" method=\"POST\"><input style=\"display: block;margin: 0 auto;padding: 10px;text-align: center;min-width: 150px;font-family: Trebuchet MS;\" type=\"submit\" value=\"Clicar botão\"></form>");
  server.send(200, "text/html", "<form action=\"/toggle\" method=\"POST\"style=\"display: flex; height:100%; align-items: center; justify-content: center;\"><input type=\"submit\" value=\"Enviar sinal\" style=\"text-align: center; width: 100%; padding:15px; border:1px solid #ffffff; border-radius: 5px; background: #76b900; color:white; font-weight: bold;font-size: 18px;font-family: 'Trebuchet MS', 'Lucida Sans Unicode', 'Lucida Grande', 'Lucida Sans', Arial, sans-serif;\"/></form>");
}

/* /toggle */
void handleToggle()
{
  String irCode;
  unsigned long long ULirCode;

  String decodedBrand;
  int INTdecodedBrand;

  //Get signal code from EEPROM
  for (int i = 96; i < 160; ++i)
  {
    irCode += char(EEPROM.read(i));
  }

  //Convert irCode to long long
  std::stringstream buffer(irCode.c_str());
  buffer >> std::hex >> ULirCode;

  Serial.println();
  Serial.print("IR Code: ");
  serialPrintUint64(ULirCode, HEX);
  Serial.println("");

  //Get brand from EEPROM
  for (int i = 160; i < 168; ++i)
  {
    decodedBrand += char(EEPROM.read(i));
  }
  Serial.print("Brand: ");
  INTdecodedBrand = decodedBrand.toInt();
  Serial.println(INTdecodedBrand);

  //Send IR signal depending on the brand
  switch (INTdecodedBrand)
  {
  case 1:
    break;
  case 2:
    break;
  case 3: //NEC
    Serial.println("Sending signal NEC");
    irsend.sendNEC(ULirCode);
    break;
  case 4:
    break;
  case 5:
    break;
  case 6:
    break;
  case 7: //SAMSUNG
    Serial.println("Sending signal SAMSUNG");
    irsend.sendSAMSUNG(ULirCode);
    break;
  default:
    break;
  }
  // irsend.sendNEC(ULirCode);
  // irsend.sendNEC(0x20DF10EFUL);
  server.sendHeader("Location", "/root"); // Add a header to respond with a new location for the browser to go to the root page again
  server.send(303);                       // Send it back to the browser with an HTTP status 303 (See Other) to redirect
}

/* wildcard */
void handleNotFound()
{
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}