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
bool RunBoth = 1;
bool mcu = 1;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
int numberNetwork;
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
const char* binPath ="/bin";
IPAddress localIP;
//IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;
//IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 0, 0);

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)
// Make String for html
String makeOptionHtml (String ssid){
  return "<option value=\""+ ssid +"\">" + ssid + "</option>";
}
String listHTML (const size_t bytes, String fileName){
  String html = "<tr><th scope=\"row\">";
  html += fileName;
  html += "</th><td>";
  html += server_ui_size(bytes);
  html += "</td><td><input type =\"submit\" value =\"Delete\" name=\"";
  html += fileName;
  html += "\"></td></tr>";
  return html;
}
String server_ui_size(const size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}
//processor
String processorWIFI(const String& var) {
  if(var =="PLACEHOLDER_SSID"){
    String p ="";
    if(numberNetwork>0){
      for (int i = 0; i < numberNetwork; i++){
        p+= makeOptionHtml(WiFi.SSID(i));
      }
      return p;
    }
  }
  else if(var == "PLACEHOLDER_IP") {
    return WiFi.localIP().toString();
  }else if (var == "PLACEHOLDER_CONNECT"){
    if(WiFi.status() != WL_CONNECTED){
      return "not connected";
    }else{
      return ssid;
    }
  }
  return String();
}
String processorOTA(const String& var) {
  String state_MCU1 = "running";
  String state_MCU2 = "running";
  if(RunBoth == 0){
    if(Runflag == 1){
      state_MCU2 = "off";
    }else{
      state_MCU1 = "off";
    }
  }
  if(var == "STATE_MCU1"){
    return state_MCU1;
  }else if(var =="STATE_MCU2"){
    return state_MCU2;
  }else if(var =="PLACEHOLDER_PROGRAMM"){
    String FileList;
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
      String FileName = file.name();
      FileName = "/bin/" + FileName;
      if(SPIFFS.exists(FileName)){
        FileList += makeOptionHtml(file.name());
      }
      file = root.openNextFile();
    }
    return FileList;
  }
  return String();
}
String processorList(const String& var){
  String htmlList = handleListFiles();
  if(var == "PLACEHOLDER_LIST"){
    return htmlList;
  }
  return String();
}
// function to handle flash

String handleFlash(String FileProg)
{
  initMCU();
  String flashwr;
  int lastbuf = 0;
  uint8_t cflag, fnum = 256;
  FileProg = "/bin/" + FileProg;
  fsUploadFile = SPIFFS.open(FileProg, "r");
  if (fsUploadFile) {
    bini = fsUploadFile.size() / 256;
    lastbuf = fsUploadFile.size() % 256;
    for (int i = 0; i < bini; i++) {
      fsUploadFile.read(binread, 256);
      stm32SendCommand(STM32WR);
      while (!Serial.available()) ;
      cflag = Serial.read();
      if (cflag == STM32ACK)
        if (stm32Address(STM32STADDR + (256 * i)) == STM32ACK) {
          if (stm32SendData(binread, 255) == STM32ACK);
          else return "Error";
        }
    }
    fsUploadFile.read(binread, lastbuf);
    stm32SendCommand(STM32WR);
    while (!Serial.available()) ;
    cflag = Serial.read();
    if (cflag == STM32ACK)
      if (stm32Address(STM32STADDR + (256 * bini)) == STM32ACK) {
        if (stm32SendData(binread, lastbuf) == STM32ACK)
          return "Finished";
        else return "Error";
      }
    //flashwr += String(binread[0]) + "," + String(binread[lastbuf - 1]) + "<br>";
    fsUploadFile.close();
  }
  return "File not found";
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(!index){
    if (!filename.startsWith("/"))
      filename = "/bin/" + filename;
    if (SPIFFS.exists(filename)) {
      SPIFFS.remove(filename);
    }
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
void eraseMCU(AsyncWebServerRequest *request){
  initMCU();
  if (stm32Erase() == STM32ACK)
    request->send(200, "text/plain", "Erase done");
  else if (stm32Erasen() == STM32ACK)
    request->send(200, "text/plain", "Erase done");
  else
    request->send(200, "text/plain", "Erase fail");
}

String handleListFiles()
{
  String FileList;
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    String FileName = file.name();
    FileName = "/bin/" + FileName;
    if(SPIFFS.exists(FileName)){
      file = SPIFFS.open(FileName,"r");
      FileList+= listHTML(file.size(),file.name());
    }
    file = root.openNextFile();
  }
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
  scanWifi();
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
void scanWifi(){
  WiFi.disconnect();
  delay(100);
  numberNetwork = WiFi.scanNetworks();
  Serial.println("Scan done");
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
  //ip = readFile(SPIFFS, ipPath);
  //gateway = readFile (SPIFFS, gatewayPath);
  Serial.println(ssid);
  Serial.println(pass);
  //Serial.println(ip);
  //Serial.println(gateway);
  //init wifi
  initWiFi(); 
  // OTA route
  server.serveStatic("/", SPIFFS, "/");
  /*
  server.on("/programm1", HTTP_GET, [](AsyncWebServerRequest *request){
    mcu = 1;
    handleFlash(request);
  });
  server.on("/programm2", HTTP_GET, [](AsyncWebServerRequest *request){
    mcu = 0;
    handleFlash(request);
  });
  */
  server.on("/erase1", HTTP_GET, [](AsyncWebServerRequest *request) {
    mcu = 1;
    eraseMCU(request);
  });
  server.on("/erase2", HTTP_GET, [](AsyncWebServerRequest *request) {
    mcu = 0;
    eraseMCU(request);
  });
  server.on("/run1", HTTP_GET, [](AsyncWebServerRequest *request) {
    RunMode();
    Runflag =1;
    RunBoth = 0;
    digitalWrite(NRST2, LOW);
    request->send(SPIFFS, "/ota.html", "text/html", false, processorOTA);
  });
  server.on("/run2", HTTP_GET, [](AsyncWebServerRequest *request) {
    RunMode();
    Runflag =0;
    RunBoth = 0;
    digitalWrite(NRST1, LOW);
    request->send(SPIFFS, "/ota.html", "text/html", false, processorOTA);
  });
  /*
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    handleFileDelete(request);
  });
*/
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Upload done");
  }, handleUpload);
  server.on("/list", HTTP_GET ,[](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/list.html", "text/html", false, processorList);
  });
  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request) {
    RunMode();
    RunBoth=1;
    request->send(SPIFFS, "/ota.html", "text/html", false, processorOTA);
  });
  server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/about.html", "text/html");
  });
  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/wifimanager.html", "text/html", false, processorWIFI);
  });
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    scanWifi();
    request->redirect("/");
  });
  server.on("/list", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        String FileName = "/bin/" + p->name();
        if(SPIFFS.exists(FileName)){
          SPIFFS.remove(FileName);
        }
      }
    }
    request->redirect("/list");
  });
  server.on("/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    String FileProgramm;
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        if(p->name()=="files"){
          FileProgramm = p->value().c_str();
          Serial.println(FileProgramm);
        }
        if(p->name()=="mcu1"){
          mcu = 1;
        }else if(p->name()=="mcu2"){
          mcu = 0;
        }
        Serial.println(p->name());
      }
    }
    String result = handleFlash(FileProgramm);
    request->send(200, "text/plain", result);
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
        /*
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
        }*/
      }
    }
    WiFi.begin(ssid.c_str(), pass.c_str());
    delay(1000);
    request->redirect("/");
  });
  server.begin();
}


void loop() {
  //server.handleClient();
}