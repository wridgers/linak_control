#include <NewPing.h>

// Pins
#define UP_RELAY_PIN 3
#define DOWN_RELAY_PIN 4
#define ECHO_PIN 8
#define TRIGGER_PIN 9

// Configuration
#define SERIAL_RATE 115200     // Serial rate (baud)
#define MIN_DISTANCE 2         // Min distance of HC-SR04 (cm)
#define MAX_DISTANCE 400       // Max distance of HC-SR04 (cm)
#define MIN_DESK_HEIGHT 65     // Min height (inclusive) of Linak desk (cm)
#define MAX_DESK_HEIGHT 130    // Max height (inclusive) of Linak desk (cm)
#define HEIGHT_POLL_RATE 150   // Poll rate when desk is moving (ms)

// Commands
#define CMD_GET_DESK_HEIGHT "get_desk_height"
#define CMD_SET_DESK_HEIGHT "set_desk_height"
#define CMD_GET_ROOM_HEIGHT "get_room_height"
#define CMD_SET_ROOM_HEIGHT "set_room_height"

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
unsigned int room_height = 263;   // Sane default for our offices

void setup()
{
  pinMode(UP_RELAY_PIN, OUTPUT);
  pinMode(DOWN_RELAY_PIN, OUTPUT);

  digitalWrite(UP_RELAY_PIN, HIGH);
  digitalWrite(DOWN_RELAY_PIN, HIGH);

  Serial.begin(SERIAL_RATE);
  Serial.println("hello");
}

/* Copied from Stack Exchange! */
/* https://arduino.stackexchange.com/a/1237 */
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void stop_moving()
{
    digitalWrite(UP_RELAY_PIN, HIGH);
    digitalWrite(DOWN_RELAY_PIN, HIGH);
}

/* Return 0 on error */
unsigned int get_desk_height()
{
  unsigned int cm = sonar.ping() / US_ROUNDTRIP_CM;

  // HC-SR04 datasheet says these are impossible, so error.
  if (cm < MIN_DISTANCE || cm > MAX_DISTANCE) {
    return 0;
  }

  // TODO: MAKE THIS MORE ROBUST.

  return room_height - cm;
}

void set_desk_height(unsigned int target_height)
{
  unsigned int current_height = get_desk_height();

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

    // Wait a bit before polling height, otherwise we get funny values.
    delay(HEIGHT_POLL_RATE);

    unsigned int new_height = get_desk_height();
    Serial.println(new_height);

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

void loop()
{
  if (! Serial.available()) {
    delay(500);
    return;
  }

  String line = Serial.readStringUntil('\n');
  String command = getValue(line, ',', 0);

  // TODO: This could probably be a nice switch statement or something.

  if (command == CMD_GET_DESK_HEIGHT) {
    Serial.println(get_desk_height());
  } else if (command == CMD_SET_DESK_HEIGHT) {
    unsigned int desk_height = (unsigned int) getValue(line, ',', 1).toInt();

    if (desk_height >= MIN_DESK_HEIGHT && desk_height <= MAX_DESK_HEIGHT) {
      set_desk_height(desk_height);
    }
  } else if (command == CMD_GET_ROOM_HEIGHT) {
    Serial.println(room_height);
  } else if (command == CMD_SET_ROOM_HEIGHT) {
    room_height = (unsigned int) getValue(line, ',', 1).toInt();
  }

  Serial.println("ok");
}
