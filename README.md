# UDMPRIG-Client

**UDMPRIG-Client** ist ein ESP32-basierter WLAN-Client für den UDMPRIG-Server.  
Er verbindet serielle Geräte (z.B. TNC, Funkgerät, PC) kabellos mit dem Server, ermöglicht bidirektionalen AX.25/RS232-Datenaustausch, bietet ein OLED-Statusdisplay, ein Webinterface zur Konfiguration und unterstützt OTA-Firmware-Updates.

---

## Features

- **WLAN-Client-Modus** mit automatischer Verbindung zu konfigurierbarem Server (IP/Port, Standard 2323)
- **Serielle Kommunikation** (UART1, RX=16, TX=17) mit wählbarer Baudrate
- **OLED-Display 128x64** (SSD1306 I2C) zeigt Status, Callsign, Netzwerk und RS232-Aktivität
- **Webinterface** für Setup, Monitoring und Konfiguration
- **OTA-Update**: Firmware kann einfach online aktualisiert werden (z.B. von GitHub)
- **EEPROM-Speicher** für WLAN, Server-IP, Callsign, Port, Baudrate, Loglevel
- **Konfigurations-AP** (Access Point) bei fehlender WLAN-Verbindung
- **Monitor**: Echtzeit Log/Debug- und RS232-Überwachung im Webinterface
- **LED-Statusanzeige** auf GPIO2

---

## Hardware-Aufbau

### **Benötigte Komponenten**

- **ESP32-Devboard** (z.B. DOIT ESP32 DEVKIT V1)
- **OLED-Display 128x64** (SSD1306, I2C, Adresse 0x3C)
- **Serielles Kabel** zum Gerät (UART1, RX=16, TX=17)
- **LED (optional)** für Statusanzeige (GPIO2)
- **Stromversorgung** (5V via Micro-USB)

### **Schaltplan**

```
ESP32 Pinout (Hauptanschlüsse):

OLED:
 - SCL (Display)   → GPIO 22 (ESP32 SCL)
 - SDA (Display)   → GPIO 21 (ESP32 SDA)
 - VCC             → 3.3V oder 5V (je nach Display)
 - GND             → GND

RS232 (z.B. TNC, Gerät):
 - RX (ESP32 16)   → TX Gerät (ggf. Pegelanpassung)
 - TX (ESP32 17)   → RX Gerät (ggf. Pegelanpassung)
 - GND             → GND

LED (optional):
 - LED+            → GPIO 2 (Vorwiderstand nicht vergessen)
 - LED-            → GND

! ACHTUNG:
  - OLED muss mit 3.3V kompatibel sein, sonst Pegelwandler verwenden!
  - RS232 ggf. über Pegelkonverter anpassen!
```

### **Beispielhafte Verdrahtung**

| Funktion    | ESP32 Pin | Bauteil          |
|-------------|-----------|------------------|
| OLED SCL    | GPIO 22   | OLED SCL         |
| OLED SDA    | GPIO 21   | OLED SDA         |
| OLED VCC    | 3.3V/5V   | OLED VCC         |
| OLED GND    | GND       | OLED GND         |
| Gerät RX    | GPIO 17   | RS232 TX         |
| Gerät TX    | GPIO 16   | RS232 RX         |
| LED         | GPIO 2    | LED (optional)   |

---

## Inbetriebnahme

1. **Firmware flashen** (z.B. via Arduino IDE oder PlatformIO)
2. **Erster Start:**  
   Gerät startet als WLAN-AccessPoint `UDMPRIG-Client`  
   → Mit Laptop/Handy verbinden und Webinterface öffnen: http://192.168.4.1/
3. **WLAN, Server-IP, Callsign, Port, Baudrate usw. eingeben und speichern**
4. Gerät startet neu, verbindet sich mit WLAN und Server
5. **Webinterface aufrufen:**  
   - Per lokaler IP-Adresse  
   - Oder per mDNS: [http://udmprig-client.local/](http://udmprig-client.local/)
6. **Verbinde das serielle Gerät** mit den definierten Pins (s.o.)

---

## Webinterface

- **Config:**  
  WLAN, Server-IP/Port, Callsign, Baudrate, Loglevel
- **Monitor:**  
  Log- und RS232-Ausgaben live, Filter nach Loglevel, Monitor leeren möglich

---

## OLED-Anzeige

- **Zeigt das eigene Callsign**
- **WLAN-Signalstärke**
- **Verbindungsstatus zum Server**
- **RS232-Aktivität RX/TX (Balkenanzeige)**
- **Während OTA-Update: Fortschrittsbalken**

---

## OTA-Update

- Beim Start prüft das Gerät automatisch auf eine neue Firmware (z.B. auf GitHub, siehe Sketch)
- Firmware und Version werden als `firmware.bin` und `version.txt` bereitgestellt
- Update läuft vollautomatisch mit Fortschrittsanzeige auf OLED

---

## Hinweise

- **Server-Verbindung:** Callsign muss auf dem Server freigeschaltet (Whitelist) sein
- **Timeout/Keepalive:** Verbindungsabbrüche werden automatisch erkannt und neu aufgebaut
- **EEPROM:** Alle Einstellungen persistent
- **Webinterface aufrufbar über mDNS und lokale IP**

---

## Bibliotheken

Folgende Bibliotheken werden benötigt (Arduino Library Manager):

- [WiFi](https://github.com/espressif/arduino-esp32)
- [Wire](https://www.arduino.cc/en/Reference/Wire)
- [Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [WebServer](https://github.com/espressif/arduino-esp32)
- [EEPROM](https://github.com/espressif/arduino-esp32)
- [ESPmDNS](https://github.com/espressif/arduino-esp32)
- [HTTPClient](https://github.com/espressif/arduino-esp32)
- [Update](https://github.com/espressif/arduino-esp32)
- [time.h] (Standard)

---

## Beispielserver

Es wird ein UDMPRIG-Server benötigt (siehe [UDMPRIG-Server-Projekt](../UDM-PRIG-Server)).

---

## Lizenz

(c) 2025 pukepals.com, 73 de AT1NAD  
Dieses Projekt ist Open Source, Lizenz siehe Repository.

---
