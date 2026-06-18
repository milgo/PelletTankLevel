#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SD.h>
#include <SPI.h>

#define ENABLE_PIN 2
#define READ_PIN 3
#define SYNC_CLOCK_FAILED_TIMEOUT 5 //should be ~300s
#define MIDNIGHT_INTERVAL 10//86400L
#define NUM_OF_SAMPLES 11
#define BYTES_TO_READ 240
#define SD_CHIP_SELECT 4
//#define SYNC_CLOCK_EXPIRE_TIME 3000 
//#define NEXT_MEASUREMENT_TIME 1000 

// NTP Server settings
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

// MAC address for controller, UDP setup
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
unsigned int localPort = 8888;

EthernetUDP Udp;
IPAddress ip(192, 168, 1, 80); // IP address, may need to change depending on network
IPAddress timeServerIp(192, 168, 1, 1);

unsigned long lastTimeMs = 0;
unsigned long timeMs = 0;
unsigned long measurementTimeSec = 0;
//unsigned long measurementTimeSec = 0;
unsigned long syncClockTimeSec = 0;
unsigned long syncClockFailedTimeSec = 0;
unsigned long midnightTomorrow = 0;
unsigned long epoch = 0;

uint16_t mArray[NUM_OF_SAMPLES];

struct SensorData {
  uint32_t timestamp;
  uint16_t measurement;
};

void bubbleSort(int a[], int size){
  for(int i=0;i<(size-1);i++){
    for(int j=0;j<(size-(i+1));j++){
      if(a[j]>a[j+1]){
        int t = a[j];
        a[j] = a[j+1];
        a[j+1] = t;
      }
    }
  }
}

unsigned int getMeasurement(){
  unsigned int readByte = 0;
  digitalWrite(ENABLE_PIN, HIGH);
  for (int i=0; i<16; i++) 
    if(pulseIn(READ_PIN, LOW, 1000) <= 20) 
      readByte = bitSet(readByte, i);
  digitalWrite(ENABLE_PIN, LOW);
  delay(200);
  return readByte;
}


unsigned long getTimestampFromTimeServer(IPAddress timeServerIp){

  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011; 
  Udp.beginPacket(timeServerIp, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();

  delay(1000);
  
  if (Udp.parsePacket()) {
    Udp.read(packetBuffer, NTP_PACKET_SIZE);
    
    // Extract timestamp (seconds since Jan 1 1900)
    unsigned long secs = word(packetBuffer[40], packetBuffer[41]) << 16 | word(packetBuffer[42], packetBuffer[43]);
    // Convert to Unix epoch (seconds since Jan 1 1970)
    unsigned long epoch = secs - 2208988800UL;
    
    Serial.print("Unix time: ");
    Serial.println(epoch);
    return epoch;
  }
  return 0;
}

void setup() {
  timeMs = 0;
  lastTimeMs = 0;
  syncClockFailedTimeSec = 0;
  measurementTimeSec = 0;
  midnightTomorrow = MIDNIGHT_INTERVAL;
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(READ_PIN, INPUT);
  Serial.begin(9600);
  Ethernet.begin(mac, ip);
  Udp.begin(localPort);

  if (!SD.begin(4)) {
    Serial.println("SD Error");
    while (1);
  }

  //SD test - read 10 last measurements
  File fileRead = SD.open("data.log", FILE_READ);
  if (fileRead) {
    uint32_t fileSize = fileRead.size();
    if (fileSize >= BYTES_TO_READ) {
      uint32_t targetPosition = fileSize - BYTES_TO_READ;

      if (fileRead.seek(targetPosition)) {
        SensorData readData;
        for(int i=0; i<40; i++){
          if (fileRead.read((uint8_t *)&readData, sizeof(readData)) == sizeof(readData)) {
            Serial.println(String(readData.timestamp) + ": " + String(readData.measurement));
          }
        }
      }
    }
    fileRead.close();
  }
}

void loop() {

  timeMs = millis(); // Pobranie aktualnego czasu
  if(timeMs - lastTimeMs > 1000){
    measurementTimeSec++;
    Serial.print("Measuring count: ");
    Serial.println(measurementTimeSec);
    syncClockTimeSec++;
    lastTimeMs = timeMs;
  }

  //time for new measurement
  if(epoch + measurementTimeSec >= midnightTomorrow){
    Serial.println("Measuring level...");

    for(int i=0; i<NUM_OF_SAMPLES; i++)
      mArray[i] = getMeasurement();

    bubbleSort(mArray, NUM_OF_SAMPLES);
    
    for(int i=0; i<NUM_OF_SAMPLES; i++){
      Serial.print(mArray[i]);
      Serial.print(",");
    }
    Serial.println("");

    SensorData sensorData;
    sensorData.measurement = mArray[NUM_OF_SAMPLES/2];
    sensorData.timestamp = epoch + measurementTimeSec; //what if epoch == 0?
    Serial.print("Level: ");
    Serial.println(sensorData.measurement);

    File dataFile = SD.open("data.log", FILE_WRITE);
    if (dataFile) {
      dataFile.write((const uint8_t *)&sensorData, sizeof(sensorData));
      dataFile.close();     
    }

    epoch = 0;
    Serial.println("Time sync expired...");
    measurementTimeSec = 0;
  }
  
  //sync time 
  if(epoch == 0 && syncClockTimeSec - syncClockFailedTimeSec > SYNC_CLOCK_FAILED_TIMEOUT){
    Serial.println("Time syncing...");
    epoch = getTimestampFromTimeServer(timeServerIp);
    //measurementTimeSec = 0;
    if(epoch == 0){
      syncClockFailedTimeSec = syncClockTimeSec;
      midnightTomorrow = MIDNIGHT_INTERVAL;
      Serial.print("Time not synced... Midnight tomorow is: ");
      Serial.println(midnightTomorrow);
    }else{
      midnightTomorrow = (epoch - (epoch % MIDNIGHT_INTERVAL) + MIDNIGHT_INTERVAL);
      Serial.print("Time synced... Midnight tomorrow is: ");
      Serial.println(midnightTomorrow);
      measurementTimeSec = 0;
    }
  }
}

