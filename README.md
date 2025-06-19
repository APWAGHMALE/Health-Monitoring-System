# Health-Monitoring-System
# Real-Time Stress Detection using ESP8266 & MAX30102
A compact, wireless, and affordable system to monitor stress levels in real-time using Heart Rate Variability (HRV), built with ESP8266 and the MAX30102 PPG sensor.

## 📑 Table of Contents
- [Features](#features)
- [Hardware Used](#hardware-used)
- [System Overview](#system-overview)
- [How It Works](#how-it-works)
- [Results](#results)
- [Installation / Getting Started](#installation--getting-started)
- [Future Improvements](#future-improvements)
- [References](#references)
- [License](#license)

## 🔧 Features
- Real-time heart rate and SpO₂ monitoring
- HRV calculation (SDNN, RMSSD) for stress detection
- OLED display output
- Wi-Fi connectivity for remote data monitoring
- Low-power design, suitable for wearable use

## 📐 System Overview

The system consists of:
- ESP8266 microcontroller
- MAX30102 sensor
- OLED Display
- 3.3V regulated power supply

![System Block Diagram](hardware/block_diagram.png)

## 🖥️ How It Works

1. MAX30102 collects heart rate and SpO₂ data via I2C.
2. ESP8266 processes the data to calculate HRV metrics.
3. Based on HRV (e.g., SDNN, RMSSD), it classifies stress levels.
4. OLED display shows the result. Wi-Fi can optionally transmit data to a mobile or web interface.

## 📊 Results

| Subject | Pulse Oximeter BPM | Project BPM | Pulse Oximeter SpO₂ | Project SpO₂ |
|---------|--------------------|-------------|----------------------|--------------|
| Person 1 | 76                | 78          | 98%                 | 97%          |
| Person 2 | 82                | 84          | 99%                 | 95%          |
| ...     | ...                | ...         | ...                 | ...          |

> Results show a small deviation, acceptable for non-clinical monitoring.

## 🚀 Installation / Getting Started

1. Clone this repo:
   ```bash
   git clone https://github.com/yourusername/stress-detection-esp8266.git

## 📚 References

- Shaffer & Ginsberg – Overview of HRV Metrics
- Daud Muhajir et al. – Android App for HRV-based Stress Monitoring
- MAX30102 Datasheet
- Project Report (see `/docs/project_report.pdf`)
