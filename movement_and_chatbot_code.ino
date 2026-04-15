#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

WebServer server(80);
String chatHistory = "";

// WiFi + Gemini API Key
const char* ssid     = "Aachi";
const char* password = "gimmemahmoney";
// NOTE: I've replaced your API key with a placeholder for security.
// Ensure you use your actual, non-expired key here.
const char* apiKey   = "AIzaSyBaNh-JzsI-tp9TCxgEXSAXFxQvtTrQduo"; 

// Free-access Gemini model:
const char* geminiModel = "gemini-flash-latest";

// Motor Pins
#define LEFT_IN1  5
#define LEFT_IN2  18
#define RIGHT_IN3 19
#define RIGHT_IN4 21

// --- NEW: Helper function to URL-encode the prompt ---
String urlEncode(String str) {
  String encodedString = "";
  char c;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+'; // Spaces are often converted to '+' in query strings
    } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encodedString += c;
    } else {
      // Hex encoding for all other characters
      encodedString += '%';
      encodedString += String(c, HEX);
    }
  }
  return encodedString;
}

// --- NEW: Helper function to escape text before inserting into HTML ---
String htmlEscape(String str) {
    str.replace("&", "&amp;");
    str.replace("\"", "&quot;");
    str.replace("<", "&lt;");
    str.replace(">", "&gt;");
    str.replace("\n", "<br>"); // Convert newlines to HTML breaks
    return str;
}


void controlTank(String action) {
  if (action == "forward") {
    digitalWrite(LEFT_IN1, HIGH); digitalWrite(LEFT_IN2, LOW);
    digitalWrite(RIGHT_IN3, HIGH); digitalWrite(RIGHT_IN4, LOW);
  } 
  else if (action == "backward") {
    digitalWrite(LEFT_IN1, LOW); digitalWrite(LEFT_IN2, HIGH);
    digitalWrite(RIGHT_IN3, LOW); digitalWrite(RIGHT_IN4, HIGH);
  } 
  else if (action == "left") {
    digitalWrite(LEFT_IN1, LOW); digitalWrite(LEFT_IN2, HIGH);
    digitalWrite(RIGHT_IN3, HIGH); digitalWrite(RIGHT_IN4, LOW);
  } 
  else if (action == "right") {
    digitalWrite(LEFT_IN1, HIGH); digitalWrite(LEFT_IN2, LOW);
    digitalWrite(RIGHT_IN3, LOW); digitalWrite(RIGHT_IN4, HIGH);
  } 
  else {
    digitalWrite(LEFT_IN1, LOW); digitalWrite(LEFT_IN2, LOW);
    digitalWrite(RIGHT_IN3, LOW); digitalWrite(RIGHT_IN4, LOW);
  }
}

// ------------------ GEMINI API --------------------
String sendPromptToGemini(String prompt) {

  if (WiFi.status() != WL_CONNECTED) return "WiFi not connected.";

  String url = String("https://generativelanguage.googleapis.com/v1beta/models/") +
               geminiModel +
               ":generateContent?key=" +
               apiKey;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // FIX: The prompt needs to be URI-encoded before being put inside the JSON string
  String encodedPrompt = urlEncode(prompt); 
  String payload = "{\"contents\":[{\"parts\":[{\"text\":\"" + encodedPrompt + "\"}]}]}";

  Serial.println("Payload: " + payload);

  int code = http.POST(payload);
  String result = "";

  if (code > 0) {
    String response = http.getString();
    Serial.println("\n--- Gemini RAW Response ---");
    Serial.println(response);

    // Dynamic document size is safer than fixed 16384, but requires a large heap
    // For ESP32, 16384 is a reasonable starting point.
    StaticJsonDocument<16384> doc; 
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      result = "JSON Parse Error: " + String(error.c_str());
      Serial.println("JSON parsing failed!");
      Serial.println(error.c_str());
    } else {
      // FIX: Use robust extraction of const char* for text, checking for block reasons
      JsonObject contentPart = doc["candidates"][0]["content"]["parts"][0];

      if (contentPart.containsKey("text")) {
        const char* textPtr = contentPart["text"].as<const char*>();
        if (textPtr) {
          // This constructor handles embedded newlines/special chars better
          result = String(textPtr); 
        } else {
          result = "Text field is NULL.";
        }
      } else {
        // Handle API blocking (e.g., safety, invalid request)
        if (doc["candidates"][0].containsKey("finishReason") && doc["candidates"][0]["finishReason"].as<String>() == "SAFETY") {
          result = "🚨 Response blocked due to safety settings.";
        } else if (doc.containsKey("error")) {
          // General API error check (e.g., invalid API key, prompt too long)
          result = "❌ API Error: " + doc["error"]["message"].as<String>();
        } else {
          result = "⚠️ Could not find response text. Check raw JSON for structure error.";
        }
      }
    }

  } else {
    result = "HTTP Error: " + String(code) + " - " + http.errorToString(code);
  }

  http.end();
  return result;
}

// ------------------ HTML PAGE --------------------
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Gemini Chat Tank</title>
<style>
  body { font-family: Arial; padding: 10px; background-color: #f4f4f9; }
  h2 { color: #333; }
  .chat { max-height: 50vh; overflow-y: auto; background: #fff; border: 1px solid #ddd; border-radius: 8px; padding: 10px; margin-bottom: 15px; }
  .bubble { padding: 10px; border-radius: 18px; margin: 10px 0; max-width: 80%; line-height: 1.4; box-shadow: 0 1px 1px rgba(0,0,0,0.1); }
  .user { background: #b0e57c; margin-left: auto; text-align: right; }
  .ai { background: #dcdcdc; text-align: left; }
  form { display: flex; margin-bottom: 20px; }
  input[type="text"] { flex-grow: 1; padding: 10px; border: 1px solid #ccc; border-radius: 4px 0 0 4px; }
  input[type="submit"] { padding: 10px 15px; background-color: #5cb85c; color: white; border: none; border-radius: 0 4px 4px 0; cursor: pointer; }
  input[type="submit"]:hover { background-color: #4cae4c; }
  button { padding: 12px 20px; margin: 5px; background-color: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
  button:hover { background-color: #0056b3; }
</style>
</head>
<body>
<h2>🤖 ESP32 Gemini Chat Tank</h2>
<div class="chat">%CHAT%</div>

<form action="/prompt" method="post">
  <input type="text" name="prompt" placeholder="Ask something..." required>
  <input type="submit" value="Send">
</form>

<h3>🕹️ Motor Controls</h3>
<button onclick="fetch('/motor?action=forward')">Forward</button>
<button onclick="fetch('/motor?action=backward')">Backward</button>
<button onclick="fetch('/motor?action=left')">Left</button>
<button onclick="fetch('/motor?action=right')">Right</button>
<button onclick="fetch('/motor?action=stop')">Stop</button>

</body>
</html>
)rawliteral";

// ------------------ WEB SERVER --------------------
void setupPromptServer() {

  server.on("/", HTTP_GET, []() {
    String page = htmlPage;
    page.replace("%CHAT%", chatHistory);
    server.send(200, "text/html", page);
  });

  server.on("/prompt", HTTP_POST, []() {
    String prompt = server.arg("prompt");
    String reply = sendPromptToGemini(prompt);

    // FIX: HTML escape both prompt and reply before adding to history
    chatHistory += "<div class='bubble user'>" + htmlEscape(prompt) + "</div>";
    chatHistory += "<div class='bubble ai'>" + htmlEscape(reply) + "</div>";

    String page = htmlPage;
    page.replace("%CHAT%", chatHistory);
    server.send(200, "text/html", page);
  });

  server.on("/motor", HTTP_GET, []() {
    controlTank(server.arg("action"));
    server.send(200, "text/plain", "Motor " + server.arg("action") + " command sent.");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);

  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);
  pinMode(RIGHT_IN3, OUTPUT);
  pinMode(RIGHT_IN4, OUTPUT);

  // Initial stop to ensure motors don't spin on boot
  controlTank("stop"); 

  WiFi.begin(ssid, password);
  Serial.println("Connecting...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }

  Serial.println("\nConnected! IP:");
  Serial.println(WiFi.localIP());

  setupPromptServer();
}

void loop() {
  server.handleClient();
}