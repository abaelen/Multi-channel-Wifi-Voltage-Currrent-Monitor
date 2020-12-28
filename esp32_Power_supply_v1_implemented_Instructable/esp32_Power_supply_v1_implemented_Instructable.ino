/*
  FSWebServer - Example WebServer with FS backend for esp8266/esp32
  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  upload the contents of the data folder with MkSPIFFS Tool ("ESP32 Sketch Data Upload" in Tools menu in Arduino IDE)
  or you can upload the contents of a folder if you CD in that folder and run the following command:
  for file in `ls -A1`; do curl -F "file=@$PWD/$file" esp32fs.local/edit; done

  access the sample web page at http://esp32fs.local
  edit the page by going to http://esp32fs.local/edit
*/
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
//#include <ESPmDNS.h>
#include <Adafruit_INA260.h>


#define FILESYSTEM SPIFFS
// You only need to format the filesystem once
#define FORMAT_FILESYSTEM false
#define DBG_OUTPUT_PORT Serial

#if FILESYSTEM == FFat
#include <FFat.h>
#endif
#if FILESYSTEM == SPIFFS
#include <SPIFFS.h>
#endif

//Webserver constants
const char* ssid = "fill the ssid of your router";
const char* password = "fill pass of your router";
const char* host = "esp32fs";
WebServer server(80);
//holds the current upload
File fsUploadFile;

//hold datamsg
struct ch {
  float Vset=0;
  float Imax=0;
  float Wmax=0;
  float Vmax=0;
  float IAmpl=0;
  float VAmpl=0;
  float Ioff=0;
  float VDc=0;
  float IDc=0;
  int VCh=99;
  int ICh=99;
  int VPin=99;
  int IPin = 99;
} Ch[3];
//Calibration structure  
  ch Calib;

//setting PWM properties
const int freq=15000;
const int resolution=12;
float operVoltage=3.3;

//INA260 constants
Adafruit_INA260 ina260_0x40 = Adafruit_INA260();
Adafruit_INA260 ina260_0x41 = Adafruit_INA260();
Adafruit_INA260 ina260_0x44 = Adafruit_INA260();
float Vina[3]={0.0f,0.0f,0.0f};
float Iina[3]={0.0f,0.0f,0.0f};

bool ina260_0x40_active = false;
bool ina260_0x41_active = false;
bool ina260_0x44_active = false;

//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

bool exists(String path){
  bool yes = false;
  File file = FILESYSTEM.open(path, "r");
  if(!file.isDirectory()){
    yes = true;
  }
  file.close();
  return yes;
}

bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if (path.endsWith("/")) {
    path += "index.htm";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (exists(pathWithGz) || exists(path)) {
    if (exists(pathWithGz)) {
      path += ".gz";
    }
    File file = FILESYSTEM.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload() {
  if (server.uri() != "/edit") {
    return;
  }
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
    fsUploadFile = FILESYSTEM.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    DBG_OUTPUT_PORT.print(F("handleFileUpload Size: ")); DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void handleFileDelete() {
  if (server.args() == 0) {
    return server.send(500, F("text/plain"), F("BAD ARGS"));
  }
  String path = server.arg(0);
  DBG_OUTPUT_PORT.print(F("handleFileDelete: "));DBG_OUTPUT_PORT.println(path);
  if (path == "/") {
    return server.send(500, F("text/plain"), F("BAD PATH"));
  }
  if (!exists(path)) {
    return server.send(404, F("text/plain"), F("FileNotFound"));
  }
  FILESYSTEM.remove(path);
  server.send(200, F("text/plain"), "");
  path = String();
}

void handleFileCreate() {
  if (server.args() == 0) {
    return server.send(500, F("text/plain"), F("BAD ARGS"));
  }
  String path = server.arg(0);
  DBG_OUTPUT_PORT.print(F("handleFileCreate: ")); DBG_OUTPUT_PORT.println(path);
  if (path == "/") {
    return server.send(500, F("text/plain"), F("BAD PATH"));
  }
  if (exists(path)) {
    return server.send(500, F("text/plain"), F("FILE EXISTS"));
  }
  File file = FILESYSTEM.open(path, "w");
  if (file) {
    file.close();
  } else {
    return server.send(500, F("text/plain"), F("CREATE FAILED"));
  }
  server.send(200, F("text/plain"), "");
  path = String();
}

void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, F("text/plain"), F("BAD ARGS"));
    return;
  }

  String path = server.arg("dir");
  DBG_OUTPUT_PORT.print("handleFileList: "); DBG_OUTPUT_PORT.print(path);


  File root = FILESYSTEM.open(path);
  path = String();

  String output = "[";
  if(root.isDirectory()){
      File file = root.openNextFile();
      while(file){
          if (output != "[") {
            output += ',';
          }
          output += "{\"type\":\"";
          output += (file.isDirectory()) ? "dir" : "file";
          output += "\",\"name\":\"";
          output += String(file.name()).substring(1);
          output += "\"}";
          file = root.openNextFile();
      }
  }
  output += "]";
  server.send(200, "text/json", output);
}

void setup(void) {
  while (!Serial);
  //configure PWM functionalities - immediately set outputs to zero to avoid overcurrent scenarios
  //to do: configure set to zero state when wifi connection lost

  Ch[0].VCh=0;Ch[0].VPin=18;
  Ch[0].ICh=2;Ch[0].IPin=19;
  Ch[1].VCh=1;Ch[1].VPin=26;
  Ch[1].ICh=3;Ch[1].IPin=25;
  Ch[2].VCh=99;Ch[2].VPin=99;
  Ch[2].ICh=4;Ch[2].IPin=27;


  
  for (int i=0;i<(sizeof(Ch)/sizeof(Ch[0]));i++) { 
    ledcSetup(Ch[i].VCh,freq,resolution);
    ledcSetup(Ch[i].ICh,freq,resolution);
    ledcAttachPin(Ch[i].VPin, Ch[i].VCh);
    ledcAttachPin(Ch[i].IPin, Ch[i].ICh);
    ledcWrite(Ch[i].VCh,0);
    ledcWrite(Ch[i].ICh,0);
  }
  
  //create randomseed for test data creation
  //randomSeed(analogRead(0));

  
  
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.print("\n");
  DBG_OUTPUT_PORT.setDebugOutput(true);
  if (FORMAT_FILESYSTEM) FILESYSTEM.format();
  FILESYSTEM.begin();
  {
      File root = FILESYSTEM.open("/");
      File file = root.openNextFile();
      while(file){
          String fileName = file.name();
          size_t fileSize = file.size();
          DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
          file = root.openNextFile();
      }
      DBG_OUTPUT_PORT.printf("\n");
  }


  //WIFI INIT
  DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DBG_OUTPUT_PORT.print(".");
  }
  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print(F("Connected! IP address: "));
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  //MDNS.begin(host);
  DBG_OUTPUT_PORT.print(F("Open http://"));
  DBG_OUTPUT_PORT.print(host);
  DBG_OUTPUT_PORT.println(F(".local/edit to see the file browser"));


  //SERVER INIT
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) {
      server.send(404, F("text/plain"), F("FileNotFound"));
    }
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  //called when the url is not defined here
  //use it to load content from FILESYSTEM
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, F("text/plain"), F("FileNotFound"));
    }
  });

  //get heap status, analog input value and all GPIO statuses in one json call
  server.on("/all", HTTP_GET, []() {
    String json = "{";
    json += "\"heap\":" + String(ESP.getFreeHeap());
    json += ", \"analog\":" + String(analogRead(A0));
    json += ", \"gpio\":" + String((uint32_t)(0));
    json += "}";
    server.send(200, "text/json", json);
    json = String();
  });

  server.on("/SndUpdate", HTTP_GET, [] () {
    Ch[1].Vset = server.arg("PS1_Vset").toFloat();
    Ch[1].Imax = server.arg("PS1_Imax").toFloat();
    Ch[1].Wmax = server.arg("PS1_Wmax").toFloat();
    Ch[1].IAmpl = server.arg("PS1_IAmpl").toFloat();
    Ch[1].VAmpl = server.arg("PS1_VAmpl").toFloat();
    Ch[0].Vset = server.arg("PS2_Vset").toFloat();
    Ch[0].Imax = server.arg("PS2_Imax").toFloat();
    Ch[0].Wmax = server.arg("PS2_Wmax").toFloat();
    Ch[0].VAmpl = server.arg("PS2_VAmpl").toFloat();
    Ch[0].IAmpl = server.arg("PS2_IAmpl").toFloat();
    Ch[2].Vset = server.arg("PS3_Vset").toFloat();
    Ch[2].Imax = server.arg("PS3_Imax").toFloat();
    Ch[2].Wmax = server.arg("PS3_Wmax").toFloat();
    Ch[2].VAmpl = server.arg("PS3_VAmpl").toFloat();
    Ch[2].IAmpl = server.arg("PS3_IAmpl").toFloat();
    operVoltage = server.arg("operVoltage").toFloat();
    Ch[1].Ioff = server.arg("PS1_Ioff").toFloat();
    Ch[0].Ioff = server.arg("PS2_Ioff").toFloat();
    Ch[2].Ioff = server.arg("PS3_Ioff").toFloat();
    //float rnd;
    String Msg = "{";
    Msg+="\"PG1_yrand0\":"+ (String) Vina[1];
    Msg+=",\"PG2_yrand0\":"+ (String) Iina[1];
    Msg+=",\"PG3_yrand0\":"+ (String) Vina[0];
    Msg+=",\"PG4_yrand0\":"+ (String) Iina[0];
    Msg+=",\"PG5_yrand0\":"+ (String) Vina[2];
    Msg+=",\"PG6_yrand0\":"+ (String) Iina[2];
    Msg+="}"; 
    //Serial.print("Vina[1]"); Serial.print(Vina[1]);Serial.print("Iina[1]"); Serial.println(Iina[1]);
    //Serial.print("Vina[0]"); Serial.print(Vina[0]);Serial.print("Iina[0]"); Serial.println(Iina[0]);
    //Serial.print("Vina[2]"); Serial.print(Vina[2]);Serial.print("Iina[2]"); Serial.println(Iina[2]);
    server.send(200, "text/json", Msg);
  });
  
  server.begin();
  DBG_OUTPUT_PORT.println(F("HTTP server started"));


//INA260 initialization
  if (!ina260_0x40.begin(0x40)) {
    Serial.println(F("Couldn't find INA260 0x40 chip"));
    //while (1);
  } else {
      ina260_0x40_active = true;
      ina260_0x40.setAveragingCount(INA260_COUNT_256);
      ina260_0x40.setVoltageConversionTime(INA260_TIME_1_1_ms);
      ina260_0x40.setCurrentConversionTime(INA260_TIME_1_1_ms);
      ina260_0x40.setMode(INA260_MODE_CONTINUOUS);
  }
  Serial.println(F("Found INA260 chip 0x40"));
  if (!ina260_0x41.begin(0x41)) {
    Serial.println(F("Couldn't find 0x41 INA260 chip"));
    //while (1);
  } else {
      ina260_0x41_active = true;
      ina260_0x41.setAveragingCount(INA260_COUNT_256);
      ina260_0x41.setVoltageConversionTime(INA260_TIME_1_1_ms);
      ina260_0x41.setCurrentConversionTime(INA260_TIME_1_1_ms);
      ina260_0x41.setMode(INA260_MODE_CONTINUOUS);
  }

  
  Serial.println(F("Found INA260 0x41 chip"));
  if (!ina260_0x44.begin(0x44)) {
    Serial.println(F("Couldn't find INA260 0x44 chip"));
    //while (1);
  } else {
    ina260_0x44_active = true;
    ina260_0x44.setAveragingCount(INA260_COUNT_256);
    ina260_0x44.setVoltageConversionTime(INA260_TIME_1_1_ms);
    ina260_0x44.setCurrentConversionTime(INA260_TIME_1_1_ms);
    ina260_0x44.setMode(INA260_MODE_CONTINUOUS);
  }
  Serial.println(F("Found INA260 chip 0x44"));

  
}
int prtr=0;
void loop(void) {
  server.handleClient();
  //to do: create calibration procedure to set calc max voltage
  //add change condition to avoid continuous change of Dc setting
  for (int i=0;i<(sizeof(Ch)/sizeof(Ch[0]));i++) {
    if (round(Ch[i].VDc) != round(((Ch[i].Vset-1.25)/(Ch[i].VAmpl+1))/operVoltage*(pow(2,resolution)-1))) {
      if(i==1) {
        prtr=1;
        Calib.VDc=Ch[1].VDc;
      }
      Ch[i].VDc = ((Ch[i].Vset-1.25)/(Ch[i].VAmpl+1))/operVoltage*(pow(2,resolution)-1);
    }
    ledcWrite(Ch[i].VCh, (Ch[i].VDc> (pow(2,resolution)-1)) ? (pow(2,resolution)-1) : Ch[i].VDc);
    float IDc =0;
    if (i==2) {IDc = ((Ch[i].Ioff/1000.0+Ch[i].Imax/1000.0*Ch[i].IAmpl)/operVoltage)*(pow(2,resolution)-1);} //123mV offset on the PRC channel
    if (i==1) 
    {
      IDc=Ch[i].Ioff/1000.0+Ch[i].Imax/1000.0;
      //IDc = (Ch[i].Imax - 5.88466 - 0.29979*Ch[i].Vset)/(0.954864-1.189170614/Ch[i].Vset);
      //Serial.print("IDc:");Serial.println(IDc);
      IDc = ((IDc*Ch[i].IAmpl)/operVoltage)*(pow(2,resolution)-1); //123mV offset on the PRC channel
      
    }
    if (i==0) {IDc = ((Ch[i].Ioff/1000.0+Ch[i].Imax/1000.0*Ch[i].IAmpl)/operVoltage)*(pow(2,resolution)-1);} //123mV offset on the PRC channel
    if (round(Ch[i].IDc) != round(IDc)) {
      if (i==1) {
        prtr=1;
        Calib.IDc=Ch[1].IDc;
      }
      Ch[i].IDc = IDc;
    } //50mV offset as lowest value & slope offset for pull down resistor
    ledcWrite(Ch[i].ICh, (Ch[i].IDc> (pow(2,resolution)-1)) ? (pow(2,resolution)-1) : Ch[i].IDc);
    //Serial.print("channel: "); Serial.print(i); Serial.print(" - Vset: "); Serial.print(Ch[i].Vset); Serial.print(" - Imax: "); Serial.print(Ch[i].Imax); Serial.print(" - IAmpl"); Serial.println(Ch[i].IAmpl);
  }
    //while (!ina260_0x40.conversionReady() || !ina260_0x41.conversionReady() || !ina260_0x44.conversionReady());
    if (ina260_0x40_active == true) 
    {
      Vina[0]=ina260_0x40.readBusVoltage()/1000.0f;
      Iina[0]=ina260_0x40.readCurrent();
    }
    if (ina260_0x41_active == true) 
    {
      Vina[1]=ina260_0x41.readBusVoltage()/1000.0f;
      Iina[1]=ina260_0x41.readCurrent();
    }
    if (ina260_0x44_active == true) 
    {
      Vina[2]=ina260_0x44.readBusVoltage()/1000.0f;
      Iina[2]=ina260_0x44.readCurrent();
    }
    
    if (prtr==1){
      Serial.print(operVoltage);Serial.print(";");Serial.print(Calib.VDc);Serial.print(";");Serial.print(Calib.IDc);Serial.print(";");Serial.println(Iina[1]);
      prtr=0;
    }
}
