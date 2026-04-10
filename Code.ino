#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// === Pin Definitions ===
#define TURBIDITY_PIN 2
#define PUMP1_PIN 23
#define PUMP2_PIN 22
#define AERATOR_PIN 18
#define BUZZER_PIN 21
#define PUMP1_LED 17
#define PUMP2_LED 16
#define AERATOR_LED 33

// === LCD (I2C) on GPIO26 (SDA) & GPIO25 (SCL) ===
#define LCD_SDA 26
#define LCD_SCL 25
LiquidCrystal_I2C lcd(0x27, 16, 2);      // change 0x27 to 0x3F if needed

// === Display States ===
enum DisplayState {
  SHOW_SENSORS,
  SHOW_STATUS,
  SHOW_AI_RECOMMENDATION,
  SHOW_PUMP_STATUS
};

DisplayState currentDisplayState = SHOW_SENSORS;
unsigned long lastDisplayChange = 0;
const unsigned long DISPLAY_INTERVAL = 5000;  // 5s per screen
unsigned long pumpStatusDisplayTime = 0;
const unsigned long PUMP_STATUS_DURATION = 3000;  // 3s for pump status

// === Status Messages ===
String pumpStatusMessage = "";
String aiRecommendation = "";
String systemStatus = "";

// === WiFi Access Point Credentials ===
const char* ssid = "AquaVise_AP";
const char* password = "aquafarm123";

// === System States ===
bool pump1State = false; // Freshwater Pump
bool pump2State = false; // Drain Pump
bool aeratorState = false; // Aerator

WiFiServer server(80);

// === Simulated sensor values ===
float simTemperature = 25.5; // °C
float simPH = 6.5;
float simTurbidity = 6.5; // NTU

unsigned long lastTempMillis = 0;
unsigned long lastPHMillis = 0;
unsigned long lastTurbMillis = 0;

// update intervals (ms)
const unsigned long TEMP_INTERVAL = 5000;
const unsigned long PH_INTERVAL   = 6500;
const unsigned long TURB_INTERVAL = 8000;

// Utility: clamp
float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Warning function
String getWarning(float temp, float pH, float turbidity) {
  String warning = "";
  if (temp < 24.0 || temp > 28.0)
    warning += "<p style='color:red;'>⚠️ Temperature out of range!</p>";
  if (pH < 6.2 || pH > 6.8)
    warning += "<p style='color:red;'>⚠️ pH level is unsafe!</p>";
  if (turbidity > 9.0)
    warning += "<p style='color:red;'>⚠️ High turbidity detected!</p>";
  if (warning == "")
    warning = "<p style='color:green;'>✅ All readings are within safe range.</p>";
  return warning;
}

// AI Recommendation
String getAIRecommendation(float temp, float pH, float turbidity) {
  String recommendation = "";
  
  if (temp < 24.0) 
    recommendation += "Temp low! Increase heating. ";
  else if (temp > 28.0) 
    recommendation += "Temp high! Increase cooling. ";
  
  if (pH < 6.2) 
    recommendation += "pH low! Add lime. ";
  else if (pH > 6.8) 
    recommendation += "pH high! Add vinegar. ";
  
  if (turbidity > 9.0) 
    recommendation += "Water cloudy! Check filters. ";
  
  if (recommendation == "")
    recommendation = "All parameters optimal. No action needed.";
  
  return recommendation;
}

// System Status
String getSystemStatus(float temp, float pH, float turbidity) {
  String status = "Status: ";
  
  if (temp >= 24.0 && temp <= 28.0 && pH >= 6.2 && pH <= 6.8 && turbidity <= 9.0) {
    status += "NORMAL";
  } else {
    if (temp < 24.0 || temp > 28.0) status += "TEMP_ISSUE ";
    if (pH < 6.2 || pH > 6.8) status += "PH_ISSUE ";
    if (turbidity > 9.0) status += "TURB_ISSUE";
  }
  
  return status;
}

// Random Tip
String getRandomTip() {
  String tips[] = {
    "Feed fish twice a day for optimal growth.",
    "Maintain water pH between 6.2-6.8 for healthy fish.",
    "Regularly clean filters to avoid turbidity rise.",
    "Monitor temperature to prevent stress on fish.",
    "Aerate pond water for better oxygen levels."
  };
  int idx = random(0, 5);
  return tips[idx];
}

// Random Market Price
String getMarketPrice() {
  String prices[] = {
    "Tilapia: ₦800/kg, Catfish: ₦1200/kg",
    "Tilapia: ₦850/kg, Catfish: ₦1250/kg",
    "Tilapia: ₦820/kg, Catfish: ₦1220/kg",
    "Tilapia: ₦870/kg, Catfish: ₦1280/kg",
    "Tilapia: ₦830/kg, Catfish: ₦1230/kg"
  };
  int idx = random(0, 5);
  return prices[idx];
}

// Random Seasonal Forecast
String getSeasonForecast() {
  String forecasts[] = {
    "Rainy season: Expect lower water temp.",
    "Dry season: Monitor water levels carefully.",
    "Hot spell: Increase aeration in ponds.",
    "Cool weather: Fish growth may slow down.",
    "Mixed weather: Adjust feeding frequency."
  };
  int idx = random(0,5);
  return forecasts[idx];
}

// Build JSON for AJAX
String getSensorDataJSON(float temperature, float pH, float turbidity) {
  String warningHTML = getWarning(temperature, pH, turbidity);
  bool isAlert = (temperature < 24.0 || temperature > 28.0) ||
                 (pH < 6.2 || pH > 6.8) ||
                 (turbidity > 9.0);
  String json = "{";
  json += "\"temperature\":" + String(temperature,2);
  json += ",\"pH\":" + String(pH,2);
  json += ",\"turbidity\":" + String(turbidity,2);
  String escapedWarning = warningHTML;
  escapedWarning.replace("\"","'");
  escapedWarning.replace("\n"," ");
  json += ",\"warning\":\""+escapedWarning+"\"";
  json += ",\"alert\":" + String(isAlert?"true":"false");
  json += ",\"tip\":\"" + getRandomTip() + "\"";
  json += ",\"market\":\"" + getMarketPrice() + "\"";
  json += ",\"forecast\":\"" + getSeasonForecast() + "\"";
  json += ",\"recommendation\":\"" + getAIRecommendation(temperature, pH, turbidity) + "\"";
  json += ",\"pump1\":" + String(pump1State?"true":"false");
  json += ",\"pump2\":" + String(pump2State?"true":"false");
  json += ",\"aerator\":" + String(aeratorState?"true":"false");
  json += "}";
  return json;
}

// Update LCD based on current display state
void updateLCD() {
  lcd.clear();
  
  switch(currentDisplayState) {
    case SHOW_SENSORS:
      lcd.setCursor(0, 0);
      lcd.print("T:");
      lcd.print(simTemperature, 1);
      lcd.print((char)223); // degree symbol
      lcd.print("C pH:");
      lcd.print(simPH, 1);

      lcd.setCursor(0, 1);
      lcd.print("Turb:");
      lcd.print(simTurbidity, 1);
      lcd.print(" NTU");
      break;
      
    case SHOW_STATUS:
      lcd.setCursor(0, 0);
      lcd.print("SYSTEM STATUS");
      
      lcd.setCursor(0, 1);
      if (systemStatus.length() > 16) {
        lcd.print(systemStatus.substring(0, 16));
      } else {
        lcd.print(systemStatus);
      }
      break;
      
    case SHOW_AI_RECOMMENDATION:
      lcd.setCursor(0, 0);
      lcd.print("AI RECOMMENDATION");
      
      lcd.setCursor(0, 1);
      if (aiRecommendation.length() > 16) {
        // Split long recommendations into two parts
        int spaceIndex = aiRecommendation.lastIndexOf(' ', 16);
        if (spaceIndex == -1) spaceIndex = 16;
        lcd.print(aiRecommendation.substring(0, spaceIndex));
        
        // Scroll the second part if needed
        static unsigned long scrollTime = 0;
        static int scrollPos = 0;
        if (millis() - scrollTime > 300) {
          scrollTime = millis();
          lcd.setCursor(0, 1);
          String displayText = aiRecommendation.substring(spaceIndex + 1);
          if (displayText.length() > 16) {
            displayText = displayText + " " + displayText;
            lcd.print(displayText.substring(scrollPos, scrollPos + 16));
            scrollPos = (scrollPos + 1) % (displayText.length() - 15);
          } else {
            lcd.print(displayText);
          }
        }
      } else {
        lcd.print(aiRecommendation);
      }
      break;
      
    case SHOW_PUMP_STATUS:
      lcd.setCursor(0, 0);
      lcd.print("PUMP/ACTUATOR STATUS");
      
      lcd.setCursor(0, 1);
      lcd.print(pumpStatusMessage);
      break;
  }
}

// Show pump status for a limited time
void showPumpStatus(String message) {
  pumpStatusMessage = message;
  currentDisplayState = SHOW_PUMP_STATUS;
  pumpStatusDisplayTime = millis();
  updateLCD();
}

// Full HTML page
String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>AquaVise Monitor</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial, sans-serif; text-align:center; background-color:#e9f5ff; margin:0; padding:18px; }
h2 { color: #0d47a1; margin-bottom:6px; }
.sensor-box { margin: 12px auto; padding: 18px; background:#fff; border-radius:12px; max-width:980px; box-shadow:0 6px 18px rgba(13,71,161,0.06);}
.sensor-row { display:flex; justify-content:center; gap:20px; flex-wrap:wrap;}
.sensor-card { width:260px; padding:14px; border-radius:12px; background:linear-gradient(180deg,#fbfeff,#f0fbff); box-shadow:0 2px 6px rgba(0,0,0,0.04); text-align:left; display:flex; gap:10px; align-items:center; transition: transform 0.3s ease, box-shadow 0.3s ease;}
.sensor-card:hover { transform: scale(1.04); box-shadow:0 6px 18px rgba(0,0,0,0.12);}
.icon-wrap { width:56px; height:56px; display:flex; align-items:center; justify-content:center; border-radius:10px; transition: transform 0.3s ease, box-shadow 0.3s ease;}
.sensor-card:hover .icon-wrap { transform: scale(1.2); box-shadow:0 4px 12px rgba(0,0,0,0.2);}
.sensor-body { flex:1; }
.sensor-title { font-size:14px; margin:0 0 6px 0; color:#0d47a1; display:flex; align-items:center; gap:8px; }
.sensor-value { font-size:20px; font-weight:700; transition: all 600ms ease; }
.status { margin-top:6px; font-size:13px; }
.button { padding: 12px 20px; font-size:15px; margin:10px; border:none; border-radius:8px; color:white; cursor:pointer; transition: background-color 0.3s; text-decoration:none; display:inline-block;}
.button.on { background-color:#4CAF50; }
.button.off { background-color:#f44336; }
.bell { position: fixed; right:18px; top:18px; width:48px; height:48px; border-radius:8px; display:flex; align-items:center; justify-content:center; background:#f0f0f0; box-shadow:0 2px 6px rgba(0,0,0,0.08); z-index:999; transition: all 300ms ease; }
.bell.idle { background:#e0e0e0; color:#666; }
.bell.alert { animation: bellflash 0.5s infinite; color:#fff; }
@keyframes bellflash {0%{background:#ffeb3b;}50%{background:#ff7043;}100%{background:#ffeb3b;}}
.info-heading { color: #0d47a1; margin-top:24px; font-size:20px; font-weight:700; position:relative; display:inline-block; padding-bottom:6px;}
.info-heading::after { content:''; position:absolute; left:0; bottom:0; width:100%; height:4px; border-radius:2px; background:linear-gradient(270deg,#ff6b6b,#ffd54f,#4fc3f7); background-size:600% 100%; animation: gradientShift 4s ease infinite;}
@keyframes gradientShift {0%{background-position:0% 0%;}50%{background-position:100% 0%;}100%{background-position:0% 0%;}}
.info-section { margin-top:12px; max-width:980px; margin-left:auto; margin-right:auto; }
.info-card { display:flex; align-items:center; gap:12px; background:#fff; padding:12px 16px; border-radius:12px; box-shadow:0 2px 6px rgba(0,0,0,0.08); transition: transform 0.3s ease, box-shadow 0.3s ease; margin-bottom:12px; }
.info-card .icon-wrap { width:40px; height:40px; border-radius:10px; display:flex; align-items:center; justify-content:center; transition: transform 0.3s ease, box-shadow 0.3s ease; }
.info-card:hover .icon-wrap { transform: scale(1.2); box-shadow:0 4px 12px rgba(0,0,0,0.2); }
</style>
</head>
<body>
<h2>AquaVise Dashboard</h2>
<div id="bell" class="bell idle" title="Alerts">
<svg width="22" height="22" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
<path fill="currentColor" d="M12 2a4 4 0 00-4 4v2.1C6.2 10 5 12.2 5 14v3l-1 1v1h16v-1l-1-1v-3c0-1.8-1.2-4-3-5.9V6a4 4 0 00-4-4zM9 20a3 3 0 006 0H9z"/>
</svg>
</div>

<div class="sensor-box">
<div class="sensor-row" id="sensor-row">
  <!-- Temperature Card -->
  <div class="sensor-card" id="temp-card">
    <div class="icon-wrap" style="background:linear-gradient(135deg,#ffecd2,#ff6b6b);">
      <svg width="36" height="36" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path fill="#fff" d="M14 14.76V5a2 2 0 10-4 0v9.76A4 4 0 1014 14.76z"/></svg>
    </div>
    <div class="sensor-body">
      <div class="sensor-title">Temperature</div>
      <div class="sensor-value" id="temp-value">-- °C</div>
      <div class="status" id="temp-status"></div>
    </div>
  </div>

  <!-- pH Card -->
  <div class="sensor-card" id="ph-card">
    <div class="icon-wrap" style="background:linear-gradient(135deg,#b8f3d4,#00a896);">
      <svg width="36" height="36" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path fill="#fff" d="M8 2h8v2h-8zM7 4h10l-3 8v6a3 3 0 11-4 0v-6L7 4z"/></svg>
    </div>
    <div class="sensor-body">
      <div class="sensor-title">pH Level</div>
      <div class="sensor-value" id="ph-value">--</div>
      <div class="status" id="ph-status"></div>
    </div>
  </div>

  <!-- Turbidity Card -->
  <div class="sensor-card" id="turb-card">
    <div class="icon-wrap" style="background:linear-gradient(135deg,#d7f1ff,#4fc3f7);">
      <svg width="36" height="36" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path fill="#fff" d="M12 2s6 5.5 6 10a6 6 0 11-12 0C6 7.5 12 2 12 2z"/></svg>
    </div>
    <div class="sensor-body">
      <div class="sensor-title">Turbidity</div>
      <div class="sensor-value" id="turb-value">-- NTU</div>
      <div class="status" id="turb-status"></div>
    </div>
  </div>
</div>

<div id="warnings" style="margin-top:12px;"></div>

<!-- Buttons -->
<div style="margin-top:12px;">
  <a href="/toggle1" id="btnPump1" class="button off">Freshwater Pump</a>
  <a href="/toggle2" id="btnPump2" class="button off">Drain Pump</a>
  <a href="/toggle3" id="btnAerator" class="button off">Aerator</a>
</div>

<!-- AI Recommendation Section -->
<div id="recommendation" style="margin-top:12px; font-weight:600; color:#ff6b6b;">
  <div><strong>AI Suggestion:</strong></div>
  <div id="recommendation-text">Loading Recommendations...</div>
</div>

<!-- Tips, Market & Forecast Section -->
<h2 class="info-heading">Tips, Market & Seasonal Forecast</h2>
<div class="info-section">
  <div class="info-card">
    <div class="icon-wrap" style="background:#ffcc80;">
      <svg width="24" height="24" viewBox="0 0 24 24">
        <path fill="#fff" d="M12 2L15 8H9L12 2Z"/>
      </svg>
    </div>
    <div class="info-text" id="tip-text">Loading Tip...</div>
  </div>

  <div class="info-card">
    <div class="icon-wrap" style="background:#90caf9;">
      <svg width="24" height="24" viewBox="0 0 24 24">
        <path fill="#fff" d="M5 4h14v2H5zM5 8h14v2H5zM5 12h14v2H5z"/>
      </svg>
    </div>
    <div class="info-text" id="market-text">Loading Market...</div>
  </div>

  <div class="info-card">
    <div class="icon-wrap" style="background:#a5d6a7;">
      <svg width="24" height="24" viewBox="0 0 24 24">
        <path fill="#fff" d="M6 2h12v4H6zM4 6h16v2H4zM4 10h16v2H4z"/>
      </svg>
    </div>
    <div class="info-text" id="forecast-text">Loading Forecast...</div>
  </div>
</div>

<script>
function setBellState(isAlert) {
  const bell = document.getElementById('bell');
  if(isAlert){ bell.classList.remove('idle'); bell.classList.add('alert'); }
  else { bell.classList.remove('alert'); bell.classList.add('idle'); }
}

function updateButtonsState(pump1, pump2, aerator){
  const btn1 = document.getElementById('btnPump1');
  const btn2 = document.getElementById('btnPump2');
  const btn3 = document.getElementById('btnAerator');
  btn1.className = "button " + (pump1?"on":"off");
  btn2.className = "button " + (pump2?"on":"off");
  btn3.className = "button " + (aerator?"on":"off");

  btn1.onclick = (e)=>{ e.preventDefault(); fetch('/toggle1'); btn1.classList.toggle('on'); btn1.classList.toggle('off'); };
  btn2.onclick = (e)=>{ e.preventDefault(); fetch('/toggle2'); btn2.classList.toggle('on'); btn2.classList.toggle('off'); };
  btn3.onclick = (e)=>{ e.preventDefault(); fetch('/toggle3'); btn3.classList.toggle('on'); btn3.classList.toggle('off'); };
}

function fetchSensors(){
  fetch('/sensorData').then(resp => resp.json()).then(res => {
    document.getElementById('temp-value').innerText = res.temperature.toFixed(2)+" °C";
    document.getElementById('ph-value').innerText = res.pH.toFixed(2);
    document.getElementById('turb-value').innerText = res.turbidity.toFixed(2)+" NTU";
    document.getElementById('warnings').innerHTML = res.warning;
    document.getElementById('recommendation-text').innerText = res.recommendation;
    document.getElementById('tip-text').innerText = res.tip;
    document.getElementById('market-text').innerText = res.market;
    document.getElementById('forecast-text').innerText = res.forecast;
    setBellState(res.alert);
    updateButtonsState(res.pump1,res.pump2,res.aerator);
  });
}

setInterval(fetchSensors,2000); // fetch every 2 seconds
window.onload = fetchSensors;
</script>
</body>
</html>
)rawliteral";

  return html;
}

void setup() {
  Serial.begin(115200);

  // ---- LCD init on GPIO26/25 ----
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(" AquaVise Ready ");
  lcd.setCursor(0,1);
  lcd.print("  Starting AP... ");

  pinMode(PUMP1_PIN, OUTPUT);
  pinMode(PUMP2_PIN, OUTPUT);
  pinMode(AERATOR_PIN, OUTPUT);
  pinMode(PUMP1_LED, OUTPUT);
  pinMode(PUMP2_LED, OUTPUT);
  pinMode(AERATOR_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.softAP(ssid, password);
  Serial.println("AP Started");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  server.begin();

  // Brief LCD status after WiFi up
  delay(800);
  updateLCD();
}

void loop() {
  WiFiClient client = server.available();
  
  // Update sensor values
  unsigned long now = millis();
  if(now - lastTempMillis > TEMP_INTERVAL) { 
    simTemperature += random(-5,6)*0.1; 
    simTemperature = clampf(simTemperature, 22,30); 
    lastTempMillis=now; 
  }
  if(now - lastPHMillis > PH_INTERVAL) { 
    simPH += random(-5,6)*0.01; 
    simPH = clampf(simPH,6.0,7.0); 
    lastPHMillis=now; 
  }
  if(now - lastTurbMillis > TURB_INTERVAL) { 
    simTurbidity += random(-5,6)*0.1; 
    simTurbidity = clampf(simTurbidity,4,12); 
    lastTurbMillis=now; 
  }
  
  // Update AI recommendation and system status
  aiRecommendation = getAIRecommendation(simTemperature, simPH, simTurbidity);
  systemStatus = getSystemStatus(simTemperature, simPH, simTurbidity);
  
  // Handle display rotation
  if (currentDisplayState == SHOW_PUMP_STATUS) {
    // Show pump status for limited time only
    if (now - pumpStatusDisplayTime > PUMP_STATUS_DURATION) {
      currentDisplayState = SHOW_SENSORS;
      lastDisplayChange = now;
    }
  } else if (now - lastDisplayChange > DISPLAY_INTERVAL) {
    // Rotate through display states
    lastDisplayChange = now;
    switch(currentDisplayState) {
      case SHOW_SENSORS:
        currentDisplayState = SHOW_STATUS;
        break;
      case SHOW_STATUS:
        currentDisplayState = SHOW_AI_RECOMMENDATION;
        break;
      case SHOW_AI_RECOMMENDATION:
        currentDisplayState = SHOW_SENSORS;
        break;
      default:
        currentDisplayState = SHOW_SENSORS;
    }
  }
  
  // Update LCD display
  updateLCD();
  
  if (!client) {
    return;
  }

  String request = client.readStringUntil('\r');
  client.flush();

  if(request.indexOf("/toggle1") != -1){
    pump1State = !pump1State;
    digitalWrite(PUMP1_PIN, pump1State ? HIGH : LOW);
    digitalWrite(PUMP1_LED, pump1State ? HIGH : LOW);
    digitalWrite(BUZZER_PIN,HIGH); delay(200); digitalWrite(BUZZER_PIN,LOW);
    
    // Show pump status on LCD
    showPumpStatus(pump1State ? "FreshPump: ON " : "FreshPump: OFF");
  }
  if(request.indexOf("/toggle2") != -1){
    pump2State = !pump2State;
    digitalWrite(PUMP2_PIN, pump2State ? HIGH : LOW);
    digitalWrite(PUMP2_LED, pump2State ? HIGH : LOW);
    digitalWrite(BUZZER_PIN,HIGH); delay(200); digitalWrite(BUZZER_PIN,LOW);
    
    // Show pump status on LCD
    showPumpStatus(pump2State ? "DrainPump: ON " : "DrainPump: OFF");
  }
  if(request.indexOf("/toggle3") != -1){
    aeratorState = !aeratorState;
    digitalWrite(AERATOR_PIN, aeratorState ? HIGH : LOW);
    digitalWrite(AERATOR_LED, aeratorState ? HIGH : LOW);
    digitalWrite(BUZZER_PIN,HIGH); delay(200); digitalWrite(BUZZER_PIN,LOW);
    
    // Show pump status on LCD
    showPumpStatus(aeratorState ? "Aerator: ON " : "Aerator: OFF");
  }

  if(request.indexOf("/sensorData") != -1){
    String json = getSensorDataJSON(simTemperature, simPH, simTurbidity);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println(json);
  } else {
    String html = getHTML();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println(html);
  }
  delay(1);
}