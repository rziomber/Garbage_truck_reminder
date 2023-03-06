#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>  //https://github.com/bblanchon/ArduinoJson
#include <NTPClient.h>    //https://github.com/arduino-libraries/NTPClient
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
#define WiFimodeButton D6

String ssid, pass;
const String accesspassword = "hasuo";
const char* WiFiHostname = "GarbageTruckReminder";

NTPClient timeClient(ntpUDP);
ESP8266WebServer server(80);
DynamicJsonDocument dates(20000);
DynamicJsonDocument pinConfig(100);
char tryToConnect = 0;

void setup() {
  pinConfig["Mixed"] = D1;
  pinConfig["Plastic"] = D2;
  pinConfig["Glass"] = D3;
  pinConfig["Papper"] = D8;
  pinConfig["Bio"] = D7;
  for (const auto& item : pinConfig.as<JsonObject>()) {
    // const char* key = item.key().c_str();
    int pin = item.value().as<int>();
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  analogWriteRange(255);
  Serial.begin(9600);
  //https://www.mischianti.org/2020/06/22/wemos-d1-mini-esp8266-integrated-littlefs-filesystem-part-5/
  LittleFS.begin();
  File settingsFile = LittleFS.open("/settings.txt", "r");
  String settingsString;
  if (settingsFile && !settingsFile.isDirectory()) {
    settingsString = settingsFile.readString();
  }
  settingsFile.close();

  //https://arduinojson.org/v6/doc/upgrade/
  DynamicJsonDocument doc(10024);
  DeserializationError error = deserializeJson(doc, settingsString);
  if (!error) {
    ssid = doc["ssid"].as<String>();
    pass = doc["pass"].as<String>();
  }

  File datesFile = LittleFS.open("/dates.txt", "r");
  if (datesFile && !datesFile.isDirectory()) {
    settingsString = datesFile.readString();
    deserializeJson(dates, settingsString);
  }
  datesFile.close();

  WiFi.softAPdisconnect();
  WiFi.disconnect();

  pinMode(WiFimodeButton, INPUT_PULLUP);
  if (digitalRead(WiFimodeButton) == LOW || (ssid == "" || pass.length() < 8)) {
    //access point part
    Serial.println("Creating Accesspoint");
    IPAddress apIP(192, 168, 1, 1);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("aquarium", "aquarium123");
    Serial.print("IP address:\t");
    Serial.println(WiFi.softAPIP());
    tryToConnect = 3;
  } else {
    connectToAccessPoint();
  }
  Serial.println();

  Serial.println(F("WiFi connected"));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_POST, handleRootPost);
  //server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/json", HTTP_GET, handleJSON);
  server.on("/json", HTTP_POST, receiveJSON);

  const char* headerkeys[] = { "Cookie" };
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  //ask server to track these headers
  server.collectHeaders(headerkeys, headerkeyssize);
  server.begin();
  timeClient.begin();
  timeClient.setUpdateInterval(20UL * 60UL * 1000UL);
}
unsigned long lastTime = 0;
void loop() {
  server.handleClient();
  if (millis() >= lastTime + 500UL) {
    if (WiFi.status() == WL_CONNECTED)
      timeClient.update();
    unsigned long now = timeClient.getEpochTime();
    for (unsigned char i = 0; i < dates.as<JsonArray>().size(); i++) {
      unsigned long tm = dates[i]["values"][0].as<unsigned long>();
      unsigned char pin = pinConfig[dates[i]["type"].as<const char*>()].as<int>();
      //Serial.println(dates[i]["type"].as<String>());
      if (dates[i]["values"].as<JsonArray>().size()) {
        //Serial.println("Now " + String(now) + " tm " + String(tm));
        if (now >= tm - 24UL * 60UL * 60UL) {
          if (now >= tm + 10UL * 60UL * 60UL) {
            //Serial.println("remove");
            dates[i]["values"].remove(0);
            digitalWrite(pin, LOW);
            break;
            /*String jsonData;
            serializeJson(dates, jsonData);
            File datesFile = LittleFS.open("/dates.txt", "w");
            datesFile.print(jsonData);
            datesFile.close();
            */
          } else if (now >= tm) {
            //continuous
            analogWrite(pin, 20. / 100. * 255.);
            //digitalWrite(pin, HIGH);
          } else {
            //flashing
            //digitalWrite(pin, !digitalRead(pin));
            if (millis() % 1000 >= 500) analogWrite(pin, 20. / 100. * 255.);
            else
              digitalWrite(pin, LOW);
          }
        } else digitalWrite(pin, LOW);
      } else digitalWrite(pin, LOW);
    }
    /*
    JsonArray array = dates.as<JsonArray>();
    unsigned char iter = 0;
    for (JsonObject v : array) {
      Serial.println(v["type"].as<String>());
      serializeJson(v, Serial);
      JsonArray array2 = v["values"].as<JsonArray>();
      for (JsonVariant v2 : array2) {
        Serial.println(v2.as<String>());
        if (v2.as<String>() == "2023-02-07") {
          v2.remove(0);
          Serial.println("ok\n\n\n\n\n");
          dates[iter]["values"].remove(0);
        }
      }
      iter++;
      Serial.println();  //V.as<int>());
    }*/
    //Serial.println(timeClient.getEpochTime());

    if (tryToConnect == 0 && millis() > 45UL * 60UL * 1000UL && timeClient.getHours() != 2 && timeClient.getEpochTime() > 1677429692UL) {
      WiFi.softAPdisconnect();
      WiFi.disconnect();
      WiFi.mode(WIFI_OFF);
      tryToConnect = 1;
    } else if (tryToConnect == 1 && timeClient.getHours() == 2) {
      connectToAccessPoint();
      tryToConnect = 0;
    }

    lastTime = millis();
  }
}

void handleRoot() {
  if (!is_authentified()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(301);
    return;
  }

  String content = R"rawliteral(<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta charset="UTF-8">
    <title>Garbage truck reminder</title>
    <style>
      .center {
        text-align: center;
      }

      .main-div div {
        border-style: solid;
        border-width: 1px;
        margin-left: 10px;
        margin-top: 5px;
        margin-bottom: 2em;
        float: left;
      }

      .main-div p {
        font-weight: bold;
        text-align: center;
      }

      .main-div th {
        text-align: left;
      }
    </style>
  </head>
  <body>
    <div class="center">
      <p>
        <a href="/login?DISCONNECT=YES">LOGOUT!</a>
      </p>
      <button id="save" disabled>Save to device</button>
    </div>
    <div class="main-div"></div>
    <div style="dispaly:block; clear:both;"></div>
    <script>
      fetch("/json").then((response) => response.json()).then((data) => {
        console.log(data);
        data.forEach((tableObj) => {
          let tableBody = document.querySelector(`#table-${tableObj.type} tbody`);
          let dates = new Set(tableObj.values);
          printTable(tableBody, dates, true);
          console.log(tableObj);
        });
      }).catch((err) => console.log("Can’t access /json response. Blocked by browser? " + err));
      document.getElementById("save").addEventListener("click", toJSON);
      const maindiv = document.querySelector(".main-div");
      //const garbageTypes = ['Mixed', 'Plastic', 'Glass', 'Papper'];
      const garbageTypes = [{
        type: 'Mixed',
        color: 'green'
      }, {
        type: 'Plastic',
        color: '#B3A828'
      }, {
        type: 'Glass',
        color: 'blue'
      }, {
        type: 'Papper',
        color: 'gray'
      }, {
        type: 'Bio',
        color: 'red'
      }];
      garbageTypes.forEach(value => {
        let div = document.createElement("div");
        div.innerHTML = `
					
					<p style="color:${value.color};">${value.type}</p>
					<form id="form-${value.type}">
						<!--<label for="date-input">Enter a date:</label>-->
						<input type="date" id="date-input-${value.type}">
							<button type="submit">Add to table</button>
						</form>
						<table id="table-${value.type}">
							<thead>
								<tr>
									<th>Date</th>
									<th></th>
								</tr>
							</thead>
							<tbody></tbody>
						</table>`;
        maindiv.append(div);
        let form = document.querySelector(`#form-${value.type}`);
        let dateInput = document.querySelector(`#date-input-${value.type}`);
        let tableBody = document.querySelector(`#table-${value.type} tbody`);
        form.addEventListener('submit', event => {
          event.preventDefault();
          let date = dateInput.value;
          if (date.length < 4) return;
          document.getElementById("save").disabled = false;
          let newDates = new Set(convertTable2Array(tableBody));
          newDates.add(date);
          let tempArr = Array.from(newDates);
          tempArr.sort();
          newDates = new Set(tempArr);
          printTable(tableBody, newDates);
          dateInput.value = '';
        });
      });

      function printTable(tableBody, dates, convert = false) {
        tableBody.innerHTML = "";
        dates.forEach(value => {
          let row = document.createElement('tr');
          let dateCell = document.createElement('td');
          if (convert == false) dateCell.innerText = value;
          else dateCell.innerText = unixTime2string(value);
          row.appendChild(dateCell);
          dateCell = document.createElement('td');
          dateCell.innerHTML = '<button type="button">Remove</button>';
          dateCell.addEventListener("click", function() {
            let newDates = new Set(convertTable2Array(tableBody));
            newDates.delete(this.parentNode.children[0].innerText);
            printTable(tableBody, newDates);
            document.getElementById("save").disabled = false;
          });
          row.appendChild(dateCell);
          tableBody.appendChild(row);
        })
      }

      function toJSON() {
        let allValues = [];
        garbageTypes.forEach(value => {
          let tableBody = document.querySelector(`#table-${value.type} tbody`);
          let t = convertTable2Array(tableBody, true);
          let tableObj = {
            type: value.type,
            values: t
          };
          allValues.push(tableObj);
          //console.log(JSON.stringify(value.type));
          //console.log(JSON.stringify({type:`${value.type}`, dates:`${t}`}));
        });
        console.log(JSON.stringify(allValues));
        fetch('/json', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify(allValues)
        }).then(response => {
          if (response.status == 204) location.reload();
        });
      }

      function string2UnixTime(string) {
        string += "T00:00";
        let garbageTime = new Date(string);
        return Math.floor(garbageTime.getTime() / 1000);
      }

      function unixTime2string(unixTime) {
        let garbageTime = new Date(unixTime * 1000);
        let timeStr = garbageTime.toLocaleDateString("sv");
        return timeStr;
      }

      function convertTable2Array(table, convert = false) {
        let array = [];
        for (let i = 0; i < table.rows.length; i++) {
          //console.log(table.rows[i].cells[0].innerText);
          if (convert == false) array.push(table.rows[i].cells[0].innerText);
          else array.push(string2UnixTime(table.rows[i].cells[0].innerText));
        }
        return array;
      }
    </script>
    <form action='/' method='POST'> Home WiFi Network Name (SSID): <input type='text' name='SSID' value='$ssid'>
      <br> Password: <input type='password' name='wifipass' minlength='8'>
      <br>
      <input type='submit' value='Submit'>
    </form>
  </body>
</html>)rawliteral";

  content.replace("$ssid", ssid);
  server.send(200, "text/html", content);
}

void handleRootPost() {
  if (!is_authentified()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(301);
    return;
  }

  if ((server.hasArg("SSID") && server.arg("SSID") != "")) {
    DynamicJsonDocument doc(10024);
    doc["ssid"] = ssid = server.arg("SSID");
    if (server.hasArg("wifipass") && server.arg("wifipass").length() >= 8)
      doc["pass"] = server.arg("wifipass");
    else
      doc["pass"] = pass;

    File settingsFile = LittleFS.open("/settings.txt", "w");
    String settingsString;
    serializeJson(doc, settingsString);
    settingsFile.print(settingsString);
    settingsFile.close();

    if (server.hasArg("SSID") && server.arg("SSID") != "" && server.hasArg("wifipass") && server.arg("wifipass").length() >= 8) {
      ESP.restart();
    }
  }

  handleRoot();
}

void handleJSON() {
  String content;
  serializeJson(dates, content);
  server.send(200, "application/json", content);
}

void receiveJSON() {
  if (!is_authentified()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(301);
    return;
  }

  String jsonData = server.arg("plain");
  StaticJsonDocument<20000> doc;
  DeserializationError error = deserializeJson(doc, jsonData);

  if (!error) {
    dates.set(doc);
    File datesFile = LittleFS.open("/dates.txt", "w");
    datesFile.print(jsonData);
    datesFile.close();
  }
  server.send(204);  //https://developer.mozilla.org/en-US/docs/Web/HTTP/Status#successful_responses
}

bool is_authentified() {
  Serial.println("Enter is_authentified");
  if (server.hasHeader("Cookie")) {
    Serial.print("Found cookie: ");
    String cookie = server.header("Cookie");
    Serial.println(cookie);
    String cookielogin = "WEATHERSTATION=" + md5(accesspassword);
    if (cookie.indexOf(cookielogin) != -1) {
      Serial.println("Authentification Successful");
      return true;
    }
  }
  Serial.println("Authentification Failed");
  return false;
}

void handleLogin() {
  String msg;
  if (server.hasHeader("Cookie")) {
    Serial.print("Found cookie: ");
    String cookie = server.header("Cookie");
    Serial.println(cookie);
  }
  if (server.hasArg("DISCONNECT")) {
    Serial.println("Disconnection");
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("Set-Cookie", "WEATHERSTATION=0");
    server.send(301);
    return;
  }
  if (server.hasArg("PASSWORD")) {
    if (server.arg("PASSWORD") == accesspassword) {
      server.sendHeader("Location", "/");
      server.sendHeader("Cache-Control", "no-cache");
      String cookielogin = "WEATHERSTATION=" + md5(accesspassword);
      server.sendHeader("Set-Cookie", cookielogin);
      server.send(301);
      Serial.println("Log in Successful");
      return;
    }
    msg = "Wrong username/password! try again.";
    Serial.println("Log in Failed");
  }
  String content = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>" + String(WiFiHostname) + "</title></head>";
  content += "<body><form action='/login' method='POST'>";
  content += "Password:<input type='password' name='PASSWORD' placeholder='password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form>" + msg + "</body></html>";
  server.send(200, "text/html", content);
}

String md5(String str) {
  MD5Builder _md5;
  _md5.begin();
  _md5.add(String(str));
  _md5.calculate();
  return _md5.toString();
}

void connectToAccessPoint() {
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  //Serial.print("connecting to...");
  //Serial.println(ssid + " " + pass);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(WiFiHostname);
  WiFi.begin(ssid.c_str(), pass.c_str());
  /*
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
  */
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
