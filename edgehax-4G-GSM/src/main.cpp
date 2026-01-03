#include <Arduino.h>
#include <ModbusMaster.h>
#include <EEPROM.h>

// =================== MODEM ===================
#define SerialAT Serial1
#define RXD2 16
#define TXD2 17
#define PWRKEY 26
const char* APN = "airtelgprs.com";

// Alert numbers
const char* ALERT_NUMBER1 = "+918072878465";   // FIXED format
const char* ALERT_NUMBER2 = "+918778665578";

// =================== INFLUX ==================
const char* INFLUX_URL =
"https://us-east-1-1.aws.cloud2.influxdata.com/api/v2/write?org=GCT&bucket=machines_lb_power_monitoring&precision=s";
const char* INFLUX_TOKEN =
"d0Lg7WGZLNhj5ibHEQoxAzRwrT4HgqvZJj0WSK6_1QxwLVOi87uTFmz6FyNO0z0SNgvbgrr9BqmJXfaY6APuSg==";

// =================== RS485 / MODBUS ==================
#define MAX485_DE 4
#define MAX485_RE_NEG 15
#define MODBUS_RX 3
#define MODBUS_TX 1

HardwareSerial ModbusSerial(2);
ModbusMaster node;

// =================== ALERT FLAGS ==================
bool powerAlertSent = false;
bool voltageAlertSent = false;
bool pfAlertSent = false;
bool thdAlertSent = false;

// =================== MODBUS ==================
void preTransmission() {
  digitalWrite(MAX485_RE_NEG, 1);
  digitalWrite(MAX485_DE, 1);
}
void postTransmission() {
  delayMicroseconds(200);
  digitalWrite(MAX485_RE_NEG, 0);
  digitalWrite(MAX485_DE, 0);
}

// =================== AT ==================
void sendAT(const char* cmd, int wait = 1000) {
  SerialAT.println(cmd);
  delay(wait);
  while (SerialAT.available()) SerialAT.read();
}

void modemPowerOn() {
  pinMode(PWRKEY, OUTPUT);
  digitalWrite(PWRKEY, LOW);
  delay(1000);
  digitalWrite(PWRKEY, HIGH);
  delay(6000);
}

// =================== HTTPS ==================
void sendHTTPS(String payload) {
  sendAT("AT+HTTPTERM");
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"CID\",1");
  SerialAT.print("AT+HTTPPARA=\"URL\",\""); SerialAT.print(INFLUX_URL); SerialAT.println("\"");
  sendAT("AT+HTTPPARA=\"CONTENT\",\"text/plain\"");
  SerialAT.print("AT+HTTPPARA=\"USERDATA\",\"Authorization: Token ");
  SerialAT.print(INFLUX_TOKEN);
  SerialAT.println("\"");

  delay(1000);

  SerialAT.print("AT+HTTPDATA=");
  SerialAT.print(payload.length());
  SerialAT.println(",10000");
  delay(2000);
  SerialAT.print(payload);
  delay(1000);

  sendAT("AT+HTTPACTION=1", 8000);
  sendAT("AT+HTTPREAD", 3000);
}

// =================== MODBUS READ ==================
float readModbus(uint16_t reg) {
  if (node.readHoldingRegisters(reg, 2) == node.ku8MBSuccess) {
    uint32_t v = (node.getResponseBuffer(1) << 16) | node.getResponseBuffer(0);
    float f;
    memcpy(&f, &v, 4);
    return f;
  }
  return 0;
}

// =================== SMS ==================
void sendSMS(const char* number, String message) {
  sendAT("AT+CMGF=1");
  SerialAT.print("AT+CMGS=\""); SerialAT.print(number); SerialAT.println("\"");
  delay(1000);
  SerialAT.print(message);
  SerialAT.write(26);
  delay(5000);
}

// =================== ALERT ENGINE ==================
void checkAlerts(float V, float I, float P, float PF, float THD) {

  // Power
  if (P > 100 && !powerAlertSent) {
    String msg = "ALERT: Power High " + String(P, 2) + " kW";
    sendSMS(ALERT_NUMBER1, msg);
    sendSMS(ALERT_NUMBER2, msg);
    powerAlertSent = true;
  }
  if (P <= 100) powerAlertSent = false;

  // Voltage
  if (V < 200 && !voltageAlertSent) {
    String msg = "ALERT: Voltage Low " + String(V, 2) + " V";
    sendSMS(ALERT_NUMBER1, msg);
    sendSMS(ALERT_NUMBER2, msg);
    voltageAlertSent = true;
  }
  if (V >= 200) voltageAlertSent = false;

  // Power factor
  if (PF < 0.93 && !pfAlertSent) {
    String msg = "ALERT: PF Low " + String(PF, 2);
    sendSMS(ALERT_NUMBER1, msg);
    sendSMS(ALERT_NUMBER2, msg);
    pfAlertSent = true;
  }
  if (PF >= 0.93) pfAlertSent = false;

  // THD
  if (THD > 10 && !thdAlertSent) {
    String msg = "ALERT: High THD " + String(THD, 2) + " %";
    sendSMS(ALERT_NUMBER1, msg);
    sendSMS(ALERT_NUMBER2, msg);
    thdAlertSent = true;
  }
  if (THD <= 10) thdAlertSent = false;
}

// =================== SETUP ==================
void setup() {
  Serial.begin(115200);
  SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2);
  modemPowerOn();

  sendAT("AT");
  sendAT("ATE0");
  sendAT("AT+CPIN?");
  sendAT("AT+CREG?");
  sendAT("AT+CGATT=1", 3000);
  sendAT(("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"").c_str());
  sendAT("AT+CGACT=1,1", 4000);

  pinMode(MAX485_DE, OUTPUT);
  pinMode(MAX485_RE_NEG, OUTPUT);
  digitalWrite(MAX485_DE, 0);
  digitalWrite(MAX485_RE_NEG, 0);

  ModbusSerial.begin(9600, SERIAL_8E1, MODBUS_RX, MODBUS_TX);
  node.begin(1, ModbusSerial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
}

// =================== LOOP ==================
void loop() {

  int feeder = 1;

  float Vavg  = readModbus(0x8C);
  delay(100);
  float Iavg  = readModbus(0x94);
  delay(100);
  float Freq  = readModbus(0x9C);
  delay(100);
  float Kw    = readModbus(0x64);
  delay(100);
  float Pf    = readModbus(0x74);
  delay(100);
  float VTHD  = readModbus(0xB8);
  delay(100);
  float CTHD  = readModbus(0xBE);
  delay(100);

  checkAlerts(Vavg, Iavg, Kw, Pf, CTHD);

   int RSSI = random(-80,-40);
    String payload = "power_data,feeder=" + String(feeder);
  payload += " Vavg=" + String(Vavg,2);
  payload += ",Iavg=" + String(Iavg,2);
  payload += ",Frequency=" + String(Freq,2);
  payload += ",Kw=" + String(Kw,2);
  payload += ",Pf=" + String(Pf,2);
  payload += ",VTHD=" + String(VTHD,2);
  payload += ",CTHD=" + String(CTHD,2);
  payload += ",RSSI=" + String(RSSI);

  sendHTTPS(payload);

  delay(10000);
}
