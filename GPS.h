// Copyright (C) 2024, Mark Qvist
// Copyright (C) 2026, GPS support contributed by GlassOnTin

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifndef GPS_H
#define GPS_H

#if HAS_GPS == true

#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

TinyGPSPlus gps_parser;
HardwareSerial gps_serial(1);

bool gps_ready = false;
bool gps_has_fix = false;
uint8_t gps_sats = 0;
double gps_lat = 0.0;
double gps_lon = 0.0;
double gps_alt = 0.0;
double gps_speed = 0.0;
double gps_hdop = 0.0;
uint32_t gps_last_update = 0;

#define GPS_REPORT_INTERVAL 5000
uint32_t gps_last_report = 0;
void kiss_indicate_stat_gps();

void gps_power_on() {
  #if defined(PIN_GPS_EN)
    pinMode(PIN_GPS_EN, OUTPUT);
    #if GPS_EN_ACTIVE == LOW
      digitalWrite(PIN_GPS_EN, LOW);
    #else
      digitalWrite(PIN_GPS_EN, HIGH);
    #endif
  #endif

  #if defined(PIN_GPS_RST)
    pinMode(PIN_GPS_RST, OUTPUT);
    digitalWrite(PIN_GPS_RST, HIGH);
  #endif

  #if defined(PIN_GPS_STANDBY)
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    digitalWrite(PIN_GPS_STANDBY, HIGH);
  #endif
}

void gps_power_off() {
  #if defined(PIN_GPS_EN)
    #if GPS_EN_ACTIVE == LOW
      digitalWrite(PIN_GPS_EN, HIGH);
    #else
      digitalWrite(PIN_GPS_EN, LOW);
    #endif
  #endif
}

void gps_setup() {
  gps_power_on();
  delay(100);
  gps_serial.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);
  gps_ready = true;
}

void gps_update() {
  if (!gps_ready) return;

  while (gps_serial.available() > 0) {
    gps_parser.encode(gps_serial.read());
  }

  if (gps_parser.location.isUpdated()) {
    gps_has_fix = gps_parser.location.isValid();
    if (gps_has_fix) {
      gps_lat = gps_parser.location.lat();
      gps_lon = gps_parser.location.lng();
      gps_last_update = millis();
    }
  }

  if (gps_parser.altitude.isUpdated() && gps_parser.altitude.isValid()) {
    gps_alt = gps_parser.altitude.meters();
  }

  if (gps_parser.speed.isUpdated() && gps_parser.speed.isValid()) {
    gps_speed = gps_parser.speed.kmph();
  }

  if (gps_parser.satellites.isUpdated()) {
    gps_sats = gps_parser.satellites.value();
  }

  if (gps_parser.hdop.isUpdated()) {
    gps_hdop = gps_parser.hdop.hdop();
  }

  // Mark fix as stale after 10 seconds without update
  if (gps_has_fix && (millis() - gps_last_update > 10000)) {
    gps_has_fix = false;
  }

  // Periodically push GPS telemetry to host
  if (millis() - gps_last_report >= GPS_REPORT_INTERVAL) {
    gps_last_report = millis();
    kiss_indicate_stat_gps();
  }
}

void gps_teardown() {
  gps_serial.end();
  gps_power_off();
  gps_ready = false;
}

#endif
#endif
