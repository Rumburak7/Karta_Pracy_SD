[README_1.md](https://github.com/user-attachments/files/32124197/README_1.md)
# Karta Pracy – ESP32 Arbeitszeiterfassung

Ein eigenständiges ESP32-Programm zur einfachen Arbeitszeiterfassung ("Kommen/Gehen") über ein lokales WLAN. Alle Daten werden auf einer SD-Karte gespeichert und über eine mobil-freundliche Weboberfläche verwaltet – ganz ohne App oder Cloud-Anbindung.

## Funktionen

- **Kommen / Gehen** per Knopfdruck über die Weboberfläche erfassen
- Automatische Zeitstempel per NTP (Zeitzone: Europa/Berlin, `CET-1CEST`)
- Speicherung aller Einträge auf einer **SD-Karte** (bis zu 3000 Einträge)
- Live-Übersicht: erstes Kommen, letztes Gehen, Arbeitszeit heute
- Filter nach Tag / Woche / Monat mit Summenberechnung
- **CSV-Export** und **CSV-Import** (erkennt automatisch Komma oder Semikolon als Trennzeichen)
- Manuelles Nachtragen von Einträgen (z. B. vergessenes Ausstempeln)
- Korrekturfunktionen: letzten Eintrag rückgängig machen, einen ganzen Tag löschen, offene "Kommen"-Einträge ohne passendes "Gehen" automatisch bereinigen
- Druckbarer **Monatsbericht** als HTML-Seite

## Hardware

Getestet mit einem ESP32-Board mit angeschlossenem SD-Kartenmodul (SPI). Die Pinbelegung ist im Code über folgende Konstanten definiert und muss ggf. an die eigene Verkabelung angepasst werden:

| Funktion | Pin |
|---|---|
| SD CS   | 21 |
| SD MOSI | 35 |
| SD MISO | 37 |
| SD SCK  | 36 |

## Installation

1. Projekt in der Arduino IDE öffnen (`Program_Praca_SD.ino`).
2. Benötigte Bibliotheken sind im ESP32-Board-Paket bereits enthalten: `WiFi`, `WebServer`, `SPI`, `SD`, `time.h`. Es muss nichts zusätzlich installiert werden.
3. Im Sketch die WLAN-Zugangsdaten eintragen:

   ```cpp
   const char* WIFI_SSID     = "DEIN_WLAN_NAME";
   const char* WIFI_PASSWORD = "DEIN_WLAN_PASSWORT";
   ```

   ⚠️ **Wichtig für GitHub:** Trage deine echten Zugangsdaten erst *nach* dem Klonen lokal ein und committe sie nicht. Am einfachsten legst du dir eine eigene, nicht versionierte Kopie an oder lagerst SSID/Passwort in eine separate, per `.gitignore` ausgeschlossene Header-Datei aus.

4. Sketch auf das ESP32-Board hochladen.
5. Seriellen Monitor mit **115200 Baud** öffnen – dort wird nach dem Verbinden die IP-Adresse des Geräts angezeigt.
6. Diese IP-Adresse im Browser (im selben WLAN) öffnen, z. B. `http://192.168.1.50/`.

## Nutzung

Die Startseite (`/` bzw. `/praca`) zeigt Uhrzeit, Datum und die Bedienelemente. Über die Buttons „WEJŚCIE“ (Kommen) und „WYJŚCIE“ (Gehen) wird jeweils ein Zeitstempel gespeichert. CSV-Export/-Import sowie der Monatsbericht sind über die entsprechenden Buttons erreichbar.

### Wichtige Endpunkte

| Endpunkt | Beschreibung |
|---|---|
| `GET /` , `/praca` | Weboberfläche |
| `GET /praca/we` | Kommen erfassen |
| `GET /praca/wy` | Gehen erfassen |
| `GET /praca/list` | Alle Einträge als JSON |
| `GET /praca/export/csv` | CSV-Export (Komma-getrennt) |
| `POST /praca/import` | CSV-Import (Komma oder Semikolon) |
| `GET /praca/raport?rok=JJJJ&miesiac=MM` | Druckbarer Monatsbericht |

## Bekannte Einschränkungen

- **Keine Authentifizierung:** Jeder im selben WLAN kann Einträge erfassen oder löschen. Das Gerät ist für den Einsatz in einem vertrauenswürdigen Heim-/lokalen Netzwerk gedacht, nicht für den Betrieb im offenen Internet.
- Ohne erfolgreiche NTP-Synchronisation beim Start werden Einträge zunächst mit dem Platzhalterdatum `2000-01-01` gespeichert; das Gerät versucht anschließend im Hintergrund minütlich erneut zu synchronisieren.
- Bei mehr als 3000 gespeicherten Einträgen wird jeweils der älteste Eintrag verworfen, um Platz für einen neuen zu schaffen.

## Changelog / Review-Hinweise

Beim Review vor der Veröffentlichung wurden zwei Probleme behoben:

- Ein Fehler in der Verwaltung des Einträge-Puffers: Beim Erreichen der Einträge-Grenze wurden zuvor versehentlich fast alle bisherigen Einträge verworfen (Restbestand einer älteren Version mit einem Limit von 200). Jetzt wird korrekt immer nur der jeweils älteste Eintrag entfernt.
- Ein Speicherproblem beim Kompilieren: Das Limit `MAX_ZDARZEN` war auf 3000 gesetzt, in der Annahme, dass die SD-Karte genug Platz bietet. Die Einträge-Tabelle ist jedoch ein globales Array und muss komplett in den internen RAM des ESP32 passen (nicht auf die SD-Karte) – bei 3000 Einträgen à 4 `String`-Feldern führte das zu einem Linker-Fehler (`dram0_0_seg overflowed`). Das Limit wurde auf 1000 Einträge reduziert, was bei zwei Buchungen pro Tag etwa zwei Jahre Verlauf abdeckt und komfortabel in den RAM passt. Bei Bedarf kann der Wert in der Konstante `MAX_ZDARZEN` im Sketch angepasst werden – bei erneutem Speicherüberlauf entsprechend weiter reduzieren.

## Lizenz

Noch keine Lizenz festgelegt – füge z. B. eine `LICENSE`-Datei (MIT, GPL-3.0, …) hinzu, bevor du das Repository veröffentlichst.
