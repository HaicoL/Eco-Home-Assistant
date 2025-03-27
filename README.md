# Eco-Home-Assistant

<!DOCTYPE html>
<html lang="en">
<h1>Home Assistant - ESP32</h1>
  
<p>Dit project maakt gebruik van een ESP32 om een home assistant systeem te beheren met MQTT, Blynk en verschillende sensoren.</p>
  
<h2>1. WiFi, MQTT en Blynk Instellingen</h2>
<p>We beginnen met het instellen van de WiFi- en MQTT-verbindingen en de Blynk-authenticatie.</p>
  
    #define BLYNK_TEMPLATE_ID "user9"
    #define BLYNK_TEMPLATE_NAME "user9@server.wyns.it"
    #include <WiFi.h>
    #include <PubSubClient.h>
    #include <DHT.h>
    #include <Wire.h>
    #include <BH1750.h>
    #include <Adafruit_SSD1306.h>
    #include <BlynkSimpleEsp32.h>
  
    const char* ssid = "your_wifi";
    const char* password = "your_wifi_password";
    const char* mqtt_server = "the_ip_of_your_raspberry_pi";
    const char* mqtt_user = "your_MQTT_username";
    const char* mqtt_password = "your_MQTT_password";
    const char* client_id = "esp32_home_assistant";
    char auth[] = "your_auth_token";
    
<h2>2. Sensoren en Hardware Configuratie</h2>  
<p>Hier definiëren we de pinnen voor de verschillende sensoren en componenten.</p>
    
  
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
  

<h2>3. Opstellen van variabelen</h2>  
<p>Nu hebben we variabelen nodig om hier later in de code de ingelezen en binnenkomende waardes aan toe te kennen en deze te kunnen onthouden.</p>
    
  
    float min_temp = 18.0;
    float max_temp = 22.0;
    bool light_on = false;
    float lastTemperature = -1000.0;
    float lastHumidity = -1000.0;
    float lastLux = 0.0;
    bool lastLightOn = false;
    bool doorOpen = false;
    unsigned long doorOpenedTime = 0;
  

<h2>4. Opzetten van Blynk reactions</h2>  
<p>Om Blynk te laten reageren op acties die we uitvoeren gaan we functies schrijven om de nodige acties te doen werken.</p>
    
  
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
  

<h2>5. setup()</h2>
<p>In de setup maken we verbinding met de seriële monitor, WiFi, MQTT-client en Blynk-server.  
We initialiseren ook de benodigde hardware en software, zodat deze klaar zijn voor gebruik.  
De pinmodi worden ingesteld om de LEDs aan te sturen en sensorwaarden uit te lezen.  
Tot slot wordt de functie readAndSendSensorData elke 5 seconden uitgevoerd om  
de luchtvochtigheid, temperatuur en lichtniveaus naar Blynk te verzenden.</p>
  
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

<h2>6. Funcie voor het connecteren met MQTT</h2>
<p>We schrijven een functie om ervoor te zorgen dat we kunnen connecteren met MQTT. 
Hier gaan we dan ook subscriben op de topics waar data naar wordt weggeschreven om deze binnen te halen en te kunnen gebruiken. (dit kan je doen als je vanop een raspberri pi waardes wil aanpassen of een signaal versturen)
Als de connectie mislukt wordt er een print weergegeven en opnieuw geprobeerd om de verbinding te maken.</p>
  
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

<h2>7. loop()</h2>
<p>De loopfunctie beheert de MQTT-verbinding, leest sensorgegevens en controleert de deurstatus.
Als de deur 15 seconden open blijft, wordt de verwarming uitgeschakeld.
De RGB LED toont de verwarmingsstatus op basis van temperatuurinstellingen.
Sensorwaarden worden op het OLED-scherm weergegeven en de lichtstatus wordt alleen bij verandering gepubliceerd.
Tot slot wordt debug-info geprint en starten we Blynk en Blynk timer op om deze te kunnen gebruikem.</p>
  
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


<h2>8. Lezen, bewaren en versturen van sensor data</h2>
<p>We lezen de sensorgegevens opnieuw en controleren of de waarden geldig zijn voordat we ze opslaan.
Bij veranderingen worden de waarden naar MQTT en Blynk gestuurd.</p>
  
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
    

<h2>9. Deur open check functie</h2>
<p>Door de afstand van de ultrasoon in te gaan lezen kunnen we bepalen of de deur op dit moment open of dicht is.</p>
  
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

<h2>10. Toon informatie op OLED</h2>
<p>Deze functie toont de huidige meetwaarden op het OLED-scherm.
De temperatuur, vochtigheid, deurstatus, lichtstatus en helderheid worden geformatteerd en weergegeven.</p>
  
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

<h2>11. Stel RGB LED kleuren in</h2>
<p>We maken een simpele functie die we kunnen oproepen om de RGB kleuren in te stellen.</p>
  
    // a basic function to change the color of our RGB LED
    void setColor(int R, int G, int B) {
      analogWrite(LED_RED, R);
      analogWrite(LED_GREEN, G);
      analogWrite(LED_BLUE, B);
    }

<h2>Conclusie</h2>
<p>Dit project laat zien hoe je een home assistant bouwt met een ESP32, MQTT en Blynk. Het stuurt gegevens naar een dashboard en past de hardware aan op basis van de gemeten waarden.</p>

<h1>Hardware used</h1>
<ul>
    <li><strong>ESP32</strong> – Microcontroller voor sensoren, actuatoren en communicatie via MQTT en Blynk.</li>
    <li><strong>Raspberry Pi 5</strong> – Centrale hub voor dataopslag, verwerking en MQTT-server.</li>
    <li><strong>DHT11</strong> – Temperatuur- en vochtigheidssensor.</li>
    <li><strong>HC-SR04</strong> – Ultrasone sensor voor deurstatusdetectie.</li>
    <li><strong>BH1750</strong> – Lichtintensiteitssensor.</li>
    <li><strong>SSD1306 OLED Display (I2C, 128x64)</strong> – Voor het weergeven van meetwaarden op de ESP32.</li>
    <li><strong>LCD Display (op Raspberry Pi)</strong> – Voor het tonen van temperatuur en ingestelde min/max waarden.</li>
    <li><strong>RGB LED</strong> – Geeft de status van de verwarming of koeling weer.</li>
    <li><strong>Witte LED</strong> – Simuleert een lichtbron.</li>
    <li><strong>220Ω weerstand</strong> – Mogelijk nodig voor het schakelen van LED's.</li>
    <li><strong>Breadboard & Jumper Wires</strong> – Voor het maken van de elektrische verbindingen.</li>
</ul>

<h1>Aansluitschema</h1>
aansluitschema_esp32:(https://github.com/user-attachments/assets/5a1e5382-ecb3-42d3-94bf-33b6576bf85d)


<h1>Blynk app layout</h1>
Picture 1:(https://github.com/user-attachments/assets/e9b56a7a-49d1-4b68-a4b3-62b03ec97e25)
<br>
Picture 2:(https://github.com/user-attachments/assets/b13434bf-4c85-43ac-bd5a-9f27948c670a)

<h1>Grafana layout</h1>
Dashboard:(https://github.com/user-attachments/assets/33b8ab81-291e-43e9-9d23-5bddd39ba03c)

<h1>Foto's van de opstelling</h1>
bovenaf:(https://github.com/user-attachments/assets/d5c333a0-92d0-4cb2-b34b-b4e527f14479)
vooraan:(https://github.com/user-attachments/assets/06003256-62e2-4f6e-b757-da81c6079f58)


</body>
</html>

