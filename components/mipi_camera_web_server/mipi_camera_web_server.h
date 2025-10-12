#pragma once

#include "esphome/core/component.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP32_VARIANT_ESP32P4
#include <esp_http_server.h>
#endif

namespace esphome {
namespace mipi_camera_web_server {

class MipiCameraWebServer : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  void set_camera(mipi_dsi_cam::MipiDsiCam *camera) { this->camera_ = camera; }
  void set_port(uint16_t port) { this->port_ = port; }

 protected:
  mipi_dsi_cam::MipiDsiCam *camera_{nullptr};
  uint16_t port_{80};

#ifdef USE_ESP32_VARIANT_ESP32P4
  httpd_handle_t server_{nullptr};
  
  // Buffer pour JPEG
  uint8_t *jpeg_buffer_{nullptr};
  size_t jpeg_buffer_size_{150 * 1024};
  SemaphoreHandle_t jpeg_mutex_{nullptr};
  
  // Handlers HTTP
  static esp_err_t index_handler_(httpd_req_t *req);
  static esp_err_t stream_handler_(httpd_req_t *req);
  static esp_err_t snapshot_handler_(httpd_req_t *req);
  static esp_err_t control_handler_(httpd_req_t *req);
  
  bool encode_jpeg_(const uint8_t *rgb565_data, size_t width, size_t height, 
                    uint8_t **jpeg_out, size_t *jpeg_size, int quality = 12);
  
  static void rgb565_to_rgb888_(const uint8_t *rgb565, uint8_t *rgb888, size_t pixels);
#endif
};

}  // namespace mipi_camera_web_server
}  // namespace esphome#pragma once
