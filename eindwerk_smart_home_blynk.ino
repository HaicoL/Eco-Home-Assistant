#define BLYNK_TEMPLATE_ID "user9"
#define BLYNK_TEMPLATE_NAME "user9@server.wyns.it"

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_SSD1306.h>
#include <BlynkSimpleEsp32.h>

// WiFi, MQTT en Blynk instellingen
const char* ssid = "Proximus-Home-089155";
const char* password = "xrsmp5uk6uj2kss6";
const char* mqtt_server = "192.168.129.71";
const char* mqtt_user = "haico";
const char* mqtt_password = "";
const char* client_id = "esp32_home_assistant";
char auth[] = "8Wle5tD5B__VxYCnc7kVadJXSRUZWZEm";

WiFiClient espClient;
PubSubClient client(espClient);

// DHT11 sensor
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Witte LED
#define LED_PIN 2

// RGB LED
#define LED_RED 25
#define LED_GREEN 26
#define LED_BLUE 27

// HC-SR04 ultrasone sensor
#define TRIG_PIN 5
#define ECHO_PIN 18

// OLED scherm
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// BH1750 lichtsensor
BH1750 lightMeter;

// Blynk widgets
BlynkTimer timer;
WidgetLED ledLight(V0);
WidgetLED ketel_status(V2);
WidgetTerminal terminal(V10);

float min_temp = 18.0;
float max_temp = 22.0;
bool light_on = false;
float lastTemperature = -1000.0;
float lastHumidity = -1000.0;
float lastLux = 0.0;
bool lastLightOn = false;
bool doorOpen = false;
unsigned long doorOpenedTime = 0;

// once blynk is connected succesfully we want to turn on the RGB led to 
// indicate that our heating is on. We also want to pull in the last written 
// values to sync our system and not lose data. Lastly we want to clear our 
// terminal for any leftover data and print the options.
BLYNK_CONNECTED() {
  ketel_status.on();
  Blynk.syncAll();

  terminal.clear();
  terminal.println("=== HOME ASSISTANT TERMINAL ===");
  terminal.println("Type 'DOOR' or 'LIGHT' to get status.");
  terminal.println("-------------------------------");
}

// this command is listening when something is written to V1
// V1 is a button in the app that will switch the led on and off.
BLYNK_WRITE(V1) {
  int pinValue = param.asInt();
  light_on = pinValue == 1;
  client.publish("home/light", light_on ? "on" : "off", true);
  digitalWrite(LED_PIN, light_on ? HIGH : LOW);
  if (light_on) {
    ledLight.on();
  } else {
    ledLight.off();
  }
}

// once a value is written to V5 (Step H input) we want to read this in
// and update our minimum temperature accordingly
BLYNK_WRITE(V5) {  
  float new_temp = param.asFloat();
  if (new_temp != min_temp) {
    min_temp = new_temp;
    Serial.printf("Nieuwe min temp: %.1f\n", min_temp);
    client.publish("home/temp_min", String(min_temp).c_str(), true);
    Blynk.virtualWrite(V4, min_temp);
  }
}

// once a value is written to V7 (Step H input) we want to read this in
// and update our maximum temperature accordingly
BLYNK_WRITE(V7) { 
  float new_temp = param.asFloat();
  if (new_temp != max_temp) {
    max_temp = new_temp;
    Serial.printf("Nieuwe max temp: %.1f\n", max_temp);
    client.publish("home/temp_max", String(max_temp).c_str(), true);
    Blynk.virtualWrite(V6, max_temp);
  }
}

// for our terminal we want to give access to the status of our door 
// en the status of our light, this way you can check if your door was left
// open. More info can be added later if wanted or if more sensors are connected.
BLYNK_WRITE(V10) {
  if (String("DOOR") == param.asStr()) {
    bool doorCurrentlyOpen = isDoorOpen();
    terminal.printf("The door is currently %s\n", doorCurrentlyOpen ? "OPEN" : "CLOSED");
  } else if (String("LIGHT") == param.asStr()) {
    terminal.printf("The light is currently %s\n", light_on ? "ON" : "OFF");
  } else {
    terminal.println("Invalid command. Try 'DOOR' or 'LIGHT'.");
  }
  terminal.flush();
  delay(4000);
  terminal.clear();

  terminal.println("=== HOME ASSISTANT TERMINAL ===");
  terminal.println("Type 'DOOR' or 'LIGHT' to get status.");
  terminal.println("-------------------------------");
}

// in our setup we connect to serial monitor, wifi, MQTT client and Blynk server
// we also start our needed hardware and software to make them ready for use
// our pin modes also need to be set to be able to control our leds and read our values in
// lastly we want to run the readAndSendSensorData function that sends our humidity, 
// temperature and our light levels to blynk every 5 seconds
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Verbonden!");

  client.setServer(mqtt_server, 1883);
  connectMQTT();

  Wire.begin();
  lightMeter.begin();
  dht.begin();

  Blynk.begin(auth, ssid, password, "server.wyns.it", 8081);

  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED niet gevonden!");
    while (true)
      ;
  }

  timer.setInterval(5000L, readAndSendSensorData);
}

// we connect to the MQTT network with our credentials and subscribe 
//to the topics to read the last written values in the database
void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Verbinden met MQTT...");
    if (client.connect(client_id, mqtt_user, mqtt_password)) {
      Serial.println("Verbonden!");
      client.subscribe("home/temp_min");
      client.subscribe("home/temp_max");
      client.subscribe("home/light");
    } else {
      Serial.print("Fout, rc=");
      Serial.print(client.state());
      Serial.println(" Wachten op herverbinding...");
      delay(5000);
    }
  }
}

// we check if a connection was made with the MQTT server, if not
// we try to make the connection again. Then we read out the values 
// of our sensors and check if the door is open
void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  float lux = lightMeter.readLightLevel();
  bool doorCurrentlyOpen = isDoorOpen();

  // checking is the door is currently open, if this
  // is the case we set the soor open value to true and
  // start the timer for 15 seconds, after 15 seconds
  // we shut of the heating to save power
  if (doorCurrentlyOpen) {
    if (!doorOpen) {
      doorOpen = true;
      doorOpenedTime = millis();
    } else if (millis() - doorOpenedTime >= 15000) {
      setColor(0, 0, 0);
      ketel_status.setColor("#000000");
    }
  } else {
    doorOpen = false;
  }

  // controlling the RGB LED based on the minimim and macimum 
  // set temperatures to set the color showing the status of 
  // the heating
  if (!doorCurrentlyOpen) {
    if (temperature < min_temp) {
      setColor(255, 0, 0);
      ketel_status.setColor("#ff0000");
    } else if (temperature > max_temp) {
      setColor(0, 0, 255);
      ketel_status.setColor("#0000ff");
    } else {
      setColor(0, 255, 0);
      ketel_status.setColor("#00ff00");
    }
  }

  // display on the OLED screen
  displayInformation(temperature, humidity, doorCurrentlyOpen, light_on, lux);

  // we publish the updated status of our light, but only
  // when the value has changed to reduce data trafic
  if (light_on != lastLightOn) {
    client.publish("home/light_status", light_on ? "ON" : "OFF", true);
    lastLightOn = light_on;
  }

  // Debug info
  Serial.println("----Overzicht----");
  Serial.printf("Min: %.1f Max: %.1f\n", min_temp, max_temp);
  Serial.printf("Temp: %.1fC\n", temperature);
  Serial.printf("Vocht: %.1f%\n", humidity);
  Serial.printf("Deur: %s\n", doorCurrentlyOpen ? "OPEN" : "CLOSED");
  Serial.printf("Licht: %s\n", light_on ? "ON" : "OFF");
  Serial.printf("Helderheid: %.1flx\n", lux);
  Serial.println("");

  // we set our Blynk and the Blynk timer to run
  // making it possible to read and write from our app
  Blynk.run();
  timer.run();

  delay(5000);
}

// we read the values of our sensors again to be able to use the 
// values and send them to our Blynk app. We also do checks to
// make sure the read values are actual numbers before we safe them.
// Then we publish it to MQTT if the value has changes and to Blynk
void readAndSendSensorData() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  float lux = lightMeter.readLightLevel();

  // Controle op NaN voor temperatuur en luchtvochtigheid
  if (isnan(temperature)) {
    Serial.println("Fout: Temperatuurmeting mislukt!");
    temperature = lastTemperature;
  }
  if (isnan(humidity)) {
    Serial.println("Fout: Vochtigheidsmeting mislukt!");
    humidity = lastHumidity;
  }

  if (isnan(lux)) {
    Serial.println("Fout: Lichtsterktemeting mislukt!");
    lux = lastLux;
  }

  if (temperature != lastTemperature) {
    client.publish("home/temp", String(temperature).c_str(), true);
    lastTemperature = temperature;
  }

  if (humidity != lastHumidity) {
    client.publish("home/humidity", String(humidity).c_str(), true);
    lastHumidity = humidity;
  }

  lastLux = lux;

  Blynk.virtualWrite(V3, temperature);
  Blynk.virtualWrite(V8, humidity);
  Blynk.virtualWrite(V9, lux);
}

// we check if our door is open with the set distance
bool isDoorOpen() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = duration * 0.034 / 2;
  return (distance < 10.0);
}

// we set the needed values we want to display as the parameters 
// to be able to use them
void displayInformation(float temperature, float humidity, bool doorOpen, bool lightStatus, float lux) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("Min: %.1f Max: %.1f\n", min_temp, max_temp);
  display.printf("Temp: %.1fC\n", temperature);
  display.printf("Vocht: %.1f%\n", humidity);
  display.printf("Deur: %s\n", doorOpen ? "OPEN" : "CLOSED");
  display.printf("Licht: %s\n", lightStatus ? "ON" : "OFF");
  display.printf("Helderheid: %.1flx\n", lux);
  display.display();
}

// a basic function to change the color of our RGB LED
void setColor(int R, int G, int B) {
  analogWrite(LED_RED, R);
  analogWrite(LED_GREEN, G);
  analogWrite(LED_BLUE, B);
}
