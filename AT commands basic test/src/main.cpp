#include <Arduino.h>

#define SerialAT Serial1
#define RXD2 16
#define TXD2 17
#define PWRKEY 26

const char* APN = "airtelgprs.com";

// InfluxDB
const char* INFLUX_URL =
"https://us-east-1-1.aws.cloud2.influxdata.com/api/v2/write?org=GCT&bucket=machines_lb_power_monitoring&precision=s";

const char* INFLUX_TOKEN =
"d0Lg7WGZLNhj5ibHEQoxAzRwrT4HgqvZJj0WSK6_1QxwLVOi87uTFmz6FyNO0z0SNgvbgrr9BqmJXfaY6APuSg==";

void sendAT(const char* cmd, int wait=1000)
{
  SerialAT.println(cmd);
  delay(wait);
  while (SerialAT.available()) Serial.write(SerialAT.read());
}

void modemPowerOn()
{
  pinMode(PWRKEY, OUTPUT);
  digitalWrite(PWRKEY, LOW);
  delay(1000);
  digitalWrite(PWRKEY, HIGH);
  delay(6000);
}

void setup()
{
  Serial.begin(115200);
  SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2);

  modemPowerOn();

  sendAT("AT");
  sendAT("ATE0");
  sendAT("AT+CPIN?");
  sendAT("AT+CSQ");
  sendAT("AT+CREG?");
  sendAT("AT+CGATT=1",3000);

  // PDP context
  sendAT("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\"");
  sendAT("AT+CGACT=1,1",4000);

  // TLS config (REQUIRED)
  sendAT("AT+CSSLCFG=\"sslversion\",0,4");  // TLS 1.2
  sendAT("AT+CSSLCFG=\"authmode\",0,0");
  sendAT("AT+CSSLCFG=\"ignorelocaltime\",0,1");

  // HTTP Init
  sendAT("AT+HTTPTERM");
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"CID\",1");
  sendAT("AT+HTTPPARA=\"URL\",\"https://us-east-1-1.aws.cloud2.influxdata.com/api/v2/write?org=GCT&bucket=machines_lb_power_monitoring&precision=s\"");
  sendAT("AT+HTTPPARA=\"CONTENT\",\"text/plain\"");

  SerialAT.print("AT+HTTPPARA=\"USERDATA\",\"Authorization: Token ");
  SerialAT.print(INFLUX_TOKEN);
  SerialAT.println("\"");
  delay(1000);

  randomSeed(analogRead(0));
}

void loop()
{
  float V = random(2200,2400)/10.0;
  float I = random(5,20);
  float F = random(495,505)/10.0;
  float P = random(100,500)/10.0;

  String payload = "power_data,feeder=1 ";
  payload += "Vavg=" + String(V,2) + ",";
  payload += "Iavg=" + String(I,2) + ",";
  payload += "Frequency=" + String(F,2) + ",";
  payload += "Kw=" + String(P,2) + ",";
  payload += "Pf=0.95,VTHD=2,CTHD=3,RSSI=-60";

  Serial.println("\nSending:");
  Serial.println(payload);

  SerialAT.print("AT+HTTPDATA=");
  SerialAT.print(payload.length());
  SerialAT.println(",10000");
  delay(2000);

  SerialAT.print(payload);
  delay(1000);

  sendAT("AT+HTTPACTION=1",8000);   // POST
  sendAT("AT+HTTPREAD",3000);

  delay(10000);
}
