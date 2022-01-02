#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

// FOR OTA UPDATE
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

AsyncWebServer OTAserver(80);

/**** Global variable ****/
String device_id;
String topic_device_status;
String topic_device_capability;
String topic_device_id;
String topic_task_prime_number = "leader/task/prime_number";

int num_prime_cap;
int word_count_cap;
int vect_mult_cap;

DynamicJsonDocument doc(3000); // JSONdocument where are deserialized dashboard requests
char output[3000];
/**** Global variable ****/

/**** Access Point ****/
const char* ssidAP     = "ESP32AP";
const char* passwordAP = "123456789";
IPAddress ipAP = IPAddress (10, 10, 2, 6); 
IPAddress gatewayAP = IPAddress (10, 10, 2, 6); 
IPAddress nMaskAP = IPAddress (255, 255, 255, 0); 

// Set web server port number to 88
WiFiServer server(88);
#define WIFI_DELAY 60
/**** Access Point ****/

int wifi_status = WL_IDLE_STATUS;     // the Wifi radio's status

/**** Eeprom ****/
Preferences preferences;
/**** Eeprom ****/


/**** MQTT ****/
//#define MQTT_HOST IPAddress(10, 126, 1, 27)
IPAddress MQTT_HOST;
#define MQTT_PORT 1883

AsyncMqttClient mqttClient;
/**** MQTT ****/


/**** Function definition ****/
void AccessPointOn();
void parsePostRequest(String req, String &ssid, String &passwd, String &broker_ip);
void MQTT_connect();
void onMqttConnect(bool sessionPresent);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);


void getPrimeNumbers(int start, int end, int ris[]);
int getWordCount(const char* str, int len);
void getVectorMultiplication(int A[], int B[], int C[], int lenght);

int getPrimeNumCapability();
int getWordCountCapability();
int getVectorMultiplicationCapability();

/**** Function definition ****/


void setup() {

  //Leggi in EEPROM se è presente qualche configurazione per WIFI e prova a connetterti
  preferences.begin("wifi-cred", false);

  String eeprom_wifi_password = preferences.getString("wifi_pass", "");
  String eeprom_wifi_ssid = preferences.getString("wifi_ssid", "");

  Serial.begin(9600);

  // Risolvono il bug di Access Point senza password
  WiFi.persistent(false);
  WiFi.setSleep(false);

  if(eeprom_wifi_ssid.equals("") or eeprom_wifi_password.equals("")){
    Serial.println("Non ho trovato niente in EEPROM");
    Serial.println("Attivo ESP32 come Access Point..");
    AccessPointOn();
  }
  else{
    wifi_status = WiFi.begin(eeprom_wifi_ssid.c_str(),eeprom_wifi_password.c_str());
    int count = 0;
    // Aspetta al massimo WIFI_DELAY secondi
    while(WiFi.status() != WL_CONNECTED and count <= WIFI_DELAY){
      delay(1000);
      count ++;
      Serial.println("Provo a connettermi con le credenziali in EEPROM");
    }
    if(WiFi.status() != WL_CONNECTED){
      Serial.println("Connessione non riuscita. Le credenziali in EEPROM non sono corrette..");
      Serial.println("Pulisco EEPROM..");
      preferences.clear();
      Serial.println("Attivo ESP32 come Access Point..");
      AccessPointOn();
    }
    else
      Serial.println("Connesso al wifi con le credenziali in EEPROM");
  }
  
  String eeprom_broker_ip = preferences.getString("broker_ip", "");
  MQTT_HOST.fromString(eeprom_broker_ip);

  Serial.println(WiFi.localIP());

  // Define device id Address == MAC address
  byte mac[6];
  WiFi.macAddress(mac);
  device_id = String(mac[0], HEX) + String(mac[1], HEX) + String(mac[2], HEX) + String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);
  topic_device_status = String("clients/") + device_id + String("/status");
  topic_device_capability = String("clients/") + device_id + String("/capability");
  topic_device_id = String("clients/") + device_id + String("/ip");


  // Connettiti al broker Mqtt
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.connect();
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setWill(topic_device_status.c_str(), 0, true, "0");

  // Inizializza il server HTTP per gli OTA UPDATE
  AsyncElegantOTA.begin(&OTAserver);    // Start ElegantOTA
  OTAserver.begin();
  Serial.println("HTTP server started");
  Serial.println(WiFi.localIP());

}

void loop(){
  
  MQTT_connect();
  AsyncElegantOTA.loop();
  delay(1000);
}

// Function for parse wifi ssd and password get from client on access point mode
void parsePostRequest(String req, String &ssid, String &passwd, String &broker_ip){

  // get POST body
  // curl -d "{'ssid':'House LANister', 'passwd':'***', 'broker': '10.126.1.27'}" 192.168.4.1:88
  String post_body;
  int count = 0;
  for(int i=0; i<req.length(); i++){
    if(count == 7){
      post_body += req[i];
    }
    else{
      if(req[i] == '\n')
        count ++;
    }
  }

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, post_body);
  if (error) {  
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char* ssid1 = doc["ssid"];
  const char* passwd1 = doc["passwd"];
  const char* broker1 = doc["broker"];
  ssid = String(ssid1);
  passwd = String(passwd1);
  broker_ip = String(broker1);

  return;
}

// Function for power on access point
void AccessPointOn(){
  
  WiFi.softAP(ssidAP, passwordAP);
  WiFi.softAPConfig(ipAP, gatewayAP, nMaskAP);
  //IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(ipAP);
  
  server.begin();
  
  // Stringhe dove inserire ssid e passwd del wifi tramite richiesta post
  String ssid;
  String passwd;
  String broker_ip;

  while(1){
    WiFiClient client = server.available();
    Serial.println("Pronto a ricevere client");
    Serial.println(client);
    if (client) {                             // If a new client connects,
      Serial.println("New Client.");          // print a message out in the serial port
      String post_request = client.readString();
      Serial.println(post_request);
      
      client.println("HTTP/1.1 200 OK");
      client.println("Content-type:text/html");
      client.println("Connection: close");
      client.println();
      
      // The HTTP response ends with another blank line
      client.println();

      // Close the connection
      client.stop();
      Serial.println("Client disconnected.");
      Serial.println("");

    
      parsePostRequest(post_request, ssid, passwd, broker_ip);
     
      // una volta ottenuti ssid e passwd del wifi esci dal ciclo
      break;

    }
    delay(1000);
  }

  // Chiudi Access Point
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  server.end();

  wifi_status = WiFi.begin(ssid.c_str(),passwd.c_str());

  while(WiFi.status() != WL_CONNECTED){
    // Serial.println(WiFi.status());
    Serial.println("Mi sto connettendo al wifi....");
    delay(1000);
  }


  Serial.println("Connesso al Wifi");
  Serial.println("Scrivo in EEPROM la configurazione del wifi");

  // Se esci vuol dire che mi sono connesso al wifi -> salva le credenziali in EEPROM
  preferences.putString("wifi_pass", passwd);
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("broker_ip", broker_ip);

}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  // Stop if already connected.
  if (mqttClient.connected()) {
    return;
  }

  Serial.println("Connecting to MQTT... ");

  mqttClient.connect();
  while(mqttClient.connected() != 1){
    delay(1000);
  }
  
  Serial.println("MQTT Connected!");
}

// Callback connect() to MQTT
void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  // Sottoscriviti ai topic
  uint16_t packetIdSub = mqttClient.subscribe("leader/task/prime_num", 1);
  Serial.print("Subscribing at packetId: ");
  Serial.println(packetIdSub);

  packetIdSub = mqttClient.subscribe("leader/task/word_count", 1);
  Serial.print("Subscribing at packetId: ");
  Serial.println(packetIdSub);

  packetIdSub = mqttClient.subscribe("leader/task/vect_mult", 1);
  Serial.print("Subscribing at packetId: ");
  Serial.println(packetIdSub);

  // Calcola le capability
  num_prime_cap = getPrimeNumCapability();
  word_count_cap = getWordCountCapability();
  vect_mult_cap = getVectorMultiplicationCapability();
 
  doc.clear();

  doc["prime_num"] = num_prime_cap;
  doc["word_count"] = word_count_cap;
  doc["vect_mult"] = vect_mult_cap;

  char output[60];

  serializeJson(doc, output);
  packetIdSub = mqttClient.publish(topic_device_status.c_str(), 1, true, "1");
  packetIdSub = mqttClient.publish(topic_device_capability.c_str(), 1, true, output);
  const char* ip = WiFi.localIP().toString().c_str();
  packetIdSub = mqttClient.publish(topic_device_id.c_str(), 1, true, ip);
}

// Funzione che restituisce i primi n numeri primi
void getPrimeNumbers(int start, int end, int ris[]){
  int j, flag;
  int i=0;
  while(start <= end){
    flag = 0;
    for(j=2; j <=start/2; j++){
      if(start%j == 0){
        flag = 1;
        break;
      }
    }
    if(flag == 0){
      ris[i] = start;
      i++;
      //Serial.print(start);
      //Serial.print("  ");
    }
    start++;
  }
}

// Funzione che restituisce il numero di parole in una frase
int getWordCount(const char* str, int len){
  int word_num = 0;
  int start_index = 0;
  for(int i=0; i<len; i++){
    if(str[i] == ' ')
      start_index ++;
    else
      break;
  }
  for(int i=start_index; i<len; i++){
    if(i > 0){
      if(str[i] == ' ' && str[i-1] != ' ')
        word_num++;
      if(i == len-1 && str[i] != ' ')
        word_num++;
    }
      
  }
  return word_num;
}

// Funzione che restituisce la moltiplicazione di due vettori
void getVectorMultiplication(int A[], int B[], int C[], int lenght){
  int ris = 0;
  for(int i=0; i<lenght; i++){
    ris = 0;
    for(int j=0; j<lenght; j++)
      ris = ris + (A[i]*B[j]);
    C[i] = ris;
  }
}

// Funzione che esegue n volte il calcolo dei numeri primi nel caso peggiore (end = 400) e ritorna quanti ne riesce ad eseguire al secondo
int getPrimeNumCapability(){
  unsigned long start = 0;
  unsigned long endd =0;
  unsigned long delta = 0;
  float num_sec=0;
  int ris[400];
  for(int i=0; i<100; i++){
    start = micros();
    getPrimeNumbers(2, 400, ris);
    endd = micros();
    delta = endd-start;
    num_sec = num_sec + delta;
  }
  num_sec = 1/((num_sec/100)/1000000);
  return (int(num_sec-50));  
}

// Funzione che esegue n volte il conteggio delle parole in una frase di 5000 caratteri e ritorna quanti ne riesce ad eseguire al secondo
int getWordCountCapability(){
  unsigned long start = 0;
  unsigned long endd =0;
  unsigned long delta = 0;
  float num_sec=0;
  int res = 0;
  const char* str = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aenean fringilla venenatis nisi, vitae congue neque congue in. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Pellentesque placerat efficitur felis, ut aliquet nisl dapibus non. Phasellus id purus velit. Pellentesque in nisl ut nulla dignissim congue. Praesent sed dolor metus. Vivamus id est vitae dolor vehicula efficitur. Mf dfbu fu sdi du sduibas yuv dyuvu davyuv ucatsyc sacy dvua dyvu dvsu ";
  for(int i=0; i<100; i++){
    start = micros();
    res = getWordCount(str, 500);    
    endd = micros();
    delta = endd-start;
    num_sec = num_sec + delta;
  }
  num_sec = 1/((num_sec/100)/1000000);
  Serial.println(res);
  return (int(num_sec-50));  
}

// Funzione che esegue n volte la moltiplicazione di due vettori di 30 componenti e ritorna quanti ne riesce ad eseguire al secondo
int getVectorMultiplicationCapability(){
  unsigned long start = 0;
  unsigned long endd =0;
  unsigned long delta = 0;
  float num_sec=0;
  int A[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  int B[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  int C[30];
  for(int i=0; i<100; i++){
    start = micros();
    getVectorMultiplication(A,B,C,30);    
    endd = micros();
    delta = endd-start;
    num_sec = num_sec + delta;
  }
  num_sec = 1/((num_sec/100)/1000000);
  Serial.println(int(num_sec-50));
  return (int(num_sec-50));  
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  
  String formatted_payload = "";
  for (size_t i = 0; i < len; ++i)
    formatted_payload = formatted_payload + payload[i];

  if(strcmp(topic, "leader/task/prime_num") == 0){
    doc.clear();
    DeserializationError error = deserializeJson(doc, payload, len);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }
    int num_sec = 0;
    const char* id = device_id.c_str();
    for(JsonObject dev_item : doc["dev"].as<JsonArray>()) {
      if(strcmp(dev_item["id"], id) == 0){
        num_sec = dev_item["num_sec"];
        break;
      } 
    }
    int param = doc["param"];
    if(num_sec > num_prime_cap)
      num_sec = num_prime_cap;
    int ris[param];
    // Clear array
    for(int i=0; i<param; i++)
      ris[i] = 0;
    
    for(int i=0; i<num_sec; i++)
      getPrimeNumbers(2,param, ris);

    doc.clear();
    doc["task"] = "prime_num";
    String ris_string ="";
    for(int i=0; i<param; i++){
      if(ris[i] == 0)
        break;
      ris_string = ris_string + ris[i];
      ris_string = ris_string + " ";
    }
    doc["res"] = ris_string;

    serializeJson(doc, output);
    mqttClient.publish("task/result", 1, false, output);
  }
  else if (strcmp(topic, "leader/task/word_count") == 0)
  {
    doc.clear();
    DeserializationError error = deserializeJson(doc, payload, len);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }
    int num_sec = 0;
    const char* id = device_id.c_str();
    for(JsonObject dev_item : doc["dev"].as<JsonArray>()) {
      if(strcmp(dev_item["id"], id) == 0){
        num_sec = dev_item["num_sec"];
        break;
      } 
    }
    const char* param = doc["param"];
    if(num_sec > num_prime_cap)
      num_sec = num_prime_cap;
    
    int ris = 0;
    for(int i=0; i<num_sec; i++)
      ris = getWordCount(param, strlen(param));

    doc.clear();
    doc["task"] = "word_count";
    doc["res"] = String(ris);
    serializeJson(doc, output);
    mqttClient.publish("task/result", 1, false, output);

  }
  else if (strcmp(topic, "leader/task/vect_mult") == 0)
  {
    doc.clear();
    DeserializationError error = deserializeJson(doc, payload, len);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }
    int num_sec = 0;
    const char* id = device_id.c_str();
    for(JsonObject dev_item : doc["dev"].as<JsonArray>()) {
      if(strcmp(dev_item["id"], id) == 0){
        num_sec = dev_item["num_sec"];
        break;
      } 
    }

    const char* AS = doc["A"];
    const char* BS = doc["B"];

    if(num_sec > num_prime_cap)
      num_sec = num_prime_cap;
    
    int lenght = 1;
    for(int i=0; i<strlen(AS); i++){
        if(AS[i] == ' ')
            lenght ++;
    }
    int A[lenght];
    int B[lenght];
    int C[lenght];
    char* name = NULL;
    char* copy = strdup(AS);
    int j = 0;

    name = strtok(copy, " ");
    while(name != NULL){
        A[j] = atoi(name);
        j++;
        name = strtok(NULL, " "); 
    }

    copy = strdup(BS);
    j = 0;
    name = strtok(copy, " ");
    while(name != NULL){
        B[j] = atoi(name);
        j++;
        name = strtok(NULL, " "); 
    }
    free(copy);

    for(int i=0; i<num_sec; i++)
      getVectorMultiplication(A,B,C,lenght);
    
    String CS = "";
      for(int i=0; i<lenght; i++){
         CS = CS+C[i]; CS = CS+' ';
    }

    // Pubblica il risultato
    doc.clear();
    doc["task"] = "vect_mult";
    doc["res"] = CS;
    serializeJson(doc, output);
    mqttClient.publish("task/result", 1, false, output);
  }
  
  




}


