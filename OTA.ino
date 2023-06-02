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

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include "SPIFFS.h"
#include "stm32ota.h"

const String STM32_CHIPNAME[8] = {
  "Unknown Chip",
  "STM32F103x8/B",
};

#define NRST1 22
#define BOOT01 23
#define NRST2 19
#define BOOT02 18

const char* ssid = "Xiaomi 11T";
const char* password = "0123456789";
/*
IPAddress local_IP(10, 45, 172, 102);
IPAddress gateway(10, 45, 0, 1);
IPAddress subnet(255, 255, 0, 0);
*/
WebServer server(80);
File fsUploadFile;
uint8_t binread[256];
int bini = 0;
String stringtmp;
int rdtmp = 0;
bool Runflag = 0;
bool mcu = 1;

void handleFlash()
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
    server.send(200, "text/html", makePage("Flash Page", flashhtml));
  }
}
void handleFileUpload()
{
  if (server.uri() != "/upload") return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
  }
}
void handleFileDelete() {
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
    server.send(200, "text/html", makePage("Deleted", "<h2>" + FileList + " be deleted!<br><br><a style=\"color:white\" href=\"/\">Return </a></h2>"));
    SPIFFS.remove(FName);
  }
  else{
    return server.send(404, "text/html", makePage("File Not found", "<h2>File Not found<br><br><a style=\"color:white\" href=\"/\">Return </a></h2>"));
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
void setup(void)
{
  if(!SPIFFS.begin(1)){
        Serial.println("SPIFFS Mount Failed");
        return;
  }
  WiFi.mode(WIFI_AP_STA);
  /* start SmartConfig */
  WiFi.beginSmartConfig();
 
  /* Wait for SmartConfig packet from mobile */
  while (!WiFi.smartConfigDone()) {
    delay(500);
    Serial.print(".");
  }
  /* Wait for WiFi to connect to AP */
  //Serial.println("Waiting for WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Connected.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.begin(115200, SERIAL_8E1);
  pinMode(BOOT01, OUTPUT);
  pinMode(NRST1, OUTPUT);
  pinMode(BOOT02, OUTPUT);
  pinMode(NRST2, OUTPUT);
  //WiFi.mode(WIFI_STA);
  //WiFi.begin(sid, passwords);

  RunMode();
  if (WiFi.waitForConnectResult() == WL_CONNECTED)
  {
    server.on("/programm", HTTP_GET, handleFlash);
    server.on("/run", HTTP_GET, []() {
      String Runstate ;
      Runstate += "<h2>STM32 Restart and runing!<br><br></h2>";
      if (Runflag == 0) {
        RunMode();
        Runflag = 1;
      }
      server.send(200, "text/html", makePage("Run",  Runstate + "<h2><a style=\"color:white\" href=\"/\"> Home </a></h2>"));
    });
    server.on("/erase", HTTP_GET, []() {
      if (stm32Erase() == STM32ACK)
        stringtmp = "<h1>Erase OK</h1><h2><a style=\"color:white\" href=\"/\">Return </a></h2>";
      else if (stm32Erasen() == STM32ACK)
        stringtmp = "<h1>Erase OK</h1><h2><a style=\"color:white\" href=\"/\">Return </a></h2>";
      else
        stringtmp = "<h1>Erase failure</h1><h2><a style=\"color:white\" href=\"/\">Return </a></h2>";
      server.send(200, "text/html", makePage("Erase page", stringtmp));
    });
    server.on("/delete", HTTP_GET, handleFileDelete);
    server.onFileUpload(handleFileUpload);
    server.on("/upload", HTTP_POST, []() {
      server.send(200, "text/html", makePage("FileList", "<h1> Uploaded OK </h1><br><br><h2><a style=\"color:white\" href=\"/\">Return </a></h2>"));
    });
    server.on("/stm1", HTTP_GET ,[](){
      mcu = 1;
      initMCU();
      String starthtml = "<h1><a style=\"color:white\" href=\"/\">BACK HOME</a><h1>BOOT STATE</h1><h1>1st MCU</h1></h1><h2><br><a style=\"color:white\" href=\"/programm\">Programm MCU</a><br><br><a style=\"color:white\" href=\"/erase\">Erase MCU</a></h2>";
      server.send(200,"text/html",makePage("MCU 1",starthtml + "- Init MCU -<br> " + stringtmp));
    });

    server.on("/stm2",HTTP_GET,[](){
      mcu = 0;
      initMCU();
      String starthtml = "<h1><a style=\"color:white\" href=\"/\">BACK HOME</a><h1>BOOT STATE</h1><h1>2nd MCU</h1></h1><h2><br><a style=\"color:white\" href=\"/programm\">Programm MCU</a><br><br><a style=\"color:white\" href=\"/erase\">Erase MCU</a></h2>";
      server.send(200,"text/html",makePage("MCU 2",starthtml + "- Init MCU -<br> " + stringtmp));
    });
    server.on("/", HTTP_GET, []() {
      RunMode();
      String starthtml = "<h1>STM32-OTA</h1><h1>RUN STATE</h1><h2><a style=\"color:white\" href=\"/stm1\">1.First STM32</a><br><br><a style=\"color:white\" href=\"/stm2\">2.Second STM32</a></h2><br><h2>Upload STM32 BinFile</h2><h2><form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Upload'></form></h2>";
      String temp = handleListFiles();
      starthtml += temp;
      server.send(200, "text/html", makePage("Start Page", starthtml));
    });
    server.begin();
    Serial.println(WiFi.localIP());
  }
}

void loop(void) {
  server.handleClient();
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