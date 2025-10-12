#include "mipi_camera_web_server.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

#include <JPEGENC.h>

namespace esphome {
namespace mipi_camera_web_server {

static const char *const TAG = "mipi_camera_web_server";

void MipiCameraWebServer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MIPI Camera Web Server...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  // Créer mutex pour l'accès au buffer JPEG
  this->jpeg_mutex_ = xSemaphoreCreateMutex();
  if (this->jpeg_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create mutex");
    this->mark_failed();
    return;
  }

  // Allouer buffer JPEG en PSRAM
  this->jpeg_buffer_ = (uint8_t *) heap_caps_malloc(
    this->jpeg_buffer_size_, 
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG buffer (%u bytes)", this->jpeg_buffer_size_);
    this->mark_failed();
    return;
  }

  // Démarrer le streaming de la caméra
  if (!this->camera_->is_streaming()) {
    if (!this->camera_->start_streaming()) {
      ESP_LOGE(TAG, "Failed to start camera streaming");
      this->mark_failed();
      return;
    }
  }

  ESP_LOGI(TAG, "MIPI Camera Web Server initialized");
}

void MipiCameraWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI Camera Web Server:");
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->stream_mode_ ? "stream" : "snapshot");
  if (this->camera_) {
    ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", 
                  this->camera_->get_image_width(),
                  this->camera_->get_image_height());
  }
}

bool MipiCameraWebServer::canHandle(AsyncWebServerRequest *request) {
  if (request->method() != HTTP_GET)
    return false;

  // URLs gérées : /camera.jpg (snapshot) et /camera_stream.mjpg (stream)
  if (request->url() == "/camera.jpg")
    return true;
    
  if (this->stream_mode_ && request->url() == "/camera_stream.mjpg")
    return true;

  return false;
}

void MipiCameraWebServer::handle_request(AsyncWebServerRequest *request) {
  if (!this->camera_ || !this->camera_->is_streaming()) {
    request->send(503, "text/plain", "Camera not available");
    return;
  }

  if (request->url() == "/camera.jpg") {
    this->handle_snapshot_(request);
  } else if (request->url() == "/camera_stream.mjpg") {
    this->handle_stream_(request);
  } else {
    request->send(404);
  }
}

void MipiCameraWebServer::handle_snapshot_(AsyncWebServerRequest *request) {
  // Capturer une frame
  if (!this->camera_->capture_frame()) {
    request->send(503, "text/plain", "No frame available");
    return;
  }

  uint8_t *rgb565_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (rgb565_data == nullptr) {
    request->send(500, "text/plain", "Invalid frame data");
    return;
  }

  // Encoder en JPEG
  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;

  if (xSemaphoreTake(this->jpeg_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    request->send(503, "text/plain", "Server busy");
    return;
  }

  bool success = this->encode_jpeg_(rgb565_data, width, height, &jpeg_data, &jpeg_size, 15);
  
  if (!success || jpeg_size == 0) {
    xSemaphoreGive(this->jpeg_mutex_);
    request->send(500, "text/plain", "JPEG encoding failed");
    return;
  }

  // Copier les données JPEG pour la réponse
  uint8_t *jpeg_copy = (uint8_t *) malloc(jpeg_size);
  if (jpeg_copy == nullptr) {
    xSemaphoreGive(this->jpeg_mutex_);
    request->send(500, "text/plain", "Memory allocation failed");
    return;
  }
  memcpy(jpeg_copy, jpeg_data, jpeg_size);
  
  xSemaphoreGive(this->jpeg_mutex_);

  // Envoyer la réponse
  AsyncWebServerResponse *response = request->beginResponse(
    "image/jpeg", 
    jpeg_size,
    [jpeg_copy, jpeg_size](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      size_t remaining = jpeg_size - index;
      size_t to_send = (remaining < maxLen) ? remaining : maxLen;
      
      if (to_send > 0) {
        memcpy(buffer, jpeg_copy + index, to_send);
      }
      
      // Libérer la mémoire à la fin
      if (index + to_send >= jpeg_size) {
        free((void *) jpeg_copy);
      }
      
      return to_send;
    }
  );

  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
}

void MipiCameraWebServer::handle_stream_(AsyncWebServerRequest *request) {
  // Stream MJPEG multipart
  AsyncWebServerResponse *response = request->beginChunkedResponse(
    "multipart/x-mixed-replace; boundary=frame",
    [this](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      // Capturer une nouvelle frame
      if (!this->camera_->capture_frame()) {
        return 0;
      }

      uint8_t *rgb565_data = this->camera_->get_image_data();
      uint16_t width = this->camera_->get_image_width();
      uint16_t height = this->camera_->get_image_height();

      if (rgb565_data == nullptr) {
        return 0;
      }

      // Encoder en JPEG
      if (xSemaphoreTake(this->jpeg_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
      }

      uint8_t *jpeg_data = nullptr;
      size_t jpeg_size = 0;

      bool success = this->encode_jpeg_(rgb565_data, width, height, &jpeg_data, &jpeg_size, 12);
      
      if (!success || jpeg_size == 0) {
        xSemaphoreGive(this->jpeg_mutex_);
        return 0;
      }

      // Construire la réponse multipart
      char header[150];
      int header_len = snprintf(header, sizeof(header),
        "--frame\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n",
        jpeg_size
      );

      size_t total_size = header_len + jpeg_size + 2;  // +2 pour \r\n final

      if (total_size > maxLen) {
        xSemaphoreGive(this->jpeg_mutex_);
        return 0;
      }

      // Copier header
      memcpy(buffer, header, header_len);
      
      // Copier JPEG
      memcpy(buffer + header_len, jpeg_data, jpeg_size);
      
      // Ajouter \r\n final
      buffer[header_len + jpeg_size] = '\r';
      buffer[header_len + jpeg_size + 1] = '\n';

      xSemaphoreGive(this->jpeg_mutex_);

      // Délai pour limiter le framerate (~10 FPS)
      delay(100);

      return total_size;
    }
  );

  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
}

bool MipiCameraWebServer::encode_jpeg_(const uint8_t *rgb565_data, 
                                       size_t width, size_t height,
                                       uint8_t **jpeg_out, size_t *jpeg_size,
                                       int quality) {
  // Allouer buffer RGB888 temporaire
  size_t rgb888_size = width * height * 3;
  uint8_t *rgb888_data = (uint8_t *) heap_caps_malloc(
    rgb888_size, 
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );
  
  if (rgb888_data == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate RGB888 buffer");
    return false;
  }

  // Convertir RGB565 -> RGB888
  this->rgb565_to_rgb888_(rgb565_data, rgb888_data, width * height);

  // Encoder en JPEG avec jpegenc
  JPEGENC encoder;
  int result = encoder.open(this->jpeg_buffer_, this->jpeg_buffer_size_);
  
  if (result != JPEGE_SUCCESS) {
    ESP_LOGE(TAG, "JPEG encoder open failed");
    heap_caps_free(rgb888_data);
    return false;
  }

  result = encoder.encodeBegin(
    width, height, 
    JPEGE_PIXEL_RGB888, 
    JPEGE_SUBSAMPLE_420, 
    quality
  );
  
  if (result != JPEGE_SUCCESS) {
    ESP_LOGE(TAG, "JPEG encodeBegin failed");
    heap_caps_free(rgb888_data);
    return false;
  }

  result = encoder.addFrame(rgb888_data, width * 3);
  
  if (result != JPEGE_SUCCESS) {
    ESP_LOGE(TAG, "JPEG addFrame failed");
    heap_caps_free(rgb888_data);
    return false;
  }

  *jpeg_size = encoder.close();
  *jpeg_out = this->jpeg_buffer_;

  heap_caps_free(rgb888_data);

  if (*jpeg_size == 0 || *jpeg_size > this->jpeg_buffer_size_) {
    ESP_LOGE(TAG, "JPEG encoding produced invalid size: %u", *jpeg_size);
    return false;
  }

  ESP_LOGV(TAG, "JPEG encoded: %ux%u -> %u bytes (quality: %d)", 
           width, height, *jpeg_size, quality);
  
  return true;
}

void MipiCameraWebServer::rgb565_to_rgb888_(const uint8_t *rgb565, 
                                           uint8_t *rgb888, 
                                           size_t pixels) {
  for (size_t i = 0; i < pixels; i++) {
    // Lire pixel RGB565 (little-endian)
    uint16_t pixel = (rgb565[i * 2 + 1] << 8) | rgb565[i * 2];
    
    // Extraire les composantes RGB565
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5) & 0x3F;
    uint8_t b5 = pixel & 0x1F;
    
    // Convertir en RGB888 avec expansion
    rgb888[i * 3 + 0] = (r5 << 3) | (r5 >> 2);  // R
    rgb888[i * 3 + 1] = (g6 << 2) | (g6 >> 4);  // G
    rgb888[i * 3 + 2] = (b5 << 3) | (b5 >> 2);  // B
  }
}

}  // namespace mipi_camera_web_server
}  // namespace esphome

#endif  // USE_ESP32
