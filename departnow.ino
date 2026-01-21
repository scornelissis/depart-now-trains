#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "M5Cardputer.h"
#include "time.h"

//fill in wifi requirements
const char* ssid     = "";
const char* password = "";

//get your api key here https://openrouteservice.org/
const char* ROUTE_API_KEY = "";
const char* BASE_ROUTE_API_URL = "https://api.openrouteservice.org/v2/directions/";


//fill in latitude and longitude of your location
const float OWN_LAT = ;
const float OWN_LON = ;

String BASE_MSG;

volatile bool routeRequested = false;
String pendingRouteMsg;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

void handleRoot() {
  static const char PAGE[] PROGMEM = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
    <meta charset="utf-8">
    <title>Choose departure, destination and travel method to station</title>
    <script src="/main.js" type="module"></script>
    </head>
    <body>
    <h1>Travel Reminder</h1>
    <form id="trip-form">
  <fieldset>
    <legend>Trip details</legend>

    <label for="departure-text">Departure station</label><br>
    <input
      type="text"
      id="departure-text"
      name="departure"
      placeholder="Brugge"
      autocomplete="off"
    >

    <select id="departure-select" name="departure-select">
      <option value="">Select departure station</option>
    </select>

    <br><br>

    <label for="arrival-text">Arrival station</label><br>
    <input
      type="text"
      id="arrival-text"
      name="arrival"
      placeholder="Brussel-Zuid"
      autocomplete="off"
    >

    <select id="arrival-select" name="arrival-select">
      <option value="">Select arrival station</option>
    </select>

    <br><br>

    <label for="date-selector"> Select Date and Time when you want to depart</label><br>
    <input type="datetime-local" id="date-selector" name="date-selector">
    <br><br>

    <button type="submit">Search</button>
    <p id="error"></p>
  </fieldset>
</form>

    </body>
    </html>
  )rawliteral";
  server.send_P(200, "text/html", PAGE);
}

void handleJs() {
static const char JS[] PROGMEM = R"rawliteral(
  const BASE_API_URL = "https://api.irail.be";

  let ws;
  let wsReady = false;

  function connectWebSocket() {
    ws = new WebSocket("ws://" + location.hostname + ":81");

    ws.onopen = () => {
      console.log("WebSocket connected");
      wsReady = true;
    };

    ws.onclose = () => {
      console.warn("WebSocket closed, reconnecting...");
      wsReady = false;
      setTimeout(connectWebSocket, 1000);
    };

    ws.onerror = (err) => {
      console.error("WebSocket error", err);
      ws.close();
    };

    ws.onmessage = (event) => {
      console.log("Received from Cardputer:", event.data);
    };
  }

  function init() {
    connectWebSocket();
    function setMinDateTime() {
      const input = document.querySelector("#date-selector");

      const now = new Date();
      now.setMinutes(now.getMinutes() - now.getTimezoneOffset());

      const minValue = now.toISOString().slice(0, 16);
      input.setAttribute("min", minValue);
    }

    setMinDateTime();
    document.querySelector("form").addEventListener("submit", handleSearch);
  }

  async function handleSearch(e) {
    e.preventDefault();

    const departureText = document.querySelector("#departure-text").value;
    const arrivalText = document.querySelector("#arrival-text").value;

    const station = departureText;
    const dateTimeWishToLeave = extractDateTime();

    const response = await fetch(
      `${BASE_API_URL}/liveboard/?station=${station}&date=${dateTimeWishToLeave.date}&time=${dateTimeWishToLeave.time}&format=json&lang=nl`
    );

    const data = await response.json();
    const stationLatitude = data.stationinfo.locationY;
    const stationLongitude = data.stationinfo.locationX;
    const departures = data.departures.departure;

    const available = checkIfStationIsAvailable(departures, arrivalText);
    let timeOfDepart;
    let platform;
    let destination;

    if (available){
      const departure = getMatchingDeparture(departures, arrivalText);
      timeOfDepart = departure.time;
      platform = departure.platform;
      destination = departure.stationinfo.name;

    } else {
      document.querySelector("error").innerText = "No trains towards preferred destination around that time";
      return;
    }
    let payload;

    if (ws.readyState === WebSocket.OPEN) {
      payload = getPayload(stationLatitude, stationLongitude, timeOfDepart, platform, destination);
      console.log("Sending payload:", payload);
      ws.send(JSON.stringify(payload));
    }
  }

  function extractDateTime() {
    const input = document.querySelector("#date-selector").value;
    if (!input) return null;

    const [datePart, timePart] = input.split("T");

    const time = timePart.replace(":", "");

    const [year, month, day] = datePart.split("-");
    const date = `${day}${month}${year.slice(-2)}`; 

    return { date, time };
  }

  function getPayload(stationLat, stationLong, timeOfDepart, platform, destination){
    const payload = {
      type: "route",
      to: {
        lat: parseFloat(stationLat),
        lon: parseFloat(stationLong)
      },
      time: timeOfDepart,
      destination: destination,
      platform: parseInt(platform)
    }
    return payload;
  }

  function checkIfStationIsAvailable(departureStations, arrivalStation) {
    return departureStations.some(
      d => d.stationinfo?.name === arrivalStation
    );
  }

  function getMatchingDeparture(departureStations, arrivalStation) {
    return departureStations.find(
      d => d.stationinfo?.name === arrivalStation
    );
  }

  init();
)rawliteral";
server.send_P(200, "application/javascript", JS);
}

String getRouteProfile(const String& mode) {
  if (mode == "car") return "driving-car";
  if (mode == "bike") return "cycling-regular";
  if (mode == "electric-bike") return "cycling-electric";
  if (mode == "walk") return "foot-walking";
  return "driving-car";
}

bool hasBeenCalledWalk = false;
bool hasBeenCalledCar = false;
bool hasBeenCalledEbike = false;
bool hasBeenCalledBike = false;


bool hasBeenCalledForMode(const String& mode) {
  if (mode == "car") return hasBeenCalledCar;
  if (mode == "walk") return hasBeenCalledWalk;
  if (mode == "bike") return hasBeenCalledBike;
  if (mode == "electric-bike") return hasBeenCalledEbike;
  return false;
}

void setHasBeenCalledForMode(const String& mode) {
  if (mode == "car") hasBeenCalledCar = true;
  else if (mode == "walk") hasBeenCalledWalk = true;
  else if (mode == "bike") hasBeenCalledBike = true;
  else if (mode == "electric-bike") hasBeenCalledEbike = true;
}

int durationCar = 0;
int durationBike = 0;
int durationEbike = 0;
int durationWalk = 0;

void setDurationMode(const String& mode, int duration){
  if (mode == "car") durationCar = duration;
  if (mode == "bike") durationBike = duration;
  if (mode == "electric-bike") durationEbike = duration;
  if (mode == "walk") durationWalk = duration;
}

int getDurationMode(const String& mode){
  if (mode == "car") return durationCar;
  if (mode == "bike") return durationBike;
  if (mode == "electric-bike") return durationEbike;
  if (mode == "walk") return durationWalk;
  return 0;
}

void handleRouteMessage(const String& msg, const String& mode) {

  StaticJsonDocument<1024> docMsg;
  if (deserializeJson(docMsg, msg)) return;

  float toLat = docMsg["to"]["lat"].as<float>();
  float toLon = docMsg["to"]["lon"].as<float>();
  unsigned long timeOfDepartureTrain = docMsg["time"];
  time_t nowEpoch = time(nullptr);
  int platform = docMsg["platform"];
  String destination = docMsg["destination"].as<String>();

  long diffSeconds = timeOfDepartureTrain - nowEpoch;

  

  String url = String(BASE_ROUTE_API_URL) + getRouteProfile(mode); 

  WiFiClientSecure client;
  client.setInsecure(); //You could try to set this secure and see what happens but I haven't tried this behaviour yet
  HTTPClient https;

  if (https.begin(client, url) && !hasBeenCalledForMode(mode)) {
    setHasBeenCalledForMode(mode);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", ROUTE_API_KEY);

    StaticJsonDocument<512> reqDoc;
    JsonArray coords = reqDoc.createNestedArray("coordinates");
    JsonArray start = coords.createNestedArray();
    start.add(OWN_LON); start.add(OWN_LAT);
    JsonArray end = coords.createNestedArray();
    end.add(toLon); end.add(toLat);

    reqDoc["geometry"] = false; 
    reqDoc["instructions"] = false;

    String body;
    serializeJson(reqDoc, body);
    int httpCode = https.POST(body);

    if (httpCode == 200) {
      String response = https.getString();
      DynamicJsonDocument docRoute(8192);
      DeserializationError error = deserializeJson(docRoute, response);

      if (error) {
        M5.Display.clear();
        M5.Display.setCursor(0,0);
        M5.Display.println("JSON Error:");
        M5.Display.println(error.c_str());
      } else {

        float duration = docRoute["routes"][0]["summary"]["duration"]; 

        int durationSeconds = (int)(duration + 180.0); //3 minutes buffer time  
        setDurationMode(mode, durationSeconds);
      }
    } else {
      M5.Display.clear();
      M5.Display.printf("HTTP Error: %d\n", httpCode);
    }
    https.end();
  }
  int duration = getDurationMode(mode);
  long departNow = diffSeconds - duration;

  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.setTextSize(2);

  M5.Display.println("STATION INFO");
  M5.Display.println("--------------");
  M5.Display.printf("Destination: %s\n", destination.c_str());
  M5.Display.printf("Platform: %d\n", platform);

  if (diffSeconds < 0) {
    M5.Display.println("Train already departed");
  } 
  else if (departNow < 0) {
    long lateSeconds = -departNow;
    int lateMinutes = lateSeconds / 60;
    int lateHours   = lateMinutes / 60;
    lateMinutes     = lateMinutes % 60;

    M5.Display.print("You are late by: \n");
    if (lateHours > 0) {
      M5.Display.printf("%dh %02dmin\n", lateHours, lateMinutes);
    } else {
      M5.Display.printf("%dmin\n", lateMinutes);
    }
  } 
  else {
    int waitMinutes = departNow / 60;
    int waitHours   = waitMinutes / 60;
    waitMinutes     = waitMinutes % 60;

    M5.Display.print("Depart in: ");
    if (waitHours > 0) {
      M5.Display.printf("%dh %02dmin\n", waitHours, waitMinutes);
      
    } else {
      M5.Display.printf("%dmin\n", waitMinutes);
      if (waitMinutes == 5){
          M5.Speaker.setVolume(230);
          M5.Speaker.tone(9000, 100);
          M5.Speaker.tone(10000, 100);
          M5.Speaker.tone(11000, 100);
          M5.Speaker.tone(12000, 100);
          M5.Speaker.tone(13000, 100);
      } 
      if (waitMinutes == 3){
        M5.Speaker.setVolume(230);
        M5.Speaker.tone(11000, 100);
        M5.Speaker.tone(11000, 100);
        M5.Speaker.tone(11000, 100);

      }
      if (waitMinutes == 0){
        M5.Speaker.setVolume(230);
        M5.Speaker.tone(11000, 200);
      }
    }
  }
  M5.Display.println("Mode: " + mode);

}

void onWsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if(type != WStype_TEXT) return;

  payload[length] = '\0';             
  pendingRouteMsg = String((char*)payload);
  routeRequested = true;   
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);


  M5.Display.clear();
  M5.Display.setTextSize(2);
  M5.Display.setCursor(0, 0);
  M5.Display.println("Connecting WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Display.print(".");
  }



  M5.Display.println("\nConnected!");
  M5.Display.println(WiFi.localIP());

  // Set up NTP (UTC) with optional timezone offset
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov"); 
  // 3600 = GMT+1 (Belgium standard time offset in seconds)
  // 0 = daylight savings not applied automatically here - set to 3600 to adjust for daylight savings in Belgium
  // NTP servers: pool.ntp.org, time.nist.gov

  webSocket.begin();
  webSocket.onEvent(onWsEvent);

  server.on("/", handleRoot);
  server.on("/main.js", handleJs);
  server.onNotFound(handleNotFound);

  server.begin();
  
  M5.Display.println("Server started");
  M5.Speaker.setVolume(230);
  M5.Speaker.tone(10000, 100);
}


const char* transportArray[4] = {"car", "bike", "electric-bike", "walk"};
int transportIndex = 0; 
const char* transportMethod = "car";
time_t last_run;
void loop() {
  server.handleClient();
  webSocket.loop();

  if (routeRequested) {
    routeRequested = false;
    BASE_MSG = pendingRouteMsg;
    hasBeenCalledWalk = false;
    hasBeenCalledCar = false;
    hasBeenCalledEbike = false;
    hasBeenCalledBike = false;
  }

  if (M5Cardputer.Keyboard.isChange()){
    if(M5Cardputer.Keyboard.isKeyPressed('c')){
      transportMethod = "car";
      handleRouteMessage(BASE_MSG, transportMethod);
    }

    if(M5Cardputer.Keyboard.isKeyPressed('e')){
      transportMethod = "electric-bike";
      handleRouteMessage(BASE_MSG, transportMethod);
    }

    if(M5Cardputer.Keyboard.isKeyPressed('b')){
      transportMethod = "bike";
      handleRouteMessage(BASE_MSG, transportMethod);
    }

    if(M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('f')){
      transportMethod = "walk";
      handleRouteMessage(BASE_MSG, transportMethod);
    }

    if(M5Cardputer.Keyboard.isKeyPressed(',')){
      transportIndex = (transportIndex - 1 + 4) % 4;
      handleRouteMessage(BASE_MSG, transportArray[transportIndex]);
    }

    if(M5Cardputer.Keyboard.isKeyPressed('/')){
      transportIndex = (transportIndex + 1) % 4;
      handleRouteMessage(BASE_MSG, transportArray[transportIndex]);
    }
  }

  time_t now = time(nullptr);

  if (difftime(now, last_run) >= 60){
      handleRouteMessage(BASE_MSG, transportMethod);
      last_run = now;

  }


    M5Cardputer.update();

  
}
