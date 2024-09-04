/*
Монитор для RADEX MR107+ (VID: abba PID: a104) с регистрацией в zabbix

Для работы с BMP280 используется библиотека BMx280MI
 
Компилировать как MiniCore 3.3v 8MHz
Прошивать:  avrdude -B 125kHz  -p m328p -c usbasp  -U flash:w:./file.hex:i -Uefuse:w:0xFD:m -Uhfuse:w:0xDA:m -Ulfuse:w:0xFF:m
            avrdude -p m328p -c stk500pp -P /dev/ttyUSB0 -U flash:w:./$1:i -Uefuse:w:0xFD:m -Uhfuse:w:0xDA:m -Ulfuse:w:0xFF:m

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

*/

#define MAGICKKEY 0x1234              // Magick key
#define USBHOSTSS 7                   // Need relocate from 10 to 7 on USB host shield.
#define ZABBIXPORT 10051              // Zabbix server Port
#define ZABBIXADDR {109,107,189,186}  // Zabbix server IP, comma separated
#define ZABBIXMAXLEN 128
#define ZABBIXAGHOST "MR"             // Zabbix item's host name
#define ZABBIXSENDPERIOD 600          // Period in secoonds
#define ZABBRADONKEY "rad"
#define ZABBTEMPERATUREKEY "tem"
#define ZABBHUMMIDITYKEY "hum"
#define ZABBPRESSUREKEY "pres"
#define ZABBTEMPBMP280EKEY "Tp"
#define BUFFERSIZE 70
#define wdt_on true
#define LED_CHK 4
#define ETH_RESET 5
#define USB_RESET 10
#define MACADDR { 0x00, 0xAB, 0xBC, 0xCD, 0xDE, 0x10 }
#define SERIAL_OUT

//#include <Wire.h>
#include <EEPROM.h>
#include <cdcacm.h>
#include <usbhub.h>
#define PIN_SPI_SS_ETHERNET_LIB 6
#include <Ethernet.h>

#include <SPI.h>
#if wdt_on
#include <avr/wdt.h>
#endif

#ifdef ZABBTEMPBMP280EKEY
#include <BMx280I2C.h>
#define I2C_ADDRESS 0x76
BMx280I2C bmx280(I2C_ADDRESS);
#endif

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
uint8_t  rec_buffer[BUFFERSIZE + 1], errorCount = 0, errorEthCount = 0;
String str;
unsigned long curTime = 0, tmpTime;
bool bmp280active = false;

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
    errorEthCount = 0;
    client.write(res, len);
  } else {
    //Serial.println("Eth fail");
    digitalWrite(LED_CHK, HIGH);
    curTime = 0;
    if (errorEthCount++ > 3) {
      for(;;);
    }
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
    //if (rcode)
        //ErrorMessage<uint8_t>(PSTR("SetLineCoding"), rcode);
    return rcode;
}

USB     Usb;
ACMAsyncOper  AsyncOper;
ACM           Acm(&Usb, &AsyncOper);
uint8_t snd_buffer[] = {0x7b, 0xff, 0x20, 0x00, 0x06, 0x00, 0x83, 0x04, 0x00, 0x00, 0xda, 0xfb, 0x04, 0x08, 0x0c, 0x00, 0xef, 0xf7};

void setup() {
  #if wdt_on
    MCUSR &= ~(1 << WDRF);
    wdt_disable();
  #endif
  pinMode(LED_CHK, OUTPUT);
  digitalWrite(LED_CHK, HIGH);
  pinMode(ETH_RESET, OUTPUT);
  digitalWrite(ETH_RESET, LOW);
  digitalWrite(ETH_RESET, HIGH);
  pinMode(PIN_SPI_SS_ETHERNET_LIB, OUTPUT);
  digitalWrite(PIN_SPI_SS_ETHERNET_LIB, HIGH);
  
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
  //Serial.print("Z");
  Serial.println(configData.zabbSrv);
  //Serial.print("I");
  Serial.println(configData.keyID);
  for (int i= 0; i < 6; i++) {
    Serial.print(mac[i], HEX);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println();
  Serial.println("c-cfg");
  int waitSerial = 200;
  while (Serial.available() == 0 && waitSerial-- > 0) {
    delay(10);
  }
  /* Запуск конфигуратора */
  if(Serial.available() > 0) {
    if (Serial.read() == 'c') {
      Serial.flush();
      Serial.print("Z:");
      String tmp = "";
      uint8_t tmpChr;
      //byte ipADDR[4] = {0,0,0,0};
      uint8_t addrIDX = 0;
      /* Воод IP адреса сервера zabbix */
      while(1) {
        if(Serial.available() > 0) {
          tmpChr = Serial.read();
          Serial.print((char) tmpChr);
          if (tmpChr == 46) {                 // .
            configData.zabbSrv[addrIDX++] = tmp.toInt();
            tmp = "";
          } else if ((tmpChr == 13) || (tmpChr == 10)) {          // \n
            configData.zabbSrv[addrIDX++] = tmp.toInt();
            Serial.println();
            break;
          } else {
            tmp = tmp + (char) tmpChr;
          }
        }
      }
      Serial.print("I:");
      tmp = "";
      /* ID контроллера */
      while(1) {
        if(Serial.available() > 0) {
          tmpChr = Serial.read();
          Serial.print((char) tmpChr);
          if ((tmpChr == 13) || (tmpChr == 10)) {
            configData.keyID = tmp.toInt();
            Serial.println();
            break;
          } else {
            tmp = tmp + (char) tmpChr;
          }
        }
      }
      //configData.zabbSrv = IPAddress(ipADDR);
      EEPROM.put(0, configData);
    }
  }
  Serial.println("Run");

  pinMode(USBHOSTSS, OUTPUT);
  digitalWrite(USBHOSTSS, HIGH); // Disable SS fo USB host.
  pinMode(USB_RESET, OUTPUT);
  digitalWrite(USB_RESET, LOW);
  digitalWrite(USB_RESET, HIGH);
  #if wdt_on
    wdt_enable(WDTO_8S);
  #endif
  #ifdef ZABBTEMPBMP280EKEY
  if (bmx280.begin()) {
    bmx280.resetToDefaults();
    bmx280.writeOversamplingPressure(BMx280MI::OSRS_P_x16);
    bmx280.writeOversamplingTemperature(BMx280MI::OSRS_T_x16);
    bmp280active = true;
  }
  #endif
  Ethernet.init(PIN_SPI_SS_ETHERNET_LIB);
  #if defined(WIZ550io_WITH_MACADDRESS) // Use assigned MAC address of WIZ550io
    if (Ethernet.begin() == 0) {
  #else
    if (Ethernet.begin(mac) == 0) {
  #endif
      for(;;);
    }
  //Serial.print("IP:");
  Serial.println(Ethernet.localIP());
  if (Usb.Init() == -1) {
    //Serial.println("e2");
      //while ( 1 );
  }
  //digitalWrite(LED_CHK, LOW);
  //delay( 200 );
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
      //if (rcode)
        //ErrorMessage<uint8_t>(PSTR("SndData"), rcode);
      delay(50);
      rcvd = BUFFERSIZE;
      //for (uint16_t i = 0; i < sizeof(rec_buffer); i++)
      //  rec_buffer[i] = 0;
      memset(rec_buffer, 0, sizeof(rec_buffer));
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
            /* Давление и температура с bmp280 */
            #if defined(ZABBPRESSUREKEY) && defined(ZABBTEMPBMP280EKEY)
            if (bmp280active && bmx280.measure()) {
              while (!bmx280.hasValue());
              sendToZabbix(ZABBPRESSUREKEY, bmx280.getPressure());
              //sendToZabbix(ZABBTEMPBMP280EKEY, bmx280.getTemperature());
            }
            #endif
          } else {
            delay(1000);
            curTime = 0;    // Обнулим интервал в случае ошибки, что бы ускорить перезапрос данных.
          }
       }
    }
  } else {
    /*if (bmp280active && bmx280.measure()) {
      while (!bmx280.hasValue());
      Serial.println(bmx280.getPressure());
    }*/
    /* Сюда попадем при неудачной попытке подключить mr107 */
    Serial.println("e3");
    digitalWrite(LED_CHK, HIGH);
    curTime = 0;    // Обнулим интервал в случае ошибки, что бы ускорить перезапрос данных.
    if (errorCount++ > 20) {
      for(;;);
    }
    delay(1000);
  }
}
