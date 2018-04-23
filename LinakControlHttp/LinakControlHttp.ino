#include <NewPing.h>
#include <WiFi.h>
#include "Passwords.h"

// Pins
#define ECHO_PIN 22
#define TRIGGER_PIN 23
#define LED_PIN 2
#define UP_RELAY_PIN 16
#define DOWN_RELAY_PIN 17

// Sensor settings
#define SERIAL_RATE 115200     // Serial rate (baud)
#define MIN_DISTANCE 2         // Min distance of HC-SR04 (cm)
#define MAX_DISTANCE 400       // Max distance of HC-SR04 (cm)
#define MIN_DESK_HEIGHT 65     // Min height (inclusive) of Linak desk (cm)
#define MAX_DESK_HEIGHT 130    // Max height (inclusive) of Linak desk (cm)
#define HEIGHT_POLL_RATE 60   // Poll rate when desk is moving (ms)
#define ROOM_HEIGHT 264
#define AVG_SAMPLES 20

WiFiServer server(80);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

unsigned long last_req;

/* Return 0 on error */
double get_desk_height_inner(int samples)
{
  noInterrupts();
  cli();
  double ping_time = sonar.ping_median(samples);
  sei();
  interrupts();  
  double cm = ping_time / US_ROUNDTRIP_CM;

  // HC-SR04 datasheet says these are impossible, so error.
  if (cm < MIN_DISTANCE || cm > MAX_DISTANCE) {
    return 0;
  }

  return ROOM_HEIGHT - cm;
}

void maybe_delay() {
  unsigned long next_allowed = last_req + HEIGHT_POLL_RATE;
  unsigned long current_time = millis();
  if (current_time < next_allowed) {
    delay(next_allowed - current_time);
  }
  last_req = millis();
}

double get_desk_height(unsigned int samples)
{
  double height = 0;
  unsigned int limit = 10;

  while (!(height > (MIN_DESK_HEIGHT - 10) && height < (MAX_DESK_HEIGHT + 10)) && limit-- > 0) {
    maybe_delay();
    height = get_desk_height_inner(samples);
    if (!(height > (MIN_DESK_HEIGHT - 10) && height < (MAX_DESK_HEIGHT + 10)) && limit-- > 0) {
      Serial.println(height);
    }
  }

  return height;
}

void stop_moving()
{
  digitalWrite(UP_RELAY_PIN, HIGH);
  digitalWrite(DOWN_RELAY_PIN, HIGH);
}

void set_desk_height(unsigned int target_height)
{
  double current_height = get_desk_height(1);
  //  Serial.println(current_height);

  if (current_height == 0 || current_height == target_height) {
    return;
  }

  bool go_up = current_height < target_height;
  bool go_down = !go_up;

  digitalWrite(go_up ? UP_RELAY_PIN : DOWN_RELAY_PIN, LOW);

  while (true) {
    if (go_up && current_height >= target_height) {
      // We reached target height going up.
      break;
    }

    if (go_down && current_height <= target_height) {
      // We reached target height going down.
      break;
    }

    if (current_height <= MIN_DESK_HEIGHT || current_height >= MAX_DESK_HEIGHT) {
      // Bail out, the desk is out of bounds!
      break;
    }

    int new_height = get_desk_height(1);

    if (new_height == 0) {
      // Bail out, error.
      break;
    }

    if (abs(new_height - current_height) >= 5) {
      // Bail out, the desk has moved unreasonably far for some reason...
      break;
    }

    current_height = new_height;
  }

  stop_moving();
}

String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

bool containsHeight(String line) {
  if (line.startsWith("GET /") && line.endsWith(" ") && (line.charAt(line.length() - 2) >= '0') && (line.charAt(line.length() - 2) <= '9')) {
    for (int i = 0; i < line.length(); i++) {
      if (line.charAt(i) == '/' && i < (line.length() - 1)) {
        if (line.charAt(i + 1) >= '0' && line.charAt(i + 1) <= '9') {
          return true;
        }
        return false;
      }
    }
  }
  return false;
}

void setup_wifi() {
  digitalWrite(LED_PIN, HIGH);
  WiFi.disconnect(true);
  
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_PIN, LOW);
}

void setup()
{
  Serial.begin(SERIAL_RATE);
  pinMode(LED_PIN, OUTPUT);      // set the LED pin mode

  pinMode(UP_RELAY_PIN, OUTPUT);
  pinMode(DOWN_RELAY_PIN, OUTPUT);
  stop_moving();

  last_req = millis();

  // We start by connecting to a WiFi network
  setup_wifi();

  server.begin(); 
}

void loop() {
  if (WiFi.status() != WL_CONNECTED){
    setup_wifi();
  }
  
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // the content of the HTTP response follows the header:
            client.println(String(get_desk_height(AVG_SAMPLES)));

            // The HTTP response ends with another blank line:
            client.println();
            // break out of the while loop:
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        if (containsHeight(currentLine)) {
          unsigned int desk_height = (unsigned int) getValue(currentLine, '/', 1).toInt();
          if (desk_height >= MIN_DESK_HEIGHT && desk_height <= MAX_DESK_HEIGHT) {
            set_desk_height(desk_height);
          }
        }
      }
    }
    // close the connection:
    client.stop();
  }
}
