// POST TEMP and HUM
// Version 1.4  (2023/11/23)
// Author: yoshi99w
#define DEBUG
//#define DEBUGc

#include <DHT20.h>
#include <esp_sntp.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>

#define uS_TO_S_FACTOR 1000000ULL
#define D_SIZE 6
#define Q_SIZE 5
#define L_SIZE 512

struct Log {
  time_t TIME;
  int    BC;
  int    ReWU;
  char   TEMP[D_SIZE];
  char   HUM[D_SIZE];
  char   CPU[D_SIZE];
};

RTC_DATA_ATTR int bootCount=-1;
RTC_DATA_ATTR int syncNTP=0;
RTC_DATA_ATTR int Qhead=0;
RTC_DATA_ATTR int Qnum=0;
RTC_DATA_ATTR Log queue[Q_SIZE];
RTC_DATA_ATTR char loglog[L_SIZE]="";

void Qin(Log d) {
  queue[(Qhead+Qnum)%Q_SIZE]=d;
  if(++Qnum>Q_SIZE) {Qnum--; Qhead=(++Qhead)%Q_SIZE;}
#ifdef DEBUG
  snprintf(loglog,L_SIZE,"%sQin:%d,%d BC:%d\n",loglog,Qhead,Qnum,bootCount);
#endif
}

bool Qout(Log *d) {
  if(Qnum==0) {return false;}
  *d=queue[Qhead];
  Qnum--; Qhead=(++Qhead)%Q_SIZE;
#ifdef DEBUG
  snprintf(loglog,L_SIZE,"%sQout:%d,%d BC:%d\n",loglog,Qhead,Qnum,bootCount);
#endif
  return true;
}

const char* WIFI_SSID    ="";
const char* WIFI_PASSWORD="";
const char* URL          ="";
const char* ywSVR        ="";

DHT20 DHT;

void setup() {
  Wire.begin();
  DHT.begin();
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN,OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);
#endif
#ifdef DEBUGc
  Serial.begin(115200);
#endif
  delay(1000);

  if(++bootCount==0) {
    snprintf(loglog,L_SIZE,"bc %08x\nsn %08x\nqh %08x\nqn %08x\nq0 %08x\nqe %08x\nll %08x\n"
      ,&bootCount,&syncNTP,&Qhead,&Qnum,&queue[0],&queue[Q_SIZE-1],&loglog);
  }

  int wakeup_reason=esp_sleep_get_wakeup_cause();
#ifdef DEBUG
  snprintf(loglog,L_SIZE,"%sBoot number: %d by %d\n",loglog,bootCount,wakeup_reason);
#endif
#ifdef DEBUGc
  Serial.println("Boot number: "+String(bootCount)+" by "+String(wakeup_reason));
#endif

  configTzTime("JST-9",ywSVR,"ntp.nict.jp","ntp.jst.mfeed.ad.jp");
#ifndef DEBUGc
  if(bootCount>0) {getLOG(wakeup_reason);}
#else
  getLOG(wakeup_reason);
#endif

#ifdef DEBUGc
  Serial.printf("Connecting to %s\n",WIFI_SSID);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  int i=0;
  while(WiFi.status()!=WL_CONNECTED) {
    if(++i>20) {break;}
#ifdef LED_BUILTIN
    delay(300);
    digitalWrite(LED_BUILTIN,LOW);
    delay(200);
    digitalWrite(LED_BUILTIN,HIGH);
#else
    delay(500);
#endif
#ifdef DEBUGc
    Serial.print(".");
#endif
    if(i%2==0) {
      WiFi.disconnect();
      delay(100);
      //WiFi.reconnect();
      WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
    }
  }

  if(i<=20) {
#ifdef DEBUGc
    Serial.print("\nWiFi connected\n");
#endif

    if(--syncNTP<=0) {
#ifdef DEBUGc
      Serial.print("[NTP Svr] Connecting.");
#endif
      int j=0;
      while(sntp_get_sync_status()==SNTP_SYNC_STATUS_RESET) {
        if(++j>20) {break;}
#ifdef DEBUGc
        Serial.print(".");
#endif
        delay(1000);
      }
      if(j<=20) {
        syncNTP=8*60/5;
#ifdef DEBUGc
        Serial.println("\n[NTP Svr] Connected!");
#endif
        snprintf(loglog,L_SIZE,"%sNTP_SYNC %d\n",loglog,j);
      } else {
        if(bootCount==0) {ESP.restart();}
      }
    }

    postLOG();

    String mess=loglog;
    if(mess!="") {
#ifdef DEBUGc
      Serial.println(mess);
#endif
      if(smail(mess)) {loglog[0]=NULL;}
    }

    WiFi.disconnect();
#ifdef DEBUGc
    Serial.println("WiFi disconnected");
#endif
  } else {
    if(bootCount==0) {ESP.restart();}
  }

#ifdef DEBUGc
  Serial.println("Going to deep sleep");
  Serial.flush();
#endif
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN,LOW);  delay(300);
  digitalWrite(LED_BUILTIN,HIGH); delay(200);
  digitalWrite(LED_BUILTIN,LOW);  delay(300);
  digitalWrite(LED_BUILTIN,HIGH); delay(200);
  digitalWrite(LED_BUILTIN,LOW);  delay(300);
  digitalWrite(LED_BUILTIN,HIGH); delay(1200);
  digitalWrite(LED_BUILTIN,LOW);
#else
  delay(2500);
#endif
  struct tm localTime;
  getLocalTime(&localTime);
#ifndef DEBUG
  int t_sleep=300-(localTime.tm_min%5)*60-localTime.tm_sec;
#else
  int t_sleep=30-(localTime.tm_sec%30);
#endif
  esp_sleep_enable_timer_wakeup(t_sleep*uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {}

void getLOG(int ReWU) {
  Log dat;
  char buf[10];

  int status=DHT.read();

  dat.TIME=time(NULL);
  dat.BC  =bootCount;
  dat.ReWU=ReWU;
  //dat.ReWU=esp_sleep_get_wakeup_cause();
  snprintf(dat.TEMP,D_SIZE,dtostrf(DHT.getTemperature(),5,2,buf));
  snprintf(dat.HUM, D_SIZE,dtostrf(DHT.getHumidity(),   5,2,buf));
  snprintf(dat.CPU, D_SIZE,dtostrf(temperatureRead(),   5,2,buf));
#ifdef DEBUG
  snprintf(loglog,L_SIZE,"%sTEMP: %s HUM: %s CPU: %s\n",loglog,String(dat.TEMP),String(dat.HUM),String(dat.CPU));
#endif
#ifdef DEBUGc
  Serial.println("TEMP: "+String(dat.TEMP)+" HUM: "+String(dat.HUM)+" CPU: "+String(dat.CPU));
#endif

  Qin(dat);
}

void postLOG() {
  Log dat;
  StaticJsonDocument<192> buf;  // include Qhead,Qnum
  //StaticJsonDocument<128> buf;

#ifdef DEBUGc
  Serial.println("start postLOG");
#endif
  while(Qout(&dat)) {
#ifdef DEBUG
    snprintf(loglog,L_SIZE,"%spostLOG: %s\n",loglog,String(dat.BC));
#endif
    buf["TIME"]=dat.TIME;
    buf["BC"]  =dat.BC;
    buf["TEMP"]=dat.TEMP;
    buf["HUM"] =dat.HUM;
    buf["CPU"] =dat.CPU;
    buf["ReWU"]=dat.ReWU;
    buf["Qhed"]=Qhead;
    buf["Qnum"]=Qnum;

    WiFiClient client;
    HTTPClient http;

    String json="";
    serializeJson(buf,json);

    if (!http.begin(client,URL)) {
#ifdef DEBUGc
      Serial.println("Failed HTTPClient begin!");
#endif
      Qnum++; Qhead=(Qhead+Q_SIZE-1)%Q_SIZE;
      snprintf(loglog,L_SIZE,"%sQundo:%d,%d BC:%d\n",loglog,Qhead,Qnum,bootCount);
      return;
    } 
#ifdef DEBUGc
    Serial.println(json);
#endif
    http.addHeader("Content-Type","application/json");
    int responseCode=http.POST(json);
#ifdef DEBUGc
    String body = http.getString();

    Serial.println(responseCode);
    Serial.println(body);
#endif
    if(responseCode!=200) {
      Qnum++; Qhead=(Qhead+Q_SIZE-1)%Q_SIZE;
      snprintf(loglog,L_SIZE,"%sQundo:%d,%d BC:%d RC:%d\n",loglog,Qhead,Qnum,bootCount,responseCode);
      return;
    }

    http.end();

    delay(2000);
  }
#ifdef DEBUGc
  Serial.println("end postLOG");
#endif
}

bool smail(const String& mess) {
  WiFiClient client;
  if(!client.connect(ywSVR,25)) {
#ifdef DEBUGc
    Serial.println("Could not connect to SMTP");
#endif
    client.stop(); return false;
  }
  if(!rRes(client)) {client.stop(); return false;}
  client.println("HELO xxxxx");
  if(!rRes(client)) {client.stop(); return false;}
  client.println("MAIL FROM: <xxxxx@xxxxxxxxxxx>");
  if(!rRes(client)) {client.stop(); return false;}
  client.println("RCPT TO: <xxxxx@xxxxxxxxxxx>");
  if(!rRes(client)) {client.stop(); return false;}
  client.println("DATA");
  if(!rRes(client)) {client.stop(); return false;}
  client.println("From: <xxxxx@xxxxxxxxxxx>");
  client.println("To: <xxxxx@xxxxxxxxxxx>");
  client.println("Subject: DHT20post");
  client.println();
  client.println(mess);
  client.println(".");
  if(!rRes(client)) {client.stop(); return false;}
  client.println("QUIT");
  if(!rRes(client)) {client.stop(); return false;}
#ifdef DEBUGc
  Serial.println("Sending mail successful");
#endif
  return true;
}

bool rRes(WiFiClient &c) {
  int i=0;
  while(!c.available()) {
    delay(1);
    if(++i>10000) {return false;}
  }
  while(c.available()) {
    byte rbyte=c.read();
  }
  return true;
}
