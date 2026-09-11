/*
  Boardverwalter 2.0.17
  KARTA PRACY - samodzielny program ESP32
  ----------------------------------------------------------------
  Wersja z zapisem na karcie SD (zamiast LittleFS), plus dodany
  przycisk IMPORTU na stronie (backend juz istnial, brakowalo UI).
  Limit wpisow (MAX_ZDARZEN) jest ograniczony przez RAM ESP32, nie przez
  miejsce na karcie SD - patrz komentarz przy MAX_ZDARZEN nizej.

  CO ZMIENIC PONIZEJ (linijki oznaczone <<<):
  - WIFI_SSID / WIFI_PASSWORD

  PO WGRANIU: Monitor Portu Szeregowego (115200 baud) pokaze adres IP.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>

const char* WIFI_SSID     = "--------";
const char* WIFI_PASSWORD = "--------";  

// Piny SD
#define SD_CS   5
#define SD_SCK  18
#define SD_MOSI 23
#define SD_MISO 19

WebServer server(80);
bool timeSynced = false; // czy udalo sie pobrac czas NTP (do proby w tle w loop())

// ==================== KARTA PRACY ====================
struct Zdarzenie {
  String data;
  String godzina;
  String typ;
  String uwagi;
};
// UWAGA O PAMIECI RAM: ta tablica jest globalna, wiec MUSI zmiescic sie
// w calosci w RAM ESP32 (typowo ok. 170 KB dostepne na dane statyczne),
// niezaleznie od tego, ile wolnego miejsca ma karta SD. Kazdy wpis to
// 4 pola typu String, co daje ok. 90-100 bajtow/wpis. Limit 3000 (280 KB+)
// nie miesci sie w RAM-ie i powoduje blad linkera "dram0_0_seg overflowed"
// przy kompilacji. 1000 wpisow (~2 lata przy 2 odbiciach dziennie) miesci
// sie z bezpiecznym zapasem. Jesli nadal wyskoczy blad przepelnienia,
// obniz ta wartosc jeszcze bardziej (np. do 500).
const int MAX_ZDARZEN = 1000;
Zdarzenie zdarzenia[MAX_ZDARZEN];
int liczbaZdarzen = 0;

void zapiszDoLittleFS() {
  File plik = SD.open("/historia.txt", FILE_WRITE);
  if (!plik) return;
  for (int i = 0; i < liczbaZdarzen; i++) {
    plik.print(zdarzenia[i].data); plik.print("|");
    plik.print(zdarzenia[i].godzina); plik.print("|");
    plik.print(zdarzenia[i].typ); plik.print("|");
    plik.println(zdarzenia[i].uwagi);
  }
  plik.close();
}

void ladujZLittleFS() {
  liczbaZdarzen = 0;
  File plik = SD.open("/historia.txt", FILE_READ);
  if (!plik) return;
  while (plik.available() && liczbaZdarzen < MAX_ZDARZEN) {
    String line = plik.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int idx1 = line.indexOf('|');
    int idx2 = line.indexOf('|', idx1+1);
    int idx3 = line.indexOf('|', idx2+1);
    if (idx1 == -1 || idx2 == -1 || idx3 == -1) continue;
    zdarzenia[liczbaZdarzen].data = line.substring(0, idx1);
    zdarzenia[liczbaZdarzen].godzina = line.substring(idx1+1, idx2);
    zdarzenia[liczbaZdarzen].typ = line.substring(idx2+1, idx3);
    zdarzenia[liczbaZdarzen].uwagi = line.substring(idx3+1);
    liczbaZdarzen++;
  }
  plik.close();
}
float obliczCzasPracyDlaDnia(String data) {
  float sumaMinut = 0;
  for (int i = 0; i < liczbaZdarzen - 1; i++) {
    if (zdarzenia[i].data == data && zdarzenia[i].typ == "WEJSCIE") {
      for (int j = i + 1; j < liczbaZdarzen; j++) {
        if (zdarzenia[j].data == data && zdarzenia[j].typ == "WYJSCIE") {
          int h1, m1, s1, h2, m2, s2;
          if (sscanf(zdarzenia[i].godzina.c_str(), "%d:%d:%d", &h1, &m1, &s1) >= 2 &&
              sscanf(zdarzenia[j].godzina.c_str(), "%d:%d:%d", &h2, &m2, &s2) >= 2) {
            float roznica = (h2 * 60.0 + m2 + s2/60.0) - (h1 * 60.0 + m1 + s1/60.0);
            if (roznica > 0 && roznica < 720) sumaMinut += roznica;
          }
          break;
        }
      }
    }
  }
  return sumaMinut / 60.0;
}

float obliczCzasPracyDlaOkresu(String odDaty, String doDaty) {
  float suma = 0;
  String lastDate = "";
  for (int i = 0; i < liczbaZdarzen; i++) {
    if (zdarzenia[i].data >= odDaty && zdarzenia[i].data <= doDaty) {
      if (zdarzenia[i].data != lastDate) {
        suma += obliczCzasPracyDlaDnia(zdarzenia[i].data);
        lastDate = zdarzenia[i].data;
      }
    }
  }
  return suma;
}

void dodajZdarzenie(String typ, String uwagi) {
  if (liczbaZdarzen >= MAX_ZDARZEN) {
    // Bufor pelny: usuwamy najstarszy wpis, przesuwajac cala reszte o jedno
    // miejsce w lewo (POPRAWKA: wczesniej zostawialo tylko 199 wpisow z
    // MAX_ZDARZEN=3000, bo limit "199" byl przeniesiony z czasow gdy
    // MAX_ZDARZEN=200 - powodowalo to utrate prawie calej historii).
    for (int i = 0; i < MAX_ZDARZEN - 1; i++) zdarzenia[i] = zdarzenia[i + 1];
    liczbaZdarzen = MAX_ZDARZEN - 1;
  }
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    char bufData[11], bufGodzina[9];
    strftime(bufData, 11, "%Y-%m-%d", &timeinfo);
    strftime(bufGodzina, 9, "%H:%M:%S", &timeinfo);
    zdarzenia[liczbaZdarzen].data = String(bufData);
    zdarzenia[liczbaZdarzen].godzina = String(bufGodzina);
  } else {
    zdarzenia[liczbaZdarzen].data = "2000-01-01";
    zdarzenia[liczbaZdarzen].godzina = "00:00:00";
  }
  zdarzenia[liczbaZdarzen].typ = typ;
  zdarzenia[liczbaZdarzen].uwagi = uwagi;
  liczbaZdarzen++;
  zapiszDoLittleFS();
}

// ==================== KARTA PRACY HANDLERY ====================
void handlePracaWe() { dodajZdarzenie("WEJSCIE", "Adam"); server.send(200, "text/plain", "OK"); }
void handlePracaWy() { dodajZdarzenie("WYJSCIE", "Adam"); server.send(200, "text/plain", "OK"); }

void handlePracaList() {
  String json = "{\"wpisy\":[";
  for (int i = 0; i < liczbaZdarzen; i++) {
    if (i > 0) json += ",";
    json += "{\"data\":\"" + zdarzenia[i].data + "\",\"godzina\":\"" + zdarzenia[i].godzina + "\",\"typ\":\"" + zdarzenia[i].typ + "\"}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handlePracaExport() {
  String csv = "Data;Godzina;Typ;Uwagi\n";
  for (int i = 0; i < liczbaZdarzen; i++) {
    csv += zdarzenia[i].data + ";" + zdarzenia[i].godzina + ";" + zdarzenia[i].typ + ";" + zdarzenia[i].uwagi + "\n";
  }
  server.send(200, "text/csv", csv);
}

void handlePracaImport() {
  if (server.hasArg("data")) {
    String csvData = server.arg("data");
    // NAPRAWA: eksport CSV uzywal przecinkow, import oczekiwal tylko
    // srednikow - pliki z przycisku "EKSPORTUJ CSV" nie dalyby sie
    // poprawnie zaimportowac. Teraz separator jest rozpoznawany automatycznie.
    char sep = (csvData.indexOf(';') != -1) ? ';' : ',';
    int lines = 0;
    int startIdx = 0;
    int endIdx = csvData.indexOf('\n');
    if (endIdx > 0) {
      startIdx = endIdx + 1;
      endIdx = csvData.indexOf('\n', startIdx);
    }
    while (endIdx > 0 && lines < MAX_ZDARZEN) {
      String line = csvData.substring(startIdx, endIdx);
      line.trim();
      startIdx = endIdx + 1;
      endIdx = csvData.indexOf('\n', startIdx);
      if (line.length() > 0) {
        int sem1 = line.indexOf(sep);
        int sem2 = line.indexOf(sep, sem1 + 1);
        int sem3 = line.indexOf(sep, sem2 + 1);
        if (sem1 > 0 && sem2 > 0) {
          String data = line.substring(0, sem1);
          String godzina = line.substring(sem1 + 1, sem2);
          String typ = line.substring(sem2 + 1, sem3);
          String uwagi = (sem3 > 0) ? line.substring(sem3 + 1) : "";
          if (godzina.length() == 5) godzina = godzina + ":00";
          if (liczbaZdarzen < MAX_ZDARZEN && (typ == "WEJSCIE" || typ == "WYJSCIE")) {
            zdarzenia[liczbaZdarzen].data = data;
            zdarzenia[liczbaZdarzen].godzina = godzina;
            zdarzenia[liczbaZdarzen].typ = typ;
            zdarzenia[liczbaZdarzen].uwagi = uwagi;
            liczbaZdarzen++;
            lines++;
          }
        }
      }
    }
    zapiszDoLittleFS();
    server.send(200, "text/plain", "OK Importowano " + String(lines) + " wpisów");
  } else {
    server.send(400, "text/plain", "Brak danych");
  }
}

void handlePracaUsunOstatni() {
  if (liczbaZdarzen > 0) { liczbaZdarzen--; zapiszDoLittleFS(); }
  server.send(200, "text/plain", "OK");
}

void handlePracaUsunDzien() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char bufData[11];
  strftime(bufData, 11, "%Y-%m-%d", &timeinfo);
  String dzisiaj = String(bufData);
  int nowaIlosc = 0;
  for (int i = 0; i < liczbaZdarzen; i++) {
    if (zdarzenia[i].data != dzisiaj) zdarzenia[nowaIlosc++] = zdarzenia[i];
  }
  liczbaZdarzen = nowaIlosc;
  zapiszDoLittleFS();
  server.send(200, "text/plain", "OK");
}

void handlePracaNaprawPare() {
  for (int i = liczbaZdarzen - 1; i >= 0; i--) {
    if (zdarzenia[i].typ == "WEJSCIE") {
      bool maWyjscie = false;
      for (int j = i + 1; j < liczbaZdarzen; j++) {
        if (zdarzenia[j].typ == "WYJSCIE") { maWyjscie = true; break; }
      }
      if (!maWyjscie) {
        for (int j = i; j < liczbaZdarzen - 1; j++) zdarzenia[j] = zdarzenia[j + 1];
        liczbaZdarzen--;
        zapiszDoLittleFS();
        break;
      }
    }
  }
  server.send(200, "text/plain", "OK");
}

void handlePracaDodajRecznie() {
  if (server.hasArg("typ") && server.hasArg("data") && server.hasArg("godzina")) {
    String typ = server.arg("typ"), data = server.arg("data"), godzina = server.arg("godzina");
    if (liczbaZdarzen >= MAX_ZDARZEN) {
      // Ta sama poprawka co w dodajZdarzenie() - patrz komentarz tam.
      for (int i = 0; i < MAX_ZDARZEN - 1; i++) zdarzenia[i] = zdarzenia[i + 1];
      liczbaZdarzen = MAX_ZDARZEN - 1;
    }
    zdarzenia[liczbaZdarzen].data = data;
    zdarzenia[liczbaZdarzen].godzina = godzina + ":00";
    zdarzenia[liczbaZdarzen].typ = typ;
    zdarzenia[liczbaZdarzen].uwagi = "RECZNE";
    liczbaZdarzen++;
    zapiszDoLittleFS();
  }
  server.send(200, "text/plain", "OK");
}

void handlePraca() {
  String html = R"rawliteral(   
<!DOCTYPE html>
<html lang="pl">
 <head>
 <meta charset="UTF-8">
 <link rel="icon" type="image/png" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Ctext x='0' y='20' font-size='20' fill='white'%3E💪%3C/text%3E%3C/svg%3E">
 <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
 <meta name="apple-mobile-web-app-capable" content="yes">
 <title>💪 Karta Pracy</title>
 <style>
  .container { text-align: center; }
  h1 { text-align: center; }
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: linear-gradient(135deg, #0a0f1c 0%, #0f1629 100%); min-height: 100vh; padding: 16px; color: #eef; }
    .container { max-width: 700px; margin: 0 auto; }
    .back-btn { background: #2196F3; color: white; padding: 10px 20px; border-radius: 30px; text-decoration: none; display: inline-block; margin-bottom: 20px; }
    .card { background: rgba(18, 25, 45, 0.9); backdrop-filter: blur(10px); border-radius: 32px; padding: 20px; margin-bottom: 20px; box-shadow: 0 8px 20px rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.08); }
    .header { text-align: center; margin-bottom: 20px; }
    .header h1 { font-size: 1.8rem; background: linear-gradient(135deg, #a5f0ff, #5c9eff); -webkit-background-clip: text; background-clip: text; color: transparent; }
    .clock { font-size: 3rem; font-weight: 600; text-align: center; font-family: monospace; margin: 10px 0; letter-spacing: 2px; }
    .date { text-align: center; font-size: 1rem; color: #9aa9c1; margin-bottom: 15px; }
    .podsumowanie { background: #0a0f1c; border-radius: 24px; padding: 12px 15px; margin-bottom: 20px; display: flex; justify-content: space-between; flex-wrap: wrap; gap: 10px; }
    .podsumowanie-item { text-align: center; flex: 1; }
    .podsumowanie-item .label { font-size: 0.7rem; color: #7f8ea3; }
    .podsumowanie-item .value { font-size: 1.2rem; font-weight: bold; color: #5c9eff; }
    .button-group { display: flex; gap: 12px; margin: 20px 0; flex-wrap: wrap; }
    .btn { flex: 1; padding: 14px; border: none; border-radius: 60px; font-weight: 600; font-size: 1rem; cursor: pointer; transition: transform 0.1s ease; color: white; }
    .btn:active { transform: scale(0.96); }
    .btn-start { background: #0f6e3f; }
    .btn-pause { background: #b45f1b; }
    .btn-csv { background: #5c9eff; margin-top: 10px; width: 100%; }
    .manage-buttons { display: flex; gap: 10px; justify-content: center; margin: 15px 0; flex-wrap: wrap; }
    .small-btn { background: #1e2a44; padding: 8px 16px; border-radius: 30px; font-size: 0.8rem; cursor: pointer; text-align: center; border: none; color: white; }
    .filter-buttons { display: flex; gap: 10px; margin-bottom: 15px; justify-content: center; flex-wrap: wrap; }
    .filter-btn { background: #1e2a44; border: none; padding: 6px 20px; border-radius: 30px; color: white; cursor: pointer; font-size: 0.9rem; }
    .filter-btn.active { background: #5c9eff; }
    .suma-pracy { background: #0f6e3f; border-radius: 24px; padding: 15px; margin-bottom: 20px; text-align: center; }
    .suma-pracy .label { font-size: 0.8rem; color: #ccc; }
    .suma-pracy .value { font-size: 1.8rem; font-weight: bold; color: white; }
    .manual-panel { background: #0a0f1c; border-radius: 24px; padding: 15px; margin-top: 15px; }
    .manual-panel h4 { margin-bottom: 10px; color: #5c9eff; }
    .manual-inputs { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
    .manual-inputs input, .manual-inputs select { background: #1e2a44; border: none; padding: 8px 12px; border-radius: 20px; color: white; font-size: 0.9rem; flex: 1; min-width: 100px; }
    .btn-manual { background: #5c9eff; border: none; padding: 8px 16px; border-radius: 30px; color: white; cursor: pointer; font-weight: bold; }
    .log { max-height: 400px; overflow-y: auto; }
    .log-item { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #1e2a44; gap: 8px; flex-wrap: wrap; }
    .log-info { flex: 3; font-size: 0.85rem; }
    .log-badge { flex: 1; text-align: center; }
    .badge { padding: 4px 12px; border-radius: 40px; font-size: 0.7rem; font-weight: bold; }
    .badge-start { background: #0f6e3f; }
    .badge-pause { background: #b45f1b; }
    .footer { text-align: center; font-size: 0.7rem; color: #5c6a81; margin-top: 20px; }
    @media (max-width: 600px) { .clock { font-size: 2rem; } .log-item { flex-direction: column; align-items: flex-start; } }
  </style>
</head>
<body>
<div class="container">

<h1>💪 KARTA PRACY 💪</h1>
  <div class="card">
    <div class="clock" id="liveClock">--:--:--</div>
    <div class="date" id="liveDate"></div>

    <div class="podsumowanie" id="podsumowanie">
      <div class="podsumowanie-item"><div class="label">WEJŚCIE</div><div class="value" id="pierwszeWejscie">-</div></div>
      <div class="podsumowanie-item"><div class="label">WYJŚCIE</div><div class="value" id="ostatnieWyjscie">-</div></div>
      <div class="podsumowanie-item"><div class="label">DZIŚ PRACOWAŁEŚ</div><div class="value" id="czasPracy">0.0 h</div></div>
    </div>
    
    <div class="suma-pracy" id="sumaOkresu">
      <div class="label">SUMA ZA OKRES</div>
      <div class="value" id="sumaCzasu">0.0 h</div>
    </div>
    
    <div class="button-group">
      <button class="btn btn-start" onclick="sendCmd('we')">✅ WEJŚCIE</button>
      <button class="btn btn-pause" onclick="sendCmd('wy')">✅ WYJŚCIE</button>
    </div>
    <button class="btn btn-csv" onclick="pobierzCSV()">📥 EKSPORTUJ CSV</button>
    <button class="btn btn-csv" style="background:#009688" onclick="document.getElementById('importFile').click()">📤 IMPORTUJ CSV</button>
    <input type="file" id="importFile" accept=".csv,text/csv" style="display:none" onchange="importujPlik(this.files[0])">
    <button class="btn btn-csv" onclick="pokazRaport()" style="background:#9C27B0;margin-top:10px">📅 RAPORT MIESIĘCZNY</button>
    
    <div class="manage-buttons">
      <div class="small-btn" onclick="usunOstatni()">🗑️ Cofnij ostatnie</div>
      <div class="small-btn" onclick="usunDzien()">📅 Wyczyść dzisiejsze</div>
      <div class="small-btn" onclick="naprawPare()">🔧 Napraw parę</div>
      <div class="small-btn" onclick="usunWybranyDzienGUI()">🗑️ Usuń wybrany dzień</div>
      <input type="date" id="deleteDate" style="background:#1e2a44; border:none; padding:8px; border-radius:30px; color:white;">
    </div>
    
    <div class="filter-buttons">
      <button class="filter-btn" id="btnDzis" onclick="setFilter('dzis')">📅 DZIŚ</button>
      <button class="filter-btn" id="btnTydzien" onclick="setFilter('tydzien')">📆 TYDZIEŃ</button>
      <button class="filter-btn" id="btnMiesiac" onclick="setFilter('miesiac')">📅 MIESIĄC</button>
      <button class="filter-btn active" id="btnWszystkie" onclick="setFilter('wszystkie')">📋 WSZYSTKIE</button>
    </div>
    
    <div class="manual-panel">
      <h4>✏️ Ręczne dodanie odbicia</h4>
      <div class="manual-inputs">
        <input type="date" id="recDate">
        <input type="time" id="recTime">
        <select id="recType">
          <option value="WEJSCIE">✅ WEJŚCIE</option>
          <option value="WYJSCIE">⏸️ WYJŚCIE</option>
        </select>
        <button class="btn-manual" onclick="dodajRecznie()">➕ DODAJ</button>
      </div>
    </div>
  </div>
  <div class="card">
    <h3 style="margin-bottom:12px">📋 HISTORIA</h3>
    <div class="log" id="logList">Ładowanie...</div>
  </div>
  <div class="footer">ESP32 | Karta Pracy</div>
</div>

<script>
  let wszystkieWpisy = [];
  let aktualnyFilter = 'wszystkie';
  let now = new Date();
  document.getElementById('recDate').value = now.toISOString().slice(0,10);
  document.getElementById('recTime').value = now.toLocaleTimeString('pl-PL', {hour:'2-digit', minute:'2-digit'});
  document.getElementById('deleteDate').value = now.toISOString().slice(0,10);
  
  async function sendCmd(cmd) {
    let url = cmd === 'we' ? '/praca/we' : '/praca/wy';
    try {
      let res = await fetch(url);
      if (res.ok) location.reload();
      else alert("Błąd");
    } catch(e) { alert("Brak połączenia z ESP32"); }
  }
  
  async function pobierzCSV() { window.open('/praca/export/csv', '_blank'); }

  function importujPlik(file) {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async function(e) {
      const resp = await fetch('/praca/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'data=' + encodeURIComponent(e.target.result)
      });
      const text = await resp.text();
      alert(text);
      document.getElementById('importFile').value = '';
      location.reload();
    };
    reader.readAsText(file);
  }

  function pokazRaport() {
  let now = new Date();
  let rok = now.getFullYear();
  let miesiac = now.getMonth() + 1;
  let url = `/praca/raport?rok=${rok}&miesiac=${miesiac}`;
  window.open(url, '_blank');
}
  
  async function usunOstatni() { if(confirm("Usunąć ostatni wpis?")) { await fetch('/praca/usun_ostatni'); location.reload(); } }
  async function usunDzien() { if(confirm("Usunąć wszystkie dzisiejsze wpisy?")) { await fetch('/praca/usun_dzien'); location.reload(); } }
  async function naprawPare() { if(confirm("Usunąć wiszące WEJŚCIE bez pary?")) { await fetch('/praca/napraw_pare'); location.reload(); } }
  async function usunWybranyDzienGUI() { let data = document.getElementById('deleteDate').value; if(!data) { alert("Wybierz datę"); return; } if(confirm(`Usunąć wszystkie wpisy z ${data}?`)) { await fetch(`/praca/usun_wybrany_dzien?data=${data}`); location.reload(); } }
  
  async function dodajRecznie() {
    let data = document.getElementById('recDate').value;
    let godzina = document.getElementById('recTime').value;
    let typ = document.getElementById('recType').value;
    if (!data || !godzina) { alert("Wybierz datę i godzinę"); return; }
    if(confirm(`Dodać ${typ} na ${data} o ${godzina}?`)) {
      let url = `/praca/dodaj_recznie?typ=${typ}&data=${data}&godzina=${godzina}`;
      try { let res = await fetch(url); if (res.ok) { alert("Dodano!"); location.reload(); } else alert("Błąd dodawania"); } catch(e) { alert("Błąd połączenia"); }
    }
  }
  
  function setFilter(filter) {
    aktualnyFilter = filter;
    document.querySelectorAll('.filter-btn').forEach(btn => btn.classList.remove('active'));
    if(filter === 'dzis') document.getElementById('btnDzis').classList.add('active');
    else if(filter === 'tydzien') document.getElementById('btnTydzien').classList.add('active');
    else if(filter === 'miesiac') document.getElementById('btnMiesiac').classList.add('active');
    else document.getElementById('btnWszystkie').classList.add('active');
    wyswietlListe();
    pobierzSumeOkresu();
  }
  
  function wyswietlListe() {
    let now = new Date();
    let dzisiaj = now.toISOString().slice(0,10);
    let tydzienTemu = new Date(now);
    tydzienTemu.setDate(now.getDate() - 7);
    let tydzienTemuStr = tydzienTemu.toISOString().slice(0,10);
    let miesiacTemu = new Date(now);
    miesiacTemu.setDate(now.getDate() - 30);
    let miesiacTemuStr = miesiacTemu.toISOString().slice(0,10);
    
    let filtered = [];
    if(aktualnyFilter === 'dzis') filtered = wszystkieWpisy.filter(w => w.data === dzisiaj);
    else if(aktualnyFilter === 'tydzien') filtered = wszystkieWpisy.filter(w => w.data >= tydzienTemuStr);
    else if(aktualnyFilter === 'miesiac') filtered = wszystkieWpisy.filter(w => w.data >= miesiacTemuStr);
    else filtered = wszystkieWpisy;
    
    let html = '';
    filtered.forEach(w => {
      let klasa = w.typ === 'WEJSCIE' ? 'badge-start' : 'badge-pause';
      let godzinaKrotka = w.godzina.substring(0,5);
      html += `<div class="log-item">
        <div class="log-info">${w.data} ${godzinaKrotka}</div>
        <div class="log-badge"><span class="badge ${klasa}">${w.typ}</span></div>
        <div class="log-actions"><span>${w.uwagi || ''}</span></div>
      </div>`;
    });
    document.getElementById('logList').innerHTML = html || '<div class="log-item">Brak wpisów</div>';
  }
  
  async function pobierzSumeOkresu() {
    let url = '';
    if(aktualnyFilter === 'tydzien') url = '/praca/podsumowanie_tygodnia';
    else if(aktualnyFilter === 'miesiac') url = '/praca/podsumowanie_miesiaca';
    else { document.getElementById('sumaCzasu').innerHTML = '0.0 h'; return; }
    
    try {
      let res = await fetch(url);
      let data = await res.json();
      let g = Math.floor(data.czas_pracy);
      let m = Math.round((data.czas_pracy - g) * 60);
      if (m === 60) { g++; m = 0; }
 document.getElementById('sumaCzasu').innerHTML = g + ':' + (m < 10 ? '0' + m : m);
      document.getElementById('sumaOkresu').style.display = 'flex';
    } catch(e) { console.log("Blad"); }
  }
  
  function updateClock() {
    let now = new Date();
    document.getElementById('liveClock').innerText = now.toLocaleTimeString('pl-PL');
    document.getElementById('liveDate').innerText = now.toLocaleDateString('pl-PL', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' });
  }
  setInterval(updateClock, 1000);
  updateClock();
  
  async function pobierzPodsumowanie() {
    try {
      let res = await fetch('/praca/podsumowanie');
      let data = await res.json();
      document.getElementById('pierwszeWejscie').innerText = data.pierwsze_wejscie;
      document.getElementById('ostatnieWyjscie').innerText = data.ostatnie_wyjscie;
      let g = Math.floor(data.czas_pracy);
      let m = Math.round((data.czas_pracy - g) * 60);
      if (m === 60) { g++; m = 0; }
 document.getElementById('czasPracy').innerText = g + ':' + (m < 10 ? '0' + m : m);
    } catch(e) { console.log("Blad podsumowania"); }
  }
  
  async function pobierzDane() {
    try {
      let res = await fetch('/praca/list');
      let data = await res.json();
      wszystkieWpisy = data.wpisy;
      wyswietlListe();
    } catch(e) { console.log("Blad"); }
  }
  
  setInterval(pobierzDane, 5000);
  setInterval(pobierzPodsumowanie, 5000);
  pobierzDane();
  pobierzPodsumowanie();
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handlePracaPodsumowanie() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char bufData[11];
  strftime(bufData, 11, "%Y-%m-%d", &timeinfo);
  String dzisiaj = String(bufData);
  
  String pierwszeWejscie = "-";
  String ostatnieWyjscie = "-";
  for (int i = 0; i < liczbaZdarzen; i++) {
    if (zdarzenia[i].data == dzisiaj && zdarzenia[i].typ == "WEJSCIE") {
      if (pierwszeWejscie == "-") pierwszeWejscie = zdarzenia[i].godzina.substring(0,5);
    }
    if (zdarzenia[i].data == dzisiaj && zdarzenia[i].typ == "WYJSCIE") {
      ostatnieWyjscie = zdarzenia[i].godzina.substring(0,5);
    }
  }
  
  String json = "{\"pierwsze_wejscie\":\"" + pierwszeWejscie + "\",\"ostatnie_wyjscie\":\"" + ostatnieWyjscie + "\",\"czas_pracy\":" + String(obliczCzasPracyDlaDnia(dzisiaj)) + "}";
  server.send(200, "application/json", json);
}

void handlePracaPodsumowanieTygodnia() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char bufData[11];
  strftime(bufData, 11, "%Y-%m-%d", &timeinfo);
  String dzisiaj = String(bufData);
  
  timeinfo.tm_mday -= 7;
  mktime(&timeinfo);
  char bufTyg[11];
  strftime(bufTyg, 11, "%Y-%m-%d", &timeinfo);
  String tydzienTemu = String(bufTyg);
  
  String json = "{\"czas_pracy\":" + String(obliczCzasPracyDlaOkresu(tydzienTemu, dzisiaj)) + ",\"od_daty\":\"" + tydzienTemu + "\",\"do_daty\":\"" + dzisiaj + "\"}";
  server.send(200, "application/json", json);
}

void handlePracaPodsumowanieMiesiaca() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char bufData[11];
  strftime(bufData, 11, "%Y-%m-%d", &timeinfo);
  String dzisiaj = String(bufData);
  
  timeinfo.tm_mday -= 30;
  mktime(&timeinfo);
  char bufMies[11];
  strftime(bufMies, 11, "%Y-%m-%d", &timeinfo);
  String miesiacTemu = String(bufMies);
  
  String json = "{\"czas_pracy\":" + String(obliczCzasPracyDlaOkresu(miesiacTemu, dzisiaj)) + ",\"od_daty\":\"" + miesiacTemu + "\",\"do_daty\":\"" + dzisiaj + "\"}";
  server.send(200, "application/json", json);
}

void handlePracaUsunWybranyDzien() {
  if (server.hasArg("data")) {
    String data = server.arg("data");
    int nowaIlosc = 0;
    for (int i = 0; i < liczbaZdarzen; i++) {
      if (zdarzenia[i].data != data) {
        zdarzenia[nowaIlosc++] = zdarzenia[i];
      }
    }
    liczbaZdarzen = nowaIlosc;
    zapiszDoLittleFS();
    server.send(200, "text/plain", "USUNIETO");
  } else {
    server.send(400, "text/plain", "BRAK");
  }
}

void handlePracaExportCSV() {
  String csv = "Data,Godzina,Typ,Uwagi\n";
  for (int i = 0; i < liczbaZdarzen; i++) {
    csv += zdarzenia[i].data + "," + zdarzenia[i].godzina + "," + zdarzenia[i].typ + "," + zdarzenia[i].uwagi + "\n";
  }
  server.send(200, "text/csv", csv);
}
void handlePracaRaportMiesieczny() {
  if (!server.hasArg("rok") || !server.hasArg("miesiac")) {
    server.send(400, "text/plain", "Brak parametrów: rok i miesiac");
    return;
  }
  
  int rok = server.arg("rok").toInt();
  int miesiac = server.arg("miesiac").toInt();
  
  // Formatuj miesiąc z zerem (01-12)
  String miesiacStr = (miesiac < 10) ? "0" + String(miesiac) : String(miesiac);
  String pierwszyDzien = String(rok) + "-" + miesiacStr + "-01";
  
  // Ostatni dzień miesiąca
  int dniWMiesiacu;
  if (miesiac == 2) {
    dniWMiesiacu = ((rok % 4 == 0 && rok % 100 != 0) || rok % 400 == 0) ? 29 : 28;
  } else if (miesiac == 4 || miesiac == 6 || miesiac == 9 || miesiac == 11) {
    dniWMiesiacu = 30;
  } else {
    dniWMiesiacu = 31;
  }
  String ostatniDzien = String(rok) + "-" + miesiacStr + "-" + String(dniWMiesiacu);
  
  // Zbierz dni z danego miesiąca
  String dni[32];
  float godziny[32];
  for (int d = 1; d <= dniWMiesiacu; d++) {
    String data = String(rok) + "-" + miesiacStr + "-" + (d < 10 ? "0" + String(d) : String(d));
    godziny[d] = obliczCzasPracyDlaDnia(data);
    dni[d] = data;
  }
  
  // Generuj HTML do druku
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><link rel='icon' type='image/png' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Crect width=\"100\" height=\"100\" rx=\"20\" fill=\"%231a1a2e\"/%3E%3Ccircle cx=\"50\" cy=\"45\" r=\"25\" fill=\"%236c63ff\"/%3E%3Ctext x=\"50\" y=\"60\" text-anchor=\"middle\" fill=\"white\" font-size=\"35\"%3E🩺%3C/text%3E%3C/svg%3E'>";
  html += "<title>Raport miesięczny - " + String(rok) + "-" + miesiacStr + "</title>";
  html += "<style>";
  html += "body{font-family:Arial;padding:20px}";
  html += "h1{text-align:center;color:#333}";
  html += "table{width:100%;border-collapse:collapse;margin-top:20px}";
  html += "th,td{border:1px solid #ccc;padding:10px;text-align:center}";
  html += "th{background:#4CAF50;color:white}";
  html += "tr:nth-child(even){background:#f9f9f9}";
  html += ".suma{background:#ff9800;color:white;font-weight:bold}";
  html += "@media print{body{margin:0;padding:0}button{display:none}}";
  html += "</style>";
  html += "<body>";
  html += "<button onclick='window.print()' style='padding:10px 20px;margin-bottom:20px;cursor:pointer'>🖨️ DRUKUJ</button>";
  html += "<h1>📊 RAPORT MIESIĘCZNY</h1>";
  html += "<h2>" + String(rok) + "-" + miesiacStr + "</h2>";
  html += "<table>";
  html += "<tr><th>Dzień</th><th>Data</th><th>Przepracowane godziny</th></tr>";
  
  float sumaMiesiac = 0;
  for (int d = 1; d <= dniWMiesiacu; d++) {
    String data = String(rok) + "-" + miesiacStr + "-" + (d < 10 ? "0" + String(d) : String(d));
    float h = godziny[d];
    sumaMiesiac += h;
    html += "<tr>";
    html += "<td>" + String(d) + "</td>";
    html += "<td>" + data + "</td>";
    html += "<td>" + String(h, 2) + " h</td>";
    html += "</tr>";
  }
  
  html += "<tr class='suma'><td colspan='2'><strong>SUMA MIESIĄCA</strong></td><td><strong>" + String(sumaMiesiac, 2) + " h</strong></td></tr>";
  html += "</table>";
  html += "<p style='margin-top:20px;text-align:center;color:#666'>Wygenerowano: " + String(__DATE__) + " " + String(__TIME__) + "</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== KARTA PRACY - START ===");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ Błąd SD! Sprawdź piny/okablowanie/kartę.");
  } else {
    Serial.println("✅ SD OK!");
  }
  ladujZLittleFS();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Laczenie z WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("WiFi OK! IP: ");
  Serial.println(WiFi.localIP());

  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  Serial.print("Oczekiwanie na czas NTP");
  int timeout = 0;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo, 1000) && timeout < 30) { Serial.print("."); timeout++; }
  Serial.println();
  timeSynced = (timeout < 30);
  Serial.println(timeSynced ? "Czas pobrany!" : "Brak czasu NTP - bede probowal dalej w tle co minute");

  server.on("/", handlePraca);
  server.on("/praca", handlePraca);
  server.on("/praca/we", handlePracaWe);
  server.on("/praca/wy", handlePracaWy);
  server.on("/praca/list", handlePracaList);
  server.on("/praca/export", handlePracaExport);
  server.on("/praca/import", HTTP_POST, handlePracaImport);
  server.on("/praca/usun_ostatni", handlePracaUsunOstatni);
  server.on("/praca/usun_dzien", handlePracaUsunDzien);
  server.on("/praca/napraw_pare", handlePracaNaprawPare);
  server.on("/praca/dodaj_recznie", handlePracaDodajRecznie);
  server.on("/praca/podsumowanie", handlePracaPodsumowanie);
  server.on("/praca/podsumowanie_tygodnia", handlePracaPodsumowanieTygodnia);
  server.on("/praca/podsumowanie_miesiaca", handlePracaPodsumowanieMiesiaca);
  server.on("/praca/usun_wybrany_dzien", handlePracaUsunWybranyDzien);
  server.on("/praca/export/csv", handlePracaExportCSV);
  server.on("/praca/raport", handlePracaRaportMiesieczny);
  server.begin();
  Serial.println("Serwer start!");
  Serial.print("http://"); Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();

  static unsigned long lastRetry = 0;
  if (!timeSynced && millis() - lastRetry > 60000) {
    lastRetry = millis();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 500)) {
      timeSynced = true;
      Serial.println("✅ Czas NTP zsynchronizowany (proba w tle)");
    } else {
      Serial.println("⏳ Nadal brak czasu NTP - kolejna proba za minute");
    }
  }
}
