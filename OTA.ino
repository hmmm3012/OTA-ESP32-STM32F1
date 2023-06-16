/*
*   STM32 MCU1         ESP32      
*   PA9                 RXD             
*   PA10                TXD           
*   BOOT0               G23         
*   RST                 G22           
*   3.3V                3.3V            
*   GND                 GND           

*   STM32 MCU2         ESP32      
*   PA9                 RXD             
*   PA10                TXD           
*   BOOT0               G18         
*   RST                 G19           
*   3.3V                3.3V            
*   GND                 GND   

*/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include "stm32ota.h"
#include <FS.h>

// define pin out
#define NRST1 22
#define BOOT01 23
#define NRST2 19
#define BOOT02 18 

//Variables for stm32
const String STM32_CHIPNAME[8] = {
  "Unknown Chip",
  "STM32F103x8/B",
};
String html;
File fsUploadFile;
uint8_t binread[256];
int bini = 0;
String stringtmp;
int rdtmp = 0;
bool Runflag = 0;
bool mcu = 1;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";


//Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";

IPAddress localIP;
//IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;
//IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 0, 0);

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)

// function to handle flash

void handleFlash(AsyncWebServerRequest *request)
{
  String FileName, flashwr;
  int lastbuf = 0;
  uint8_t cflag, fnum = 256;
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    FileName =  file.name();
    file = root.openNextFile();
  }
  FileName = "/" + FileName;
  fsUploadFile = SPIFFS.open(FileName, "r");
  if (fsUploadFile) {
    bini = fsUploadFile.size() / 256;
    lastbuf = fsUploadFile.size() % 256;
    flashwr = String(bini) + "-" + String(lastbuf) + "<br>";
    for (int i = 0; i < bini; i++) {
      fsUploadFile.read(binread, 256);
      stm32SendCommand(STM32WR);
      while (!Serial.available()) ;
      cflag = Serial.read();
      if (cflag == STM32ACK)
        if (stm32Address(STM32STADDR + (256 * i)) == STM32ACK) {
          if (stm32SendData(binread, 255) == STM32ACK)
            flashwr += ".";
          else flashwr = "Error";
        }
    }
    fsUploadFile.read(binread, lastbuf);
    stm32SendCommand(STM32WR);
    while (!Serial.available()) ;
    cflag = Serial.read();
    if (cflag == STM32ACK)
      if (stm32Address(STM32STADDR + (256 * bini)) == STM32ACK) {
        if (stm32SendData(binread, lastbuf) == STM32ACK)
          flashwr += "<br>Finished<br>";
        else flashwr = "Error";
      }
    //flashwr += String(binread[0]) + "," + String(binread[lastbuf - 1]) + "<br>";
    fsUploadFile.close();
    request->send(200, "text/plain", " Programming done");
  }
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(!index){
    if (!filename.startsWith("/")) filename = "/" + filename;
    fsUploadFile = SPIFFS.open(filename, "w");
  }
  if(len) {
    if (fsUploadFile)
    fsUploadFile.write(data, len);
  }
  if(final){
    fsUploadFile.close();
  }
}

void handleFileDelete(AsyncWebServerRequest *request) {
  String FileList = "File: ";
  String FName;
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    FName = file.name();
    file = root.openNextFile();
  }
  FileList += FName;
  FName = "/" + FName;
  if (SPIFFS.exists(FName)) {
    SPIFFS.remove(FName);
  }
  request->redirect("/list");
}

String handleListFiles()
{
  String FileList;
  String Listcode;
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    String FileName = file.name();
    file = SPIFFS.open("/bin"+FileName,"r");
    String FileSize = String(file.size());
    int whsp = 6 - FileSize.length();
    while (whsp-- > 0)
    {
      FileList += " ";
    }
    FileList +=  FileName + "-" + FileSize + " bytes <br>";
    file = root.openNextFile();
  }
  FileList += "<a href=\"delete\"> DELETE FILE </a><br>";
  return FileList;
}

bool initMCU (){
  FlashMode();
  if (mcu) digitalWrite(NRST2, LOW);
  else digitalWrite(NRST1, LOW);
  while(Serial.available())
  {
    uint8_t tmp = Serial.read();
  }
  Serial.write(STM32INIT);
  while (Serial.available() == 0);
  rdtmp = Serial.read();    
  if (rdtmp == STM32ACK) {
    stringtmp = STM32_CHIPNAME[stm32GetId()];
    return true;
  }
  else if (rdtmp == STM32NACK) {
    Serial.write(STM32INIT);
    while (Serial.available() == 0);
    rdtmp = Serial.read();
    delay(10);
    if (rdtmp == STM32ACK){
      stringtmp = STM32_CHIPNAME[stm32GetId()];
      return true;
    }
    return 0;
  }
  else{
    stringtmp = "ERROR";
    return 0;
  }
}
void FlashMode()  {    //Tested  Change to flashmode
  //if (Runflag == 1) {
    digitalWrite(BOOT01, HIGH);
    digitalWrite(BOOT02, HIGH);
    delay(100);
    digitalWrite(NRST1, LOW);
    digitalWrite(NRST2, LOW);
    delay(50);
    digitalWrite(NRST1, HIGH);
    digitalWrite(NRST2, HIGH);
    delay(10);
    Runflag = 0;
  //}
}

void RunMode()  {    //Tested  Change to runmode
  //if(Runflag==0){
    digitalWrite(BOOT01, LOW);
    digitalWrite(BOOT02, LOW);
    delay(100);
    digitalWrite(NRST1, LOW);
    digitalWrite(NRST2, LOW);
    delay(50);
    digitalWrite(NRST1, HIGH);
    digitalWrite(NRST2, HIGH);
    delay(200);
    Runflag=1;
  //}
}
// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}
// Initialize WiFi
void initWiFi() {
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.softAP("ESP-WIFI-MANAGER", NULL);
  
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());

  if (!WiFi.config(localIP, localGateway, subnet)){
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      break;
    }
  }
  Serial.println(WiFi.localIP());
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200, SERIAL_8E1);
  pinMode(BOOT01, OUTPUT);
  pinMode(NRST1, OUTPUT);
  pinMode(BOOT02, OUTPUT);
  pinMode(NRST2, OUTPUT);
  initSPIFFS();	
  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  gateway = readFile (SPIFFS, gatewayPath);
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(gateway);
  //init wifi
  initWiFi(); 
  // OTA route
  server.serveStatic("/", SPIFFS, "/");
  server.on("/programm", HTTP_GET, handleFlash);
  server.on("/run1", HTTP_GET, [](AsyncWebServerRequest *request) {
    RunMode();
    digitalWrite(NRST2, LOW);
    request->send(SPIFFS, "/run1.html", "text/html");
  });
  server.on("/run2", HTTP_GET, [](AsyncWebServerRequest *request) {
    RunMode();
    digitalWrite(NRST1, LOW);
    request->send(SPIFFS, "/run2.html", "text/html");
  });
  server.on("/erase", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (stm32Erase() == STM32ACK)
      request->send(200, "text/plain", "Erase done");
    else if (stm32Erasen() == STM32ACK)
      request->send(200, "text/plain", "Erase done");
    else
      request->send(200, "text/plain", "Erase fail");
  });
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    handleFileDelete(request);
  });

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Upload done");
  }, handleUpload);
  server.on("/list", HTTP_GET ,[](AsyncWebServerRequest *request){
    String listname =  handleListFiles();
    request->send(200, "text/html", listname);
  });
  server.on("/stm1", HTTP_GET ,[](AsyncWebServerRequest *request){
    mcu = 1;
    initMCU();
    request->send(SPIFFS, "/boot1.html", "text/html");
  });
  server.on("/stm2",HTTP_GET,[](AsyncWebServerRequest *request){
    mcu = 0;
    initMCU();
    request->send(SPIFFS, "/boot2.html", "text/html");
  });
  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request) {
    RunMode();
    request->send(SPIFFS, "/ota.html", "text/html");
  });
  server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/about.html", "text/html");
  });
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP); 
  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/wifimanager.html", "text/html");
  });
  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        // HTTP POST ssid value
        if (p->name() == PARAM_INPUT_1) {
          ssid = p->value().c_str();
          Serial.print("SSID set to: ");
          Serial.println(ssid);
          // Write file to save value
          writeFile(SPIFFS, ssidPath, ssid.c_str());
        }
        // HTTP POST pass value
        if (p->name() == PARAM_INPUT_2) {
          pass = p->value().c_str();
          Serial.print("Password set to: ");
          Serial.println(pass);
          // Write file to save value
          writeFile(SPIFFS, passPath, pass.c_str());
        }
        // HTTP POST ip value
        if (p->name() == PARAM_INPUT_3) {
          ip = p->value().c_str();
          Serial.print("IP Address set to: ");
          Serial.println(ip);
          // Write file to save value
          writeFile(SPIFFS, ipPath, ip.c_str());
        }
        // HTTP POST gateway value
        if (p->name() == PARAM_INPUT_4) {
          gateway = p->value().c_str();
          Serial.print("Gateway set to: ");
          Serial.println(gateway);
          // Write file to save value
          writeFile(SPIFFS, gatewayPath, gateway.c_str());
        }
      }
    }
    request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
    delay(2000);
    ESP.restart();
  });
  server.begin();
}
/*
String updateMode(String MCU1,String MCU2){
  html = "<!DOCTYPE html><html><head><title>OTA</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"><link rel=\"icon\" type=\"image/png\" href=\"favicon.png\"><link rel=\"stylesheet\" href=\"https://use.fontawesome.com/releases/v5.7.2/css/all.css\" integrity=\"sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr\" crossorigin=\"anonymous\"></head><body><div class=\"topnav\">";
  html+="<a href=\"/\"><h1>Wifi Config</h1></a><a href=\"/ota\"><h1>Flash MCU</h1></a><a href=\"/about\"><h1>About</h1></a></div><div class=\"content\"><p>1.First STM32</p><p><a href=\"/run1\">Run</a></p><p><a href=\"/stm1\">Boot</a></p><p><a href=\"/programm\">Programm</a></p><p><a href=\"/erase\">Erase</a></p><p>2.Second STM32<br><p><a href=\"/run2\">Run</a></p><p><a href=\"/stm2\">Boot</a></p><p><a href=\"/programm\">Programm</a></p><p><a href=\"/erase\">Erase</a></p>";
  html+="<p>Upload STM32 BinFile</p><p><form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Upload'></form></p></div></body></html>"; 
}
String makePage(String title, String contents){
  String s = "<!DOCTYPE html><html><head>";
  s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  s += "<title >";
  s += title;
  s += "</title>";
  s += "<style> body{text-align:center} </style>";
  s += "</head><body text=#ffffff bgcolor=##4da5b9 >";
  s += "<div>";
  s += contents;
  s += "</div>";
  s += "</body></html>";
  return s;
}
*/
void loop() {
  //server.handleClient();
}