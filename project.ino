#include <ESP8266WebServer.h>
#include <Wire.h>
#include <MAX3010x.h>
#include <Adafruit_SSD1306.h>
#include "filters.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi Configuration
const char* ssid = "APW";  // Enter SSID here
const char* password = "wap@2004";  // Enter Password here

// Sensor configuration
MAX30105 sensor;
const auto kSamplingRate = sensor.SAMPLING_RATE_400SPS;
const float kSamplingFrequency = 400.0;

// Thresholds
const unsigned long kFingerThreshold = 10000;
const unsigned int kFingerCooldownMs = 500;
const float kEdgeThreshold = -2000.0;

// Filters
const float kLowPassCutoff = 5.0;
const float kHighPassCutoff = 0.5;

// HRV and Stress Calculation
const int kHRVWindowSize = 10;
const int kStressUpdateInterval = 30000;

// Filter Instances
LowPassFilter low_pass_filter_red(kLowPassCutoff, kSamplingFrequency);
LowPassFilter low_pass_filter_ir(kLowPassCutoff, kSamplingFrequency);
HighPassFilter high_pass_filter(kHighPassCutoff, kSamplingFrequency);
Differentiator differentiator(kSamplingFrequency);

// Statistics
MinMaxAvgStatistic stat_red;
MinMaxAvgStatistic stat_ir;

// SpO2 calibration
float kSpO2_A = 1.5958422;
float kSpO2_B = -34.6596622;
float kSpO2_C = 112.6898759;

// Measurement variables
float current_bpm = 0;
float current_spo2 = 0;
float current_rmssd = 0;
float stress_level = 0;
bool finger_detected = false;
unsigned long finger_timestamp = 0;

// HRV calculation buffers
long rr_intervals[kHRVWindowSize] = {0};
int rr_index = 0;
bool rr_window_filled = false;
unsigned long last_stress_update = 0;

// Moving average buffers
const int kAveragingSamples = 5;
float bpm_buffer[kAveragingSamples] = {0};
int bpm_index = 0;
float spo2_buffer[kAveragingSamples] = {0};
int spo2_index = 0;

// Heartbeat detection variables
float last_diff = NAN;
bool crossed = false;
unsigned long crossed_time = 0;
unsigned long last_heartbeat = 0;

// Web server
ESP8266WebServer server(80);
unsigned long last_server_update = 0;
const unsigned long server_update_interval = 1000;

void setup() {
  Serial.begin(115200);
  
  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("OLED failed"));
    while(1);
  }

  // Initialize sensor
  if(!sensor.begin() || !sensor.setSamplingRate(kSamplingRate)) {
    Serial.println("Sensor failed");  
    while(1);
  }

  // Initialize WiFi
  Serial.println("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected..!");
  Serial.print("Got IP: ");  Serial.println(WiFi.localIP());
  
  // Initialize web server
  server.on("/", handle_OnConnect);
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");

  // Initialize display
  display.clearDisplay();
  initDisplayLayout();
}

void loop() {
  server.handleClient();
  
  auto sample = sensor.readSample(1000);
  float current_value_red = sample.red;
  float current_value_ir = sample.ir;
  
  // Finger detection
  if(sample.red > kFingerThreshold) {
    if(millis() - finger_timestamp > kFingerCooldownMs) {
      finger_detected = true;
    }
  } else {
    resetMeasurementState();
    finger_detected = false;
    finger_timestamp = millis();
  }

  if(finger_detected) {
    processSensorData(current_value_red, current_value_ir);
    updateDisplay();
    
    // Update web server periodically
    if(millis() - last_server_update > server_update_interval) {
      last_server_update = millis();
    }
  } else {
    showNoFingerMessage();
  }
}

void processSensorData(float current_value_red, float current_value_ir) {
  // Filter and process sensor data
  current_value_red = low_pass_filter_red.process(current_value_red);
  current_value_ir = low_pass_filter_ir.process(current_value_ir);

  stat_red.process(current_value_red);
  stat_ir.process(current_value_ir);

  // Heartbeat detection
  float current_value = high_pass_filter.process(current_value_red);
  float current_diff = differentiator.process(current_value);

  if(!isnan(current_diff) && !isnan(last_diff)) {
    if(last_diff > 0 && current_diff < 0) {
      crossed = true;
      crossed_time = millis();
    }
    
    if(current_diff > 0) {
      crossed = false;
    }

    if(crossed && current_diff < kEdgeThreshold) {
      if(last_heartbeat != 0 && crossed_time - last_heartbeat > 300) {
        // Calculate current measurements
        current_bpm = 60000.0/(crossed_time - last_heartbeat);
        float rred = (stat_red.maximum()-stat_red.minimum())/stat_red.average();
        float rir = (stat_ir.maximum()-stat_ir.minimum())/stat_ir.average();
        current_spo2 = kSpO2_A * (rred/rir) * (rred/rir) + kSpO2_B * (rred/rir) + kSpO2_C;
        
        // Update moving averages
        bpm_buffer[bpm_index] = current_bpm;
        bpm_index = (bpm_index + 1) % kAveragingSamples;
        spo2_buffer[spo2_index] = current_spo2;
        spo2_index = (spo2_index + 1) % kAveragingSamples;
        
        // Store RR interval for HRV
        long rr_interval = crossed_time - last_heartbeat;
        rr_intervals[rr_index] = rr_interval;
        rr_index = (rr_index + 1) % kHRVWindowSize;
        if(rr_index == 0) rr_window_filled = true;
        
        // Calculate HRV and stress periodically
        if(millis() - last_stress_update > kStressUpdateInterval && rr_window_filled) {
          calculateHRVAndStress();
          last_stress_update = millis();
        }

        stat_red.reset();
        stat_ir.reset();
      }
      crossed = false;
      last_heartbeat = crossed_time;
    }
  }
  last_diff = current_diff;
}

void calculateHRVAndStress() {
  // Calculate RMSSD (root mean square of successive differences)
  float sum_sq_successive_diff = 0.0;
  for(int i = 1; i < kHRVWindowSize; i++) {
    float diff = rr_intervals[i] - rr_intervals[i-1];
    sum_sq_successive_diff += diff * diff;
  }
  current_rmssd = sqrt(sum_sq_successive_diff / (kHRVWindowSize - 1));
  
  // Calculate stress level (0-100) with inverse relationship to HRV
  stress_level = 100.0 - constrain(mapFloat(current_rmssd, 10, 100, 0.0, 100.0), 0.0, 100.0);
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float calculateAverage(float* buffer, int size) {
  float sum = 0;
  int count = 0;
  for(int i = 0; i < size; i++) {
    if(buffer[i] > 0) {
      sum += buffer[i];
      count++;
    }
  }
  return count > 0 ? sum / count : 0;
}

void initDisplayLayout() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(F(" HR   SpO2  Stress"));
  display.drawLine(0, 10, SCREEN_WIDTH, 10, WHITE);
  display.display();
}

void updateDisplay() {
  static unsigned long last_update = 0;
  if(millis() - last_update < 200) return;
  last_update = millis();

  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println(F(" HR   SpO2  Stress"));
  display.drawLine(0, 10, SCREEN_WIDTH, 10, WHITE);
  
  // Values
  display.setTextSize(2);
  
  // Heart Rate
  display.setCursor(5, 15);
  float avg_bpm = calculateAverage(bpm_buffer, kAveragingSamples);
  if(avg_bpm >= 30 && avg_bpm <= 250) {
    display.print((int)avg_bpm);
  } else {
    display.print("--");
  }
  
  // SpO2
  display.setCursor(50, 15);
  float avg_spo2 = calculateAverage(spo2_buffer, kAveragingSamples);
  if(avg_spo2 >= 70 && avg_spo2 <= 100) {
    display.print((int)avg_spo2);
    display.print("%");
  } else {
    display.print("--");
  }
  
  // Stress
  display.setCursor(95, 15);
  if(stress_level > 0) {
    display.print((int)stress_level);
    display.print("%");
  } else {
    display.print("--");
  }
  
  // Additional info line
  display.setTextSize(1);
  display.setCursor(0, 35);
  if(stress_level > 0) {
    display.print("HRV:");
    display.print(current_rmssd, 1);
    display.print("ms ");
    
    if(stress_level < 40) display.print("(Low)");
    else if(stress_level < 70) display.print("(Mod)");
    else display.print("(High)");
  }
  
  display.display();
}

void showNoFingerMessage() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(20, 25);
  display.print("No Finger");
  display.display();
}

void resetMeasurementState() {
  memset(rr_intervals, 0, sizeof(rr_intervals));
  memset(bpm_buffer, 0, sizeof(bpm_buffer));
  memset(spo2_buffer, 0, sizeof(spo2_buffer));
  rr_index = 0;
  bpm_index = 0;
  spo2_index = 0;
  rr_window_filled = false;
  current_bpm = 0;
  current_spo2 = 0;
  current_rmssd = 0;
  stress_level = 0;
  stat_red.reset();
  stat_ir.reset();
  last_diff = NAN;
  crossed = false;
  last_heartbeat = 0;
}

void handle_OnConnect() {
  float avg_bpm = calculateAverage(bpm_buffer, kAveragingSamples);
  float avg_spo2 = calculateAverage(spo2_buffer, kAveragingSamples);
  
  server.send(200, "text/html", SendHTML(avg_bpm, avg_spo2, stress_level));
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

String SendHTML(float BPM, float SpO2, float Stress) {
  String ptr = "<!DOCTYPE html>";
  ptr += "<html>";
  ptr += "<head>";
  ptr += "<title>Remote Stress Detection Using HRV</title>";
  ptr += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  ptr += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.7.2/css/all.min.css'>";
  ptr += "<link rel='stylesheet' type='text/css' href='styles.css'>";
  ptr += "<style>";
  ptr += "body { background-color: #fff; font-family: sans-serif; color: #333333; font: 14px Helvetica, sans-serif box-sizing: border-box;}";
  ptr += "#page { margin: 20px; background-color: #fff;}";
  ptr += ".container { height: inherit; padding-bottom: 20px;}";
  ptr += ".header { padding: 20px;}";
  ptr += ".header h1 { padding-bottom: 0.3em; color: #008080; font-size: 45px; font-weight: bold; font-family: Garmond, 'sans-serif'; text-align: center;}";
  ptr += "h2 { padding-bottom: 0.2em; border-bottom: 1px solid #eee; margin: 2px; text-align: left;}";
  ptr += ".header h3 { font-weight: bold; font-family: Arial, 'sans-serif'; font-size: 17px; color: #b6b6b6; text-align: center;}";
  ptr += ".box-full { padding: 20px; border 1px solid #ddd; border-radius: 1em 1em 1em 1em; box-shadow: 1px 7px 7px 1px rgba(0,0,0,0.4); background: #fff; margin: 20px; width: 300px;}";
  ptr += "@media (max-width: 494px) { #page { width: inherit; margin: 5px auto; } #content { padding: 1px;} .box-full { margin: 8px 8px 12px 8px; padding: 10px; width: inherit;; float: none; } }";
  ptr += "@media (min-width: 494px) and (max-width: 980px) { #page { width: 465px; margin 0 auto; } .box-full { width: 380px; } }";
  ptr += "@media (min-width: 980px) { #page { width: 930px; margin: auto; } }";
  ptr += ".sensor { margin: 12px 0px; font-size: 2.5rem;}";
  ptr += ".sensor-labels { font-size: 1rem; vertical-align: middle; padding-bottom: 15px;}";
  ptr += ".units { font-size: 1.2rem;}";
  ptr += "hr { height: 1px; color: #eee; background-color: #eee; border: none;}";
  ptr += "</style>";

  //Ajax Code Start
  ptr += "<script>\n";
  ptr += "setInterval(loadDoc,1000);\n";
  ptr += "function loadDoc() {\n";
  ptr += "var xhttp = new XMLHttpRequest();\n";
  ptr += "xhttp.onreadystatechange = function() {\n";
  ptr += "if (this.readyState == 4 && this.status == 200) {\n";
  ptr += "document.body.innerHTML =this.responseText}\n";
  ptr += "};\n";
  ptr += "xhttp.open(\"GET\", \"/\", true);\n";
  ptr += "xhttp.send();\n";
  ptr += "}\n";
  ptr += "</script>\n";
  //Ajax Code END

  ptr += "</head>";
  ptr += "<body>";
  ptr += "<div id='page'>";
  ptr += "<div class='header'>";
  ptr += "<h1>Remote Stress Detection Using HRV</h1>";
  ptr += "<h3>Heart Rate, SpO2 and Stress Level</h3>";
  ptr += "</div>";
  ptr += "<div id='content' align='center'>";
  ptr += "<div class='box-full' align='left'>";
  ptr += "<h2>Sensor Readings</h2>";
  ptr += "<div class='sensors-container'>";

  // For Heart Rate
  ptr += "<p class='sensor'>";
  ptr += "<i class='fas fa-heartbeat' style='color:#cc3300'></i>";
  ptr += "<span class='sensor-labels'> Heart Rate </span>";
  if(BPM >= 30 && BPM <= 250) {
    ptr += (int)BPM;
  } else {
    ptr += "--";
  }
  ptr += "<sup class='units'>BPM</sup>";
  ptr += "</p>";
  ptr += "<hr>";

  // For Sp02
  ptr += "<p class='sensor'>";
  ptr += "<i class='fas fa-burn' style='color:#f7347a'></i>";
  ptr += "<span class='sensor-labels'> SpO2 </span>";
  if(SpO2 >= 70 && SpO2 <= 100) {
    ptr += (int)SpO2;
  } else {
    ptr += "--";
  }
  ptr += "<sup class='units'>%</sup>";
  ptr += "</p>";
  ptr += "<hr>";

  // For Stress Level
  ptr += "<p class='sensor'>";
  ptr += "<i class='fas fa-brain' style='color:#6a5acd'></i>";
  ptr += "<span class='sensor-labels'> Stress Level </span>";
  if(Stress > 0) {
    ptr += (int)Stress;
  } else {
    ptr += "--";
  }
  ptr += "<sup class='units'>%</sup>";
  ptr += "</p>";

  ptr += "</div>";
  ptr += "</div>";
  ptr += "</div>";
  ptr += "</div>";
  ptr += "</div>";
  ptr += "</body>";
  ptr += "</html>";
  return ptr;
}