/*
 
Компилировать как Arduino Pro mini 3.3v 8MHz
Прошивать: avrdude -B 125kHz -p m328p -c usbasp  -U flash:w:./file.hex:i -Uefuse:w:0xFD:m -Uhfuse:w:0xDA:m -Ulfuse:w:0xFF:m

Монитор для RADEX MR107+ (VID: abba PID: a104) с регистрацией в zabbix

Сырые данные при горячем старте:

7A FF 20 80 3A 00 83 04 00 00 A7 7B 04 08 00 00
30 00 00 00 08 00 28 00 B0 5C 0E 48 35 C4 6B 43
89 67 E2 41 3E F4 90 42 50 E7 F7 8F AE 00 00 00
8C 2E F2 42 40 E6 DD 42 35 C4 6B 43 00 00 00 00
D2 D0 00 00 00 00

Сырые данные при холодном старте:

7A FF 20 80 3A 00 83 04 00 00 A7 7B 04 08 00 00
30 00 00 00 09 00 00 00 00 00 08 00 00 00 00 00
89 67 E2 41 CA C3 80 42 C0 10 3C 99 AE 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 CA 45 47 47
35 38 00 00 00 00

zakaz@quarta-rad.ru

*/

#define MAGICKKEY 0x1234          // Magick key
#define USBHOSTSS 7               // Need relocate from 10 to 7 on USB host shield.
#define ZABBIXPORT 10051          // Zabbix server Port
#define ZABBIXADDR {192,168,1,6}  // Zabbix server IP, comma separated
#define ZABBIXMAXLEN 128
#define ZABBIXAGHOST "MR"         // Zabbix item's host name
#define ZABBIXSENDPERIOD 300      // Period in secoonds
#define ZABBRADONKEY "r"
#define ZABBTEMPERATUREKEY "t"
#define ZABBHUMMIDITYKEY "h"
#define ZABBPRESSUREKEY "p"
#define BUFFERSIZE 70
#define wdt_on true
#define LED_CHK 4
#define ETH_RESET A4
#define USB_RESET 10
#define MACADDR { 0x00, 0xAB, 0xBB, 0xCC, 0xDE, 0x02 }
#define SERIAL_OUT

#include "forcedBMX280.h"
#include <EEPROM.h>
#include <cdcacm.h>
#include <usbhub.h>
#define PIN_SPI_SS_ETHERNET_LIB 6
#include <Ethernet.h>

// Satisfy the IDE, which needs to see the include statment in the ino too.
//#ifdef dobogusinclude
//#include <spi4teensy3.h>
//#endif
#include <SPI.h>
#if wdt_on
#include <avr/wdt.h>
#endif

ForcedBMP280 climateSensor = ForcedBMP280();

struct cfgData {
  uint16_t mk;
  IPAddress zabbSrv;
  uint8_t keyID;
} configData;

//  ID abba:a104
union {
  uint32_t u32;
  float flt;
} mrdata;

uint16_t rcvd = BUFFERSIZE;
uint8_t  rec_buffer[BUFFERSIZE + 1], errorCount = 0;
String str;
unsigned long curTime = 0, tmpTime;

#if defined(WIZ550io_WITH_MACADDRESS) // Use assigned MAC address of WIZ550io
  ;
#else
  byte mac[] = MACADDR;
#endif
int len;
uint8_t res[ZABBIXMAXLEN];
EthernetClient client;

void sendToZabbix(String key, float val)
{
  str = "";
  str = str + "{\"request\":\"sender data\",\"data\":[{\"host\":\"";
  str = str + ZABBIXAGHOST + configData.keyID;
  str = str + "\",\"key\":\""; 
  str = str + key;
  str = str + "\",\"value\":\"";
  str = str + val;
  str = str + "\"}]}";
  len = str.length();
  res[0] = 'Z';
  res[1] = 'B';
  res[2] = 'X';
  res[3] = 'D';
  res[4] = 0x01;
  res[5] = str.length();
  res[6] = 0;
  res[7] = 0;
  res[8] = 0;
  res[9] = 0;
  res[10] = 0;
  res[11] = 0;
  res[12] = 0;
  str.getBytes(&(res[13]), ZABBIXMAXLEN - 12);
  len = len + 13;
  if (client.connect(configData.zabbSrv, ZABBIXPORT)) {
    client.write(res, len);
  } 
  client.stop();     
}

class ACMAsyncOper : public CDCAsyncOper {
public:
    uint8_t OnInit(ACM *pacm);
};

uint8_t ACMAsyncOper::OnInit(ACM *pacm) {
    uint8_t rcode;
    // Set DTR = 1 RTS=1
    rcode = pacm->SetControlLineState(3);

    if (rcode) {
        //ErrorMessage<uint8_t>(PSTR("SCS"), rcode);
        return rcode;
    }

    LINE_CODING  lc;
    lc.dwDTERate  = 115200;
    lc.bCharFormat  = 0;
    lc.bParityType  = 0;
    lc.bDataBits  = 8;
    rcode = pacm->SetLineCoding(&lc);
    if (rcode)
        //ErrorMessage<uint8_t>(PSTR("SetLineCoding"), rcode);
    return rcode;
}

USB     Usb;
ACMAsyncOper  AsyncOper;
ACM           Acm(&Usb, &AsyncOper);
uint8_t snd_buffer[] = {0x7b, 0xff, 0x20, 0x00, 0x06, 0x00, 0x83, 0x04, 0x00, 0x00, 0xda, 0xfb, 0x04, 0x08, 0x0c, 0x00, 0xef, 0xf7};

void setup() {
  pinMode(LED_CHK, OUTPUT);
  digitalWrite(LED_CHK, HIGH);
  pinMode(ETH_RESET, OUTPUT);
  digitalWrite(ETH_RESET, LOW);
  digitalWrite(ETH_RESET, HIGH);
  pinMode(PIN_SPI_SS_ETHERNET_LIB, OUTPUT);
  digitalWrite(PIN_SPI_SS_ETHERNET_LIB, HIGH);
  
  #if wdt_on
    MCUSR &= ~(1 << WDRF);
    wdt_disable();
  #endif
  /* Read EEPROM */
  EEPROM.get(0, configData);
  if (configData.mk != MAGICKKEY) {
    configData.mk = MAGICKKEY;
    configData.zabbSrv = ZABBIXADDR;
    configData.keyID = mac[5];
    EEPROM.put(0, configData);
  }

  Serial.begin( 9600 );
  while (!Serial);
  Serial.print("Z:");
  Serial.println(configData.zabbSrv);
  Serial.print("I:");
  Serial.println(configData.keyID);
  Serial.print("M:");
  for (int i= 0; i < 6; i++) {
    Serial.print(mac[i], HEX);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println();
  Serial.println("c - config");
  int waitSerial = 200;
  while (Serial.available() == 0 && waitSerial-- > 0) {
    delay(10);
  }
  if(Serial.available() > 0) {
    if (Serial.read() == 'c') {
      while (Serial.available() > 0) Serial.read();
      Serial.print("Z:");
      while (Serial.available() == 0);
      String IP = Serial.readString();
      String tmp = "";
      byte ipADDR[4] = {0,0,0,0};
      uint8_t addrIDX = 0;
      for (uint16_t ii = 0; ii < IP.length(); ii++) {
        if (IP[ii] != '.') {
          tmp = tmp + IP[ii];
        } else {
          ipADDR[addrIDX++] = tmp.toInt();
          tmp = "";
        }
      }
      ipADDR[addrIDX] = tmp.toInt();
      IPAddress IP1(ipADDR);
      Serial.println(IP1);
      Serial.print("I:");
      while (Serial.available() == 0);
      String ID = Serial.readString();
      Serial.println(ID);
      configData.zabbSrv = IP1;
      configData.keyID = ID.toInt();
      EEPROM.put(0, configData);
    }
  }
  Serial.println("R");
  climateSensor.begin();
  pinMode(USBHOSTSS, OUTPUT);
  digitalWrite(USBHOSTSS, HIGH); // Disable SS fo USB host.
  pinMode(USB_RESET, OUTPUT);
  digitalWrite(USB_RESET, LOW);
  digitalWrite(USB_RESET, HIGH);
  #if wdt_on
    wdt_enable(WDTO_8S);
  #endif
  Ethernet.init(PIN_SPI_SS_ETHERNET_LIB);
  #if defined(WIZ550io_WITH_MACADDRESS) // Use assigned MAC address of WIZ550io
    if (Ethernet.begin() == 0) {
  #else
    if (Ethernet.begin(mac) == 0) {
  #endif
      for(;;);
    }

  if (Usb.Init() == -1) {
      while ( 1 );
  }
  digitalWrite(LED_CHK, LOW);
  delay( 200 );
}

void loop() {
  uint8_t rcode;

  Usb.Task();
  #if wdt_on
    wdt_reset();
  #endif
  if( Acm.isReady()) {
    digitalWrite(LED_CHK, LOW);
    errorCount = 0;
    tmpTime = millis();
    if (curTime == 0 || tmpTime - curTime > (unsigned long) ZABBIXSENDPERIOD * 1000 ) {
      curTime = tmpTime;
      rcode = Acm.SndData(sizeof(snd_buffer), snd_buffer);
      if (rcode)
        //ErrorMessage<uint8_t>(PSTR("SndData"), rcode);
      delay(50);
      rcvd = BUFFERSIZE;
      for (uint16_t i = 0; i < sizeof(rec_buffer); i++)
        rec_buffer[i] = 0;
      rcode = Acm.RcvData(&rcvd, rec_buffer);
       if (rcode && rcode != hrNAK) {
          //ErrorMessage<uint8_t>(PSTR("Ret"), rcode);
       } else {
          if( rcvd == BUFFERSIZE ) {
            /* Радон */
            uint16_t X24 = (uint16_t) rec_buffer[25] << 8 | (uint16_t) rec_buffer[24];
            mrdata.u32 = (uint32_t) rec_buffer[31] << 24 | (uint32_t) rec_buffer[30] << 16 | (uint32_t) rec_buffer[29] << 8 | (uint32_t) rec_buffer[28];
            if (X24 > 0 ) {  // Холодный старт
              if (mrdata.flt > 30) {  // значение меньше 30Бк
                sendToZabbix(ZABBRADONKEY, mrdata.flt);
              } else {
                sendToZabbix(ZABBRADONKEY, 30.0);
              }
            } else {
            }
            /* Температура */
            #ifdef ZABBTEMPERATUREKEY
            mrdata.u32 = (uint32_t) rec_buffer[35] << 24 | (uint32_t) rec_buffer[34] << 16 | (uint32_t) rec_buffer[33] << 8 | (uint32_t) rec_buffer[32];
            sendToZabbix(ZABBTEMPERATUREKEY, mrdata.flt);
            #endif
            /* Влажность */
            #ifdef ZABBHUMMIDITYKEY
            mrdata.u32 = (uint32_t) rec_buffer[39] << 24 | (uint32_t) rec_buffer[38] << 16 | (uint32_t) rec_buffer[37] << 8 | (uint32_t) rec_buffer[36];
            sendToZabbix(ZABBHUMMIDITYKEY, mrdata.flt);
            #endif
            /* Давление с bmp280 */
            #ifdef ZABBPRESSUREKEY
            climateSensor.takeForcedMeasurement();
            sendToZabbix(ZABBPRESSUREKEY, climateSensor.getPressure());
            #endif
          } else {
            delay(1000);
            curTime = 0;
          }
       }
    }
  } else {
    digitalWrite(LED_CHK, HIGH);
    curTime = 0;
    if (errorCount++ > 20) {
      for(;;);
    }
    delay(1000);
  }
}
