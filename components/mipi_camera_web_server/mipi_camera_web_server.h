#pragma once

#include "esphome/core/component.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP32
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#endif

namespace esphome {
namespace mipi_camera_web_server {

class MipiCameraWebServer : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_camera(mipi_dsi_cam::MipiDsiCam *camera) { this->camera_ = camera; }
  void set_port(uint16_t port) { this->port_ = port; }

  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

 protected:
  mipi_dsi_cam::MipiDsiCam *camera_{nullptr};
  uint16_t port_{8080};

#ifdef USE_ESP32
  AsyncWebServer *server_{nullptr};
  
  // Buffer pour JPEG
  uint8_t *jpeg_buffer_{nullptr};
  size_t jpeg_buffer_size_{0};
  
  void handle_index_(AsyncWebServerRequest *request);
  void handle_stream_(AsyncWebServerRequest *request);
  void handle_snapshot_(AsyncWebServerRequest *request);
  void handle_control_(AsyncWebServerRequest *request);
  
  bool encode_jpeg_(const uint8_t *rgb565_data, size_t width, size_t height, 
                    uint8_t **jpeg_out, size_t *jpeg_size);
  void rgb565_to_rgb888_(const uint8_t *rgb565, uint8_t *rgb888, size_t pixels);
#endif
};

}  // namespace mipi_camera_web_server
}  // namespace esphome
