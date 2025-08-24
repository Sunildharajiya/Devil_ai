#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include "mbedtls/base64.h"

// ========= USER CONFIG =========
const char* WIFI_SSID = "Sunil dharajiya";
const char* WIFI_PASS = "635276@sunil";
const char* DEEPGRAM_API_KEY = "e33c7f043bd61cc3b4b5afe7105bb0f2d771c19a";
const char* GEMINI_API_KEY   = "AIzaSyBE5tF3tI-yIY3dSG6VQCQsgPrcGGC9N8E";
const char* GOOGLE_TTS_KEY   = "AIzaSyBE5tF3tI-yIY3dSG6VQCQsgPrcGGC9N8E";

// ========= PINS =========
#define LED_WIFI   2    // WiFi indicator
#define LED_LISTEN 4    // Listening indicator
#define EXT_PIN    5    // external pin control
#define BTN_RECORD 28   // record button

// 🎤 I2S Microphone pins
#define I2S_WS  25
#define I2S_SD  26
#define I2S_SCK 27

// 🔊 I2S Speaker (MAX98357A)
#define I2S_BCLK 14
#define I2S_LRC  15
#define I2S_DOUT 32

// ========= GLOBAL =========
WiFiClientSecure client;

// ========= FUNCTION PROTOTYPES =========
std::vector<int16_t> recordAudio(int seconds);
String sendToDeepgram(std::vector<int16_t>& audio);
String sendToGemini(String transcript);
void speakText(String text);
void playPCM(std::vector<int16_t>& pcm);

// ========= SETUP =========
void setup() {
  Serial.begin(115200);

  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_LISTEN, OUTPUT);
  pinMode(EXT_PIN, OUTPUT);
  pinMode(BTN_RECORD, INPUT_PULLUP);
  
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_LISTEN, LOW);
  digitalWrite(EXT_PIN, LOW);

  connectWiFi();
  setupI2SMic();
  setupI2SSpeaker();
}

// ========= LOOP =========
void loop() {
  if (digitalRead(BTN_RECORD) == LOW) {
    Serial.println("Start recording 5s...");
    digitalWrite(LED_LISTEN, HIGH);

    // Capture 5 sec audio buffer
    std::vector<int16_t> audioData = recordAudio(5);

    digitalWrite(LED_LISTEN, LOW);
    Serial.println("Recording done");

    // Send to Deepgram
    String transcript = sendToDeepgram(audioData);
    Serial.println("Transcript: " + transcript);

    if (transcript == "") {
      delay(5000);
      return;
    }

    // Send to Gemini
    String reply = sendToGemini(transcript);
    Serial.println("AI Reply: " + reply);

    // Ext control
    if (reply.indexOf("external pin on") >= 0) {
      digitalWrite(EXT_PIN, HIGH);
    } else if (reply.indexOf("external pin off") >= 0) {
      digitalWrite(EXT_PIN, LOW);
    }

    // Speak reply
    speakText(reply);

    delay(10000); // wait before next round
  }
}

// ========= WIFI =========
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  digitalWrite(LED_WIFI, HIGH);
}

// ========= MIC =========
void setupI2SMic() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

std::vector<int16_t> recordAudio(int seconds) {
  std::vector<int16_t> samples;
  size_t bytesRead;
  int16_t buffer[512];

  uint32_t totalSamples = 16000 * seconds;

  for (uint32_t i = 0; i < totalSamples / 512; i++) {
    i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
    samples.insert(samples.end(), buffer, buffer + bytesRead / 2);
  }
  return samples;
}

// ========= SPEAKER =========
void setupI2SSpeaker() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = -1
  };

  i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin_config);
}

void playPCM(std::vector<int16_t>& pcm) {
  size_t bytesWritten;
  i2s_write(I2S_NUM_1, pcm.data(), pcm.size() * 2, &bytesWritten, portMAX_DELAY);
}

// ========= API CALLS =========
String sendToDeepgram(std::vector<int16_t>& audio) {
  HTTPClient http;
  http.begin("https://api.deepgram.com/v1/listen");
  http.addHeader("Authorization", "Token " + String(DEEPGRAM_API_KEY));
  http.addHeader("Content-Type", "audio/wav");

  // Add WAV header
  uint32_t dataSize = audio.size() * sizeof(int16_t);
  uint32_t fileSize = 36 + dataSize;
  uint8_t wavHeader[44] = {
    'R','I','F','F',
    (uint8_t)(fileSize & 0xff), (uint8_t)((fileSize>>8)&0xff), (uint8_t)((fileSize>>16)&0xff), (uint8_t)((fileSize>>24)&0xff),
    'W','A','V','E',
    'f','m','t',' ',
    16,0,0,0,
    1,0,
    1,0,
    0x80,0x3e,0x00,0x00,
    0x00,0x7d,0x00,0x00,
    2,0,
    16,0,
    'd','a','t','a',
    (uint8_t)(dataSize & 0xff), (uint8_t)((dataSize>>8)&0xff), (uint8_t)((dataSize>>16)&0xff), (uint8_t)((dataSize>>24)&0xff)
  };

  // Combine header + audio
  std::vector<uint8_t> wav;
  wav.insert(wav.end(), wavHeader, wavHeader+44);
  wav.insert(wav.end(), (uint8_t*)audio.data(), (uint8_t*)audio.data() + dataSize);

  int httpCode = http.POST(wav.data(), wav.size());

  String transcript = "";
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Deepgram: " + payload);

    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);
    transcript = doc["results"]["channels"][0]["alternatives"][0]["transcript"].as<String>();
  }
  http.end();
  return transcript;
}

String sendToGemini(String transcript) {
  HTTPClient http;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + String(GEMINI_API_KEY);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // 🔥 Include systemInstruction
  String body = "{"
    "\"systemInstruction\": {"
      "\"parts\": [{\"text\": \"You are an AI voice assistant named Devil. You are created by the team of three members Suni Dharajiya, Karmarajsinh Dod, and Siddhrajsinh Makwana.\"}]"
    "},"
    "\"contents\": [{"
      "\"parts\": [{\"text\": \"" + transcript + "\"}]"
    "}]"
  "}";

  int httpCode = http.POST(body);

  String reply = "";
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Gemini: " + payload);

    DynamicJsonDocument doc(8192);
    deserializeJson(doc, payload);
    reply = doc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
  } else {
    Serial.printf("Gemini request failed, code: %d\n", httpCode);
  }
  http.end();
  return reply;
}

void speakText(String text) {
  HTTPClient http;
  String url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + String(GOOGLE_TTS_KEY);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String body = "{\"input\":{\"text\":\"" + text + "\"},\"voice\":{\"languageCode\":\"en-IN\"},\"audioConfig\":{\"audioEncoding\":\"LINEAR16\"}}";
  int httpCode = http.POST(body);

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("TTS: " + payload);

    DynamicJsonDocument doc(16384);
    deserializeJson(doc, payload);
    String audioBase64 = doc["audioContent"];
    
    // Decode Base64 → PCM
    size_t decodedLen = 0;
    uint8_t *decodedAudio = (uint8_t*)malloc(audioBase64.length());
    mbedtls_base64_decode(decodedAudio, audioBase64.length(), &decodedLen,
                          (const unsigned char*)audioBase64.c_str(), audioBase64.length());

    std::vector<int16_t> pcm(decodedLen/2);
    memcpy(pcm.data(), decodedAudio, decodedLen);

    playPCM(pcm);
    free(decodedAudio);
  }
  http.end();
}
