#include "mipi_camera_web_server.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#ifdef USE_ESP32

#include <JPEGENC.h>

namespace esphome {
namespace mipi_camera_web_server {

static const char *const TAG = "mipi_camera_web_server";

// Page HTML simple
static const char CAMERA_INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-P4 Camera</title>
  <style>
    body { font-family: Arial; text-align: center; margin: 20px; background: #1a1a1a; color: #fff; }
    img { max-width: 100%; height: auto; border: 2px solid #333; border-radius: 8px; }
    .controls { margin: 20px auto; max-width: 600px; }
    button { 
      background: #0066cc; color: white; border: none; 
      padding: 10px 20px; margin: 5px; border-radius: 5px; cursor: pointer; 
    }
    button:hover { background: #0052a3; }
    .slider { width: 80%; }
  </style>
</head>
<body>
  <h1>ESP32-P4 MIPI Camera</h1>
  <div>
    <img id="stream" src="/stream" onerror="this.src='/stream?t='+Date.now()">
  </div>
  <div class="controls">
    <button onclick="snapshot()">📸 Snapshot</button>
    <button onclick="toggleStream()">⏯️ Toggle Stream</button>
    <br><br>
    <label>Brightness: <input type="range" class="slider" min="0" max="10" value="5" 
           onchange="setBrightness(this.value)"></label>
  </div>
  
  <script>
    let streaming = true;
    
    function toggleStream() {
      const img = document.getElementById('stream');
      streaming = !streaming;
      img.style.display = streaming ? 'block' : 'none';
      if (streaming) {
        img.src = '/stream?t=' + Date.now();
      }
    }
    
    function snapshot() {
      window.open('/snapshot', '_blank');
    }
    
    function setBrightness(val) {
      fetch('/control?brightness=' + val);
    }
    
    // Rafraîchir le stream toutes les 100ms
    setInterval(() => {
      if (streaming) {
        const img = document.getElementById('stream');
        const src = img.src.split('?')[0];
        img.src = src + '?t=' + Date.now();
      }
    }, 100);
  </script>
</body>
</html>
)html";

void MipiCameraWebServer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MIPI Camera Web Server...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured!");
    this->mark_failed();
    return;
  }

  // Allouer buffer JPEG (max 100KB)
  this->jpeg_buffer_size_ = 100 * 1024;
  this->jpeg_buffer_ = (uint8_t *)heap_caps_malloc(
    this->jpeg_buffer_size_, MALLOC_CAP_SPIRAM
  );

  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
    this->mark_failed();
    return;
  }

  // Créer le serveur web
  this->server_ = new AsyncWebServer(this->port_);

  // Route page index
  this->server_->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_index_(request);
  });

  // Route stream
  this->server_->on("/stream", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_stream_(request);
  });

  // Route snapshot
  this->server_->on("/snapshot", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_snapshot_(request);
  });

  // Route contrôle
  this->server_->on("/control", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_control_(request);
  });

  // Démarrer le serveur
  this->server_->begin();

  // Démarrer le streaming caméra
  this->camera_->start_streaming();

  ESP_LOGI(TAG, "Web server started on port %d", this->port_);
}

void MipiCameraWebServer::loop() {
  // Rien à faire, AsyncWebServer gère tout
}

void MipiCameraWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI Camera Web Server:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  URL: http://%s:%d", 
                network::get_ip_address().str().c_str(), this->port_);
}

void MipiCameraWebServer::handle_index_(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", CAMERA_INDEX_HTML);
}

void MipiCameraWebServer::handle_stream_(AsyncWebServerRequest *request) {
  if (!this->camera_->is_streaming()) {
    request->send(503, "text/plain", "Camera not streaming");
    return;
  }

  // Capturer une frame
  if (!this->camera_->capture_frame()) {
    request->send(503, "text/plain", "No frame available");
    return;
  }

  uint8_t *rgb565_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (rgb565_data == nullptr) {
    request->send(503, "text/plain", "Invalid frame data");
    return;
  }

  // Encoder en JPEG
  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;

  if (!this->encode_jpeg_(rgb565_data, width, height, &jpeg_data, &jpeg_size)) {
    request->send(500, "text/plain", "JPEG encoding failed");
    return;
  }

  // Envoyer l'image JPEG
  AsyncWebServerResponse *response = request->beginResponse(
    "image/jpeg", jpeg_size,
    [jpeg_data](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      size_t len = maxLen;
      memcpy(buffer, jpeg_data + index, len);
      return len;
    }
  );

  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
}

void MipiCameraWebServer::handle_snapshot_(AsyncWebServerRequest *request) {
  // Identique à handle_stream_ mais avec headers différents
  if (!this->camera_->is_streaming()) {
    request->send(503, "text/plain", "Camera not streaming");
    return;
  }

  if (!this->camera_->capture_frame()) {
    request->send(503, "text/plain", "No frame available");
    return;
  }

  uint8_t *rgb565_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (rgb565_data == nullptr) {
    request->send(503, "text/plain", "Invalid frame data");
    return;
  }

  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;

  if (!this->encode_jpeg_(rgb565_data, width, height, &jpeg_data, &jpeg_size)) {
    request->send(500, "text/plain", "JPEG encoding failed");
    return;
  }

  AsyncWebServerResponse *response = request->beginResponse(
    "image/jpeg", jpeg_size,
    [jpeg_data](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      size_t len = maxLen;
      memcpy(buffer, jpeg_data + index, len);
      return len;
    }
  );

  response->addHeader("Content-Disposition", "attachment; filename=snapshot.jpg");
  request->send(response);
}

void MipiCameraWebServer::handle_control_(AsyncWebServerRequest *request) {
  if (request->hasParam("brightness")) {
    String brightness_str = request->getParam("brightness")->value();
    uint8_t level = brightness_str.toInt();
    this->camera_->set_brightness_level(level);
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Invalid parameter");
  }
}

bool MipiCameraWebServer::encode_jpeg_(const uint8_t *rgb565_data, 
                                       size_t width, size_t height,
                                       uint8_t **jpeg_out, size_t *jpeg_size) {
  // Convertir RGB565 -> RGB888
  size_t rgb888_size = width * height * 3;
  uint8_t *rgb888_data = (uint8_t *)heap_caps_malloc(rgb888_size, MALLOC_CAP_SPIRAM);
  
  if (rgb888_data == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate RGB888 buffer");
    return false;
  }

  this->rgb565_to_rgb888_(rgb565_data, rgb888_data, width * height);

  // Encoder en JPEG
  JPEGENC encoder;
  encoder.open(this->jpeg_buffer_, this->jpeg_buffer_size_);
  encoder.encodeBegin(width, height, JPEGE_PIXEL_RGB888, JPEGE_SUBSAMPLE_420, JPEGE_Q_HIGH);
  encoder.addFrame(rgb888_data, width * 3);
  *jpeg_size = encoder.close();
  *jpeg_out = this->jpeg_buffer_;

  heap_caps_free(rgb888_data);

  if (*jpeg_size == 0) {
    ESP_LOGE(TAG, "JPEG encoding failed");
    return false;
  }

  ESP_LOGV(TAG, "JPEG encoded: %u bytes", *jpeg_size);
  return true;
}

void MipiCameraWebServer::rgb565_to_rgb888_(const uint8_t *rgb565, 
                                           uint8_t *rgb888, size_t pixels) {
  for (size_t i = 0; i < pixels; i++) {
    uint16_t pixel = (rgb565[i * 2 + 1] << 8) | rgb565[i * 2];
    
    // Extraire RGB565
    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;
    
    // Convertir en RGB888
    rgb888[i * 3 + 0] = (r << 3) | (r >> 2);  // R
    rgb888[i * 3 + 1] = (g << 2) | (g >> 4);  // G
    rgb888[i * 3 + 2] = (b << 3) | (b >> 2);  // B
  }
}

}  // namespace mipi_camera_web_server
}  // namespace esphome

#endif  // USE_ESP32
