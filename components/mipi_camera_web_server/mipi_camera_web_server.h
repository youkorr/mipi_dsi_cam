#pragma once

#include "esphome/core/component.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP32

#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome {
namespace mipi_camera_web_server {

class MipiCameraWebServer : public Component, public web_server_base::WebServerBaseHandler {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_camera(mipi_dsi_cam::MipiDsiCam *camera) { this->camera_ = camera; }
  void set_mode(bool stream) { this->stream_mode_ = stream; }

  // Implémentation de WebServerBaseHandler
  void handle_request(AsyncWebServerRequest *request) override;
  bool canHandle(AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() override { return false; }

 protected:
  mipi_dsi_cam::MipiDsiCam *camera_{nullptr};
  bool stream_mode_{true};
  
  // Buffer pour JPEG
  uint8_t *jpeg_buffer_{nullptr};
  size_t jpeg_buffer_size_{150 * 1024};  // 150KB
  SemaphoreHandle_t jpeg_mutex_{nullptr};
  
  void handle_stream_(AsyncWebServerRequest *request);
  void handle_snapshot_(AsyncWebServerRequest *request);
  
  bool encode_jpeg_(const uint8_t *rgb565_data, size_t width, size_t height, 
                    uint8_t **jpeg_out, size_t *jpeg_size, int quality = 10);
  
  static void rgb565_to_rgb888_(const uint8_t *rgb565, uint8_t *rgb888, size_t pixels);
};

}  // namespace mipi_camera_web_server
}  // namespace esphome

#endif  // USE_ESP32
