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

// Raw NMEA capture for diagnostic forwarding
#define NMEA_BUF_SIZE 128
char nmea_gga_buf[NMEA_BUF_SIZE];
uint8_t nmea_gga_len = 0;
bool nmea_gga_ready = false;
bool nmea_is_gga = false;

// Ring buffer for all NMEA sentences (diagnostic dump)
#define NMEA_RING_SLOTS 16
char nmea_ring[NMEA_RING_SLOTS][NMEA_BUF_SIZE];
uint8_t nmea_ring_len[NMEA_RING_SLOTS];
uint8_t nmea_ring_head = 0;   // next write slot
uint8_t nmea_ring_count = 0;  // sentences since last report

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
void kiss_indicate_gps_nmea();

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
    // L76K reset: active LOW for >100ms triggers reset
    pinMode(PIN_GPS_RST, OUTPUT);
    digitalWrite(PIN_GPS_RST, LOW);
    delay(200);
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
  delay(1000);  // Allow L76K time to boot after reset
  // PIN_GPS_TX/RX named from ESP32 perspective:
  // PIN_GPS_RX (39) = ESP32 receives FROM GPS module
  // PIN_GPS_TX (38) = ESP32 transmits TO GPS module
  gps_serial.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  delay(250);

  // L76K init: force internal antenna (ceramic patch)
  gps_serial.print("$PCAS15,0*19\r\n");
  delay(250);
  // Full cold start — clears ephemeris/almanac, forces fresh search
  gps_serial.print("$PCAS10,3*1F\r\n");
  delay(2000);  // Cold start needs extra time
  // Enable GPS+GLONASS+BeiDou
  gps_serial.print("$PCAS04,7*1E\r\n");
  delay(250);
  // Output GGA, GSA, GSV, and RMC
  gps_serial.print("$PCAS03,1,0,1,1,1,0,0,0,0,0,,,0,0*02\r\n");
  delay(250);
  // Set navigation mode to Vehicle (better satellite tracking)
  gps_serial.print("$PCAS11,3*1E\r\n");
  delay(250);

  gps_ready = true;
}

void gps_update() {
  if (!gps_ready) return;

  while (gps_serial.available() > 0) {
    char c = gps_serial.read();
    gps_parser.encode(c);

    // Capture raw NMEA sentences for diagnostic forwarding
    {
      static char nmea_tmp[NMEA_BUF_SIZE];
      static uint8_t nmea_tmp_len = 0;

      if (c == '$') {
        nmea_tmp_len = 0;
      }
      if (nmea_tmp_len < NMEA_BUF_SIZE - 1) {
        nmea_tmp[nmea_tmp_len++] = c;
      }
      // On sentence end, store in ring buffer and check for GGA
      if (c == '\n' && nmea_tmp_len > 6) {
        // Store every sentence in ring buffer
        uint8_t slot = nmea_ring_head;
        memcpy(nmea_ring[slot], nmea_tmp, nmea_tmp_len);
        nmea_ring_len[slot] = nmea_tmp_len;
        nmea_ring_head = (nmea_ring_head + 1) % NMEA_RING_SLOTS;
        nmea_ring_count++;

        // Also keep GGA in dedicated buffer for stat report
        if (nmea_tmp[3] == 'G' && nmea_tmp[4] == 'G' && nmea_tmp[5] == 'A') {
          memcpy(nmea_gga_buf, nmea_tmp, nmea_tmp_len);
          nmea_gga_len = nmea_tmp_len;
          nmea_gga_buf[nmea_gga_len] = '\0';
          nmea_gga_ready = true;
        }
      }
    }
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

    // Dump all buffered NMEA sentences for diagnostics
    if (nmea_ring_count > 0) {
      uint8_t count = nmea_ring_count < NMEA_RING_SLOTS ? nmea_ring_count : NMEA_RING_SLOTS;
      uint8_t start = (nmea_ring_head + NMEA_RING_SLOTS - count) % NMEA_RING_SLOTS;
      for (uint8_t i = 0; i < count; i++) {
        uint8_t slot = (start + i) % NMEA_RING_SLOTS;
        // Send each sentence as a separate NMEA KISS frame
        nmea_gga_len = nmea_ring_len[slot];
        memcpy(nmea_gga_buf, nmea_ring[slot], nmea_gga_len);
        nmea_gga_buf[nmea_gga_len] = '\0';
        kiss_indicate_gps_nmea();
      }
      nmea_ring_count = 0;
    }
  }
}

void gps_teardown() {
  gps_serial.end();
  gps_power_off();
  gps_ready = false;
}

#endif
#endif
