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
    String flashhtml = "<h1>Programming</h1><h2>" + flashwr +  "<br><br><a style=\"color:white\" href=\"/\">Back home to RUN MCU</a></h2>";
    request->send(200, "text/html", makePage("Flash Page", flashhtml));
  }
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(!index){
    if (!filename.startsWith("/")) filename = "/" + filename;
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
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
    request->send(200, "text/html", makePage("Deleted", "<h2>" + FileList + " be deleted!<br><br><a style=\"color:white\" href=\"/\">Return </a></h2>"));
    SPIFFS.remove(FName);
  }
  else{
    return request->send(404, "text/html", makePage("File Not found", "<h2>File Not found<br><br><a style=\"color:white\" href=\"/\">Return </a></h2>"));
  }
}

String handleListFiles()
{
  String FileList;
  String Listcode;
  FileList += " File: ";
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    String FileName = file.name();
    File f = SPIFFS.open("/"+FileName,"r");
    String FileSize = String(f.size());
    int whsp = 6 - FileSize.length();
    while (whsp-- > 0)
    {
      FileList += " ";
    }
    FileList +=  FileName + "   Size: " + FileSize + " bytes <br>";
    file = root.openNextFile();
  }
  Listcode = "<h2>List STM32 BinFile</h2><h2>" + FileList + "<br><br><a style=\"color:white\" href=\"/delete\">Delete BinFile </a>";
  return Listcode;
}

void initMCU (){
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
  }
  else if (rdtmp == STM32NACK) {
    Serial.write(STM32INIT);
    while (Serial.available() == 0);
    rdtmp = Serial.read();
    delay(10);
    if (rdtmp == STM32ACK){
      stringtmp = STM32_CHIPNAME[stm32GetId()];
    }
  }
  else{
    stringtmp = "ERROR";
  }
}
void FlashMode()  {    //Tested  Change to flashmode
  if (Runflag == 1) {
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
  }
}

void RunMode()  {    //Tested  Change to runmode
  if(Runflag==0){
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
  }
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
bool initWiFi() {
  if(ssid=="" || ip==""){
    Serial.println("Undefined SSID or IP address.");
    return false;
  }
  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());


  if (!WiFi.config(localIP, localGateway, subnet)){
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }
  Serial.println(WiFi.localIP());
  return true;
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200, SERIAL_8E1);
  pinMode(BOOT01, OUTPUT);
  pinMode(NRST1, OUTPUT);
  pinMode(BOOT02, OUTPUT);
  pinMode(NRST2, OUTPUT);
  initSPIFFS();	
  WiFi.mode(WIFI_MODE_APSTA);
  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  gateway = readFile (SPIFFS, gatewayPath);
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(gateway);


  if(initWiFi()) {
    // OTA route
    server.on("/programm", HTTP_GET, handleFlash);
    server.on("/run", HTTP_GET, [](AsyncWebServerRequest *request) {
      String Runstate ;
      Runstate += "<h2>STM32 Restart and runing!<br><br></h2>";
      if (Runflag == 0) {
        RunMode();
        Runflag = 1;
      }
      request->send(200, "text/html", makePage("Run",  Runstate + "<h2><a style=\"color:white\" href=\"/\"> Home </a></h2>"));
    });
    server.on("/erase", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (stm32Erase() == STM32ACK)
        stringtmp = "<h1>Erase OK</h1><h2><a style=\"color:white\" href=\"/\">Return </a></h2>";
      else if (stm32Erasen() == STM32ACK)
        stringtmp = "<h1>Erase OK</h1><h2><a style=\"color:white\" href=\"/\">Return </a></h2>";
      else
        stringtmp = "<h1>Erase failure</h1><h2><a style=\"color:white\" href=\"/\">Return </a></h2>";
      request->send(200, "text/html", makePage("Erase page", stringtmp));
    });
    server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
      handleFileDelete(request);
    });

    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", makePage("FileList", "<h1> Uploaded OK </h1><br><br><h2><a style=\"color:white\" href=\"/\">Return </a></h2>"));
    }, handleUpload);
    server.on("/stm1", HTTP_GET ,[](AsyncWebServerRequest *request){
      mcu = 1;
      initMCU();
      String starthtml = "<h1><a style=\"color:white\" href=\"/\">BACK HOME</a><h1>BOOT STATE</h1><h1>1st MCU</h1></h1><h2><br><a style=\"color:white\" href=\"/programm\">Programm MCU</a><br><br><a style=\"color:white\" href=\"/erase\">Erase MCU</a></h2>";
      request->send(200,"text/html",makePage("MCU 1",starthtml + "- Init MCU -<br> " + stringtmp));
    });

    server.on("/stm2",HTTP_GET,[](AsyncWebServerRequest *request){
      mcu = 0;
      initMCU();
      String starthtml = "<h1><a style=\"color:white\" href=\"/\">BACK HOME</a><h1>BOOT STATE</h1><h1>2nd MCU</h1></h1><h2><br><a style=\"color:white\" href=\"/programm\">Programm MCU</a><br><br><a style=\"color:white\" href=\"/erase\">Erase MCU</a></h2>";
      request->send(200,"text/html",makePage("MCU 2",starthtml + "- Init MCU -<br> " + stringtmp));
    });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      RunMode();
      String starthtml = "<h1>STM32-OTA</h1><h1>RUN STATE</h1><h2><a style=\"color:white\" href=\"/stm1\">1.First STM32</a><br><br><a style=\"color:white\" href=\"/stm2\">2.Second STM32</a></h2><br><h2>Upload STM32 BinFile</h2><h2><form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Upload'></form></h2>";
      String temp = handleListFiles();
      starthtml += temp;
      request->send(200,"text/html",makePage("MCU 2",starthtml + "- Init MCU -<br> " + stringtmp));
    });
    server.serveStatic("/", SPIFFS, "/");
    
    server.begin();
  }
  else {
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ESP-WIFI-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });
    
    server.serveStatic("/", SPIFFS, "/");
    
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
          //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(2000);
      ESP.restart();
    });
    server.begin();
  }
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
void loop() {
  //server.handleClient();
}