#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal.h>

// --- LCD CONFIG (Parallel Mode) ---
// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(13, 12, 14, 27, 26, 25);

// --- WIFI & LOCATION ---
const char* ssid = "Orion";
const char* password = "KS#mal@5642";
const float HOME_LAT = 42.745806;
const float HOME_LON = -73.742228;

#define PIN        4    // NeoPixel Pin
#define NUMPIXELS 7     
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// --- API CONFIG ---
String serverPath = "https://opensky-network.org/api/states/all?lamin=42.4458&lomin=-74.0422&lamax=43.0458&lomax=-73.4422";
unsigned long lastTime = 0;
unsigned long timerDelay = 30000; 

// --- MATH FUNCTIONS ---
float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  float p = 0.01745329251;
  float a = 0.5 - cos((lat2 - lat1) * p)/2 + cos(lat1 * p) * cos(lat2 * p) * (1 - cos((lon2 - lon1) * p))/2;
  return 12742 * asin(sqrt(a)) * 0.621371;
}

float calculateBearing(float lat1, float lon1, float lat2, float lon2) {
  float p = 0.01745329251;
  float dLon = (lon2 - lon1) * p;
  float y = sin(dLon) * cos(lat2 * p);
  float x = cos(lat1 * p) * sin(lat2 * p) - sin(lat1 * p) * cos(lat2 * p) * cos(dLon);
  float brng = atan2(y, x) / p;
  if (brng < 0) brng += 360;
  return brng;
}

int pickLED(float bearing) {
  if(bearing > 330 || bearing < 30) {return 1;} // North
  if(bearing >= 30 && bearing < 90) {return 2;}
  if(bearing >= 90 && bearing < 150) {return 3;}
  if(bearing >= 150 && bearing < 210) {return 4;}
  if(bearing >= 210 && bearing < 270) {return 5;}
  if(bearing >= 270 && bearing < 330) {return 6;}
  return 0;
}

String getRoute(String callsign) {
  HTTPClient http;
  String routeUrl = "https://opensky-network.org/api/routes?callsign=" + callsign;
  routeUrl.trim(); // Ensure no extra spaces

  http.begin(routeUrl);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);

    String origin = doc["route"][0];
    String dest = doc["route"][1];

    origin = origin.substring(1); // Erase 1 character starting at index 0
    dest = dest.substring(1); // Erase 1 character starting at index 0

    if (origin != "null" && dest != "null") {
      return origin + ">" + dest;
    }
  }
  return "N/A"; // Return this if no route is found
}

void updateDisplay(String callsign, float dist, float alt) {
  lcd.clear();

  // --- Top Line: Flight ID only ---
  lcd.setCursor(0, 0);
  lcd.print(callsign); 

  lcd.setCursor(9, 0);    // Move over to the right side of bottom line
  lcd.print("<");
  lcd.print(dist, 1); // Prints distance (e.g., 12.5)
  lcd.print("km");      // Space after km to clear old digits


  lcd.setCursor(0, 1);
  if (getRoute(callsign) != "N/A")
  {
    lcd.print(getRoute(callsign)); 
  }
  else
  { 
    lcd.print("No route"); 
  }

  lcd.setCursor(9, 1);    // Move over to the right side of bottom line
  lcd.print("^");
  lcd.print((int)alt);
  lcd.print("m  ");       // Extra spaces to clear previous numbers
}

void setup() {
  Serial.begin(115200);
  
  // Initialize LCD
  lcd.begin(16, 2);
  lcd.print("Connecting...");

  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear();
  pixels.show();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  
  lcd.clear();
  lcd.print("WiFi Connected");
  delay(2000);
}

void loop() {
  if ((millis() - lastTime) > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      http.begin(client, serverPath);
      
      int httpResponseCode = http.GET();
      if (httpResponseCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          JsonArray states = doc["states"];
          float minDistance = 999.0;
          float nearestBearing = -1.0;
          float nearestAlt = 0;
          String nearestCallsign = "";

          for (JsonVariant flight : states) {
            float fLat = flight[6];
            float fLon = flight[5];
            if (fLat == 0) continue;

            float d = calculateDistance(HOME_LAT, HOME_LON, fLat, fLon);
            if (d < minDistance) {
              minDistance = d;
              nearestBearing = calculateBearing(HOME_LAT, HOME_LON, fLat, fLon);
              nearestCallsign = flight[1].as<String>();
              nearestCallsign.trim();
              nearestAlt = flight[7].as<float>(); // Altitude in meters
            }
          }

          if (nearestBearing != -1.0) {
            // Update NeoPixel
            pixels.clear();
            pixels.setPixelColor(pickLED(nearestBearing), pixels.Color(0, 150, 150));
            pixels.show();

            // Update LCD
            updateDisplay(nearestCallsign, minDistance, nearestAlt);
          } else {
            lcd.clear();
            lcd.print("Clear skies...");
            pixels.clear();
            pixels.show();
          }
        }
      } else {
        lcd.clear();
        lcd.print("API Error: ");
        lcd.setCursor(0,1);
        lcd.print(httpResponseCode);
      }
      http.end();
    }
    lastTime = millis();
  }
}
