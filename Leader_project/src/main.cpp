#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

// FOR OTA UPDATE
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

AsyncWebServer OTAserver(80);

/**** Device data structure   ****/
#define MAX_CLIENTS 20

struct clients{
  bool state;
  int cap_prime_num;
  int cap_word_count;
  int cap_vect_mult;
  String id;
};

clients clients_array[MAX_CLIENTS];

// Capability del leader
int num_prime_cap;
int word_count_cap;
int vect_mult_cap;

// Numero di device a cui il leader ha fatto offloading
int offload_device_count = 0;
int offload_device_count_result = 0;
/**** Device data structure   ****/

/**** Access Point ****/
const char* ssidAP     = "ESP32AP";
const char* passwordAP = "123456789";
IPAddress ipAP = IPAddress (10, 10, 2, 6); 
IPAddress gatewayAP = IPAddress (10, 10, 2, 6); 
IPAddress nMaskAP = IPAddress (255, 255, 255, 0); 

// Set web server port number to 88
WiFiServer server(88);
/**** Access Point ****/

int wifi_status = WL_IDLE_STATUS;     // the Wifi radio's status
#define WIFI_DELAY 60

/**** Eeprom ****/
Preferences preferences;
//Preferences are: wifi_ssid, wifi_pass, broker_ip
/**** Eeprom ****/

/**** MQTT ****/
//#define MQTT_HOST IPAddress(10, 126, 1,27)
IPAddress MQTT_HOST;
#define MQTT_PORT 1883

AsyncMqttClient mqttClient;

const char* leader_status_topic = "leader/lead/status";
const char* leader_capability_topic = "leader/lead/capability";
const char* leader_ip_topic = "leader/lead/ip";

DynamicJsonDocument doc(3000); // JSONdocument where are deserialized dashboard requests
char output[3000];

// Latency variable
unsigned long send_time = 0;
unsigned long receive_time = 0;
float delta = 0;
/**** MQTT ****/


void accessPointOn();
void parsePostRequest(String req, String &ssid, String &passwd, String &broker_ip);
void MQTT_connect();
void onMqttConnect(bool sessionPresent);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void initClientsArray();

void getPrimeNumbers(int start, int end, int ris[]);
int getWordCount(const char* str, int len);
void getVectorMultiplication(int A[], int B[], int C[], int lenght);

int getPrimeNumCapability();
int getWordCountCapability();
int getVectorMultiplicationCapability();

void offloadTaskPrimeNumbers(int diff, int data);
void offloadTaskVectorMultiplication(int diff, int A[], int B[], int lenght);
void offloadTaskWordCount(int diff, const char* data);


bool SubscribeToCliensArray(String device_id);
void updateDeviceState(bool state, String device_id);
void updateDeviceCapability(int cap_prime_num, int cap_word_count, int cap_vect_mult, String device_id);
void printClientsArray();



void setup() {

  // Inizializza clients_array
  initClientsArray();

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
    accessPointOn();
  }
  else{
    wifi_status = WiFi.begin(eeprom_wifi_ssid.c_str(),eeprom_wifi_password.c_str());
    int count = 0;
    // Aspetta al massimo 60 secondi
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
      accessPointOn();
    }
    else
      Serial.println("Connesso al wifi con le credenziali in EEPROM");
  }
 String eeprom_broker_ip = preferences.getString("broker_ip", "");
 MQTT_HOST.fromString(eeprom_broker_ip);

  Serial.println(WiFi.localIP());

  // Connettiti al broker Mqtt
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.connect();
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setWill(leader_status_topic, 0, true, "0");

  // Inizializza il server HTTP per gli OTA UPDATE
  AsyncElegantOTA.begin(&OTAserver);    // Start ElegantOTA
  OTAserver.begin();
  Serial.println("HTTP server started");
  Serial.println(WiFi.localIP());

}

void loop(){
  
  MQTT_connect();
  AsyncElegantOTA.loop();
  
  delay(5000);
  //offloadTaskPrimeNumber(200);
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
void accessPointOn(){
  
  WiFi.softAP(ssidAP, passwordAP);
  WiFi.softAPConfig(ipAP, gatewayAP, nMaskAP);
  //IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(ipAP);
  
  server.begin();
  
  // Stringhe dove inserire ssid e passwd del wifi tramite richiesta post e l'indirizzo ip del broker mqtt
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

  // Sottoscriviti ai topic
  uint16_t packetIdSub = mqttClient.subscribe("clients/+/status", 1);
  Serial.print("Subscribing at packetId: ");
  Serial.println(packetIdSub);

  packetIdSub = mqttClient.subscribe("clients/+/capability", 1);
  Serial.print("Subscribing at packetId: ");
  Serial.println(packetIdSub);

  packetIdSub = mqttClient.subscribe("task/result", 1);
  Serial.print("Subscribing at packetId: ");
  Serial.println(packetIdSub);

  packetIdSub = mqttClient.subscribe("dashboard/task", 1);
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
  packetIdSub = mqttClient.publish(leader_status_topic, 1, true, "1");
  packetIdSub = mqttClient.publish(leader_capability_topic, 1, true, output);
  const char* ip = WiFi.localIP().toString().c_str();
  packetIdSub = mqttClient.publish(leader_ip_topic, 1, true, ip);

}

// Funzione che inizializza l'array di clients
void initClientsArray(){
  for(int i=0; i<MAX_CLIENTS; i++){
    clients_array[i].id = "";
    clients_array[i].state = false;
    clients_array[i].cap_prime_num = 0;
    clients_array[i].cap_word_count = 0;
    clients_array[i].cap_vect_mult = 0;
  }
  return;
}

// Funzione che ritorna una sottostringa di un topic: es: clients/dev1/state ritorna dev1 se index_start e index_end sono 7 e 12
String getSubstring(String topic_string, int index_start, int index_end){
  if(index_start == -1 or index_end == -1)
    return "";
  else
    return topic_string.substring(index_start, index_end);
}

// Funzione che iscrive un clients nel clients array
bool SubscribeToCliensArray(String device_id){
  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients_array[i].id != ""){
      if(clients_array[i].id.equals(device_id)){
        Serial.println("Client già registrato");
        return true;
      }
    }
    else{
      clients_array[i].id = device_id;
      Serial.println("Registro il client");
      return true;
    }
  }
  Serial.println("Ci sono già troppi clients registrati");
  return false;
}

// Funzione che aggiorna lo stato di un client
void updateDeviceState(bool state, String device_id){
  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients_array[i].id.equals(device_id)){
      clients_array[i].state = state;
      return;
    }
  }
  return;
}

// Funzione che aggiorna le capability di un client
void updateDeviceCapability(int cap_prime_num, int cap_word_count, int cap_vect_mult, String device_id){
  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients_array[i].id.equals(device_id)){
      clients_array[i].cap_prime_num = cap_prime_num;
      clients_array[i].cap_word_count = cap_word_count;
      clients_array[i].cap_vect_mult = cap_vect_mult;
    }
  }
  return;
}

// Funzione che stampa ClientsArray
void printClientsArray(){
  for(int i=0; i<MAX_CLIENTS; i++){
    Serial.print("Device ");
    Serial.print(i);
    Serial.println(":");
    if(clients_array[i].id != ""){
      Serial.println("  id: " + clients_array[i].id);
      Serial.print("  state: ");
      Serial.println(clients_array[i].state);
      // if(clients_array[i].state)
      //   Serial.println("1");
      // else
      //   Serial.println("0");
      Serial.print("  capability prime num: ");
      Serial.println(clients_array[i].cap_prime_num);
      Serial.print("  capability word count: ");
      Serial.println(clients_array[i].cap_word_count);
      Serial.print("  capability vect mult: ");
      Serial.println(clients_array[i].cap_vect_mult);
    }
  }
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

// Funzione che pubblica un messaggio sul topic leader/task/prime_number per offloadare il task prime number
void offloadTaskPrimeNumbers(int diff, int data){
  doc.clear();
  JsonArray dev = doc.createNestedArray("dev");
  int client_cap = 0;
  int client_cap_tot = 0;
  int device_online = 0;
  
  // Conta le capability totali dei device online e quanti sono online
  for(int i=0; i<MAX_CLIENTS; i++){
    client_cap = clients_array[i].cap_prime_num;
    if(clients_array[i].state == true &&  client_cap> 0){
      client_cap_tot = client_cap_tot + client_cap;
      device_online ++;
    }
  }

  offload_device_count = device_online;

  int count = 0;
  int actual_diff = diff;
  for(int i=0; i<MAX_CLIENTS; i++){
    client_cap = clients_array[i].cap_prime_num;
    if(clients_array[i].state == true &&  client_cap> 0){
      count ++;
      int num_sec = 0;
      // se è l'ultimo device gli assegno la differenza
      if(count == device_online)
        num_sec = actual_diff;
      else
        num_sec = int(round((float(diff)*float(client_cap))/float(client_cap_tot)));
      actual_diff = actual_diff - num_sec;
      JsonObject device = dev.createNestedObject();
      device["id"] = clients_array[i].id;
      device["num_sec"] = num_sec;
    }
  }

  doc["param"] = data;

  serializeJson(doc, output);
  mqttClient.publish("leader/task/prime_num", 1, false, output);
}

// Funzione che pubblica un messaggio sul topic leader/task/word_count per offloadare il task word count
void offloadTaskWordCount(int diff, const char* data){
  doc.clear();
  JsonArray dev = doc.createNestedArray("dev");
  int client_cap = 0;
  int client_cap_tot = 0;
  int device_online = 0;
  
  // Conta le capability totali dei device online e quanti sono online
  for(int i=0; i<MAX_CLIENTS; i++){
    client_cap = clients_array[i].cap_word_count;
    if(clients_array[i].state == true &&  client_cap> 0){
      client_cap_tot = client_cap_tot + client_cap;
      device_online ++;
    }
  }

  offload_device_count = device_online;


  int count = 0;
  int actual_diff = diff;
  for(int i=0; i<MAX_CLIENTS; i++){
    client_cap = clients_array[i].cap_word_count;
    if(clients_array[i].state == true &&  client_cap> 0){
      count ++;
      int num_sec = 0;
      // se è l'ultimo device gli assegno la differenza
      if(count == device_online)
        num_sec = actual_diff;
      else
        num_sec = int(round((float(diff)*float(client_cap))/float(client_cap_tot)));
      actual_diff = actual_diff - num_sec;
      JsonObject device = dev.createNestedObject();
      device["id"] = clients_array[i].id;
      device["num_sec"] = num_sec;
    }
  }

  doc["param"] = data;

  serializeJson(doc, output);
  mqttClient.publish("leader/task/word_count", 1, false, output);

}

// Funzione che pubblica un messaggio sul topic leader/task/vector_multiplication per offloadare il task vector multiplication
void offloadTaskVectorMultiplication(int diff, int A[], int B[], int lenght){
  doc.clear();
  JsonArray dev = doc.createNestedArray("dev");
  int client_cap = 0;
  int client_cap_tot = 0;
  int device_online = 0;
  
  // Conta le capability totali dei device online e quanti sono online
  for(int i=0; i<MAX_CLIENTS; i++){
    client_cap = clients_array[i].cap_vect_mult;
    if(clients_array[i].state == true &&  client_cap> 0){
      client_cap_tot = client_cap_tot + client_cap;
      device_online ++;
    }
  }

  offload_device_count = device_online;

  int count = 0;
  int actual_diff = diff;
  for(int i=0; i<MAX_CLIENTS; i++){
    client_cap = clients_array[i].cap_vect_mult;
    if(clients_array[i].state == true &&  client_cap> 0){
      count ++;
      int num_sec = 0;
      // se è l'ultimo device gli assegno la differenza
      if(count == device_online)
        num_sec = actual_diff;
      else
        num_sec = int(round((float(diff)*float(client_cap))/float(client_cap_tot)));
      actual_diff = actual_diff - num_sec;
      JsonObject device = dev.createNestedObject();
      device["id"] = clients_array[i].id;
      device["num_sec"] = num_sec;
    }
  }

  String AS = "";
  String BS = "";
  for(int i=0; i<lenght; i++){
    AS = AS+A[i]; 
    if(i != lenght-1)
      AS = AS+' ';
    BS = BS+B[i]; 
    if(i != lenght-1)
      BS = BS+' ';
  }
  doc["A"] = AS;
  doc["B"] = BS;

  serializeJson(doc, output);
  mqttClient.publish("leader/task/vect_mult", 1, false, output); 


}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.println("Publish received.");
  Serial.print("topic: ");
  Serial.println(topic);

  String formatted_payload = "";
  for (size_t i = 0; i < len; ++i)
    formatted_payload = formatted_payload + payload[i];
  
  // Se il topic è fisso, che può essere "dashboard/task" oppure "task/result"
  if(strcmp(topic,"dashboard/task") == 0)
  {
    doc.clear();
    DeserializationError error = deserializeJson(doc, payload, len);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    const char* task = doc["task"];
    const char* data = doc["data"]; 
    int num_sec = doc["num_sec"];

    // Se il task è quello dei numeri primi
    if(strcmp(task, "prime_num") == 0){
      int num = atoi(data);
      int prime_number_result[num];
      // Clear array
      for(int i=0; i<num; i++){
        prime_number_result[i] = 0;
      }
      // se il leader non può risolvere tutte le iterazioni da solo
      if(num_sec > num_prime_cap){
        offloadTaskPrimeNumbers(num_sec-num_prime_cap, num);
        for(int i=0; i<num_prime_cap; i++)
          getPrimeNumbers(2,num, prime_number_result);
      }
      else{
        for(int i=0; i<num_sec; i++)
          getPrimeNumbers(2,num, prime_number_result);
      }

  
      doc.clear();
      doc["task"] = "prime_num";
      String res ="";
      for(int i=0; i<num; i++){
        if(prime_number_result[i] == 0)
          break;
        res = res + prime_number_result[i];
        res = res + " ";
      }
      doc["res"] = res;

      serializeJson(doc, output);
      mqttClient.publish("task/result", 1, false, output);

      // Fai partire il timer per la latenza
      send_time = micros();
      
    }
    if(strcmp(task, "word_count") == 0)
    {
      int num=0;
      // se il leader non può risolvere tutte le iterazioni da solo
      if(num_sec > word_count_cap)
      {
        offloadTaskWordCount(num_sec-word_count_cap,data);
        Serial.println("NON CI RIESCO DA SOLO");
        for(int i=0; i<word_count_cap; i++)
          num = getWordCount(data,strlen(data));
      }
      else{
        for(int i=0; i<num_sec; i++)
          num = getWordCount(data,strlen(data));
      }

      doc.clear();
      doc["task"] = "word_count";
      doc["res"] = String(num);
      serializeJson(doc, output);
      mqttClient.publish("task/result", 1, false, output);

      // Fai partire il timer per la latenza
      send_time = micros();
    }
    if(strcmp(task, "vect_mult") == 0)
    {
      String data_string = String(data);
      int open = data_string.indexOf("{");
      int close = data_string.indexOf("}"); 
      int diff = close-open;
      char d[diff];
      int j = 0;
      char* name = NULL;
      int size_vect = 0;

      // Inserisci nella stringa d il primo vettore, es da {1 2 3}, {4 2 5} in d viene messo 1 2 3
      for(int i=open+1; i<close; i++){
          d[j] = data_string[i];  j++;
      }

      // Trova la dimensione dei vettori
      name = strtok(d, " ");
      while(name != NULL){
        size_vect ++;
        name = strtok(NULL, " "); 
      }
 
      // Reimposta la stringa d
      j=0;
      for(int i=open+1; i<close; i++){
          d[j] = data_string[i];  j++;
      }

      // Inserisci in A il primo vettore
      int A[size_vect];
      name = NULL;
      name = strtok(d, " ");
      j = 0;
      while(name != NULL){
        A[j] = atoi(name);
        j++;
        name = strtok(NULL, " "); 
      }

      // Inserisci in d il secondo vettore
      open = data_string.indexOf("{", close+1);
      close = data_string.indexOf("}", open+1); 
      j = 0;
      for(int i=open+1; i<close; i++){
          d[j] = data_string[i];  j++;
      }

      // Inserisci in B il secondo vettore
      int B[size_vect];
      name = NULL;
      name = strtok(d, " ");
      j = 0;
      while(name != NULL){
        B[j] = atoi(name);
        j++;
        name = strtok(NULL, " "); 
      }
      int C[size_vect];

       // se il leader non può risolvere tutte le iterazioni da solo
      if(num_sec > vect_mult_cap)
      {
        offloadTaskVectorMultiplication(num_sec-vect_mult_cap, A, B, size_vect);
        for(int i=0; i<vect_mult_cap; i++)
          getVectorMultiplication(A,B,C,size_vect);
      }
      else{
        for(int i=0; i<num_sec; i++)
          getVectorMultiplication(A,B,C,size_vect);
      }

      String CS = "";
      for(int i=0; i<size_vect; i++){
         CS = CS+C[i]; CS = CS+' ';
      }

      // Pubblica il risultato
      doc.clear();
      doc["task"] = "vect_mult";
      doc["res"] = CS;
      serializeJson(doc, output);
      mqttClient.publish("task/result", 1, false, output);

      // Fai partire il timer per la latenza
      send_time = micros();
    }
  }
  else if (strcmp(topic,"task/result") == 0)
  {
    offload_device_count_result ++;
    //Serial.println(offload_device_count);
    //Serial.println(offload_device_count_result);

    if(offload_device_count_result == offload_device_count){
      Serial.println("Anche i device hanno terminato");
      offload_device_count_result = 0;
    }
  }
  else{
    String topic_string = String(topic);
    int first_backslash = topic_string.indexOf("/");
    int second_backslash = topic_string.indexOf("/", first_backslash+1);
    String device_id = getSubstring(topic_string, first_backslash+1, second_backslash);
    String device_topic = getSubstring(topic_string, second_backslash+1, topic_string.length());

    // Aggiorno le informazioni del client
    bool subscribe_status = SubscribeToCliensArray(device_id);
    if(subscribe_status){
      // se il topic è del tipo clients/dev1/status
      if(device_topic.equals("status")){
        if(formatted_payload.equals("1"))
          updateDeviceState(true,device_id);
        else
          updateDeviceState(false, device_id);
      }
      // se il topic è del tipo clients/dev1/capability
      if(device_topic.equals("capability")){
        doc.clear();
        DeserializationError error = deserializeJson(doc, payload, len);

        if (error) {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
          return;
        }

        int prime_num = doc["prime_num"]; 
        int word_count = doc["word_count"]; 
        int vect_mult = doc["vect_mult"]; 
        updateDeviceCapability(prime_num, word_count, vect_mult, device_id);
      }
    }
  }
  
  //printClientsArray();
}
