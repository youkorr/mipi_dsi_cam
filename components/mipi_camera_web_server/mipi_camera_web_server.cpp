#include "mipi_camera_web_server.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <JPEGENC.h>

namespace esphome {
namespace mipi_camera_web_server {

static const char *const TAG = "mipi_camera_web_server";

// Page HTML simple et efficace
static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-P4 Camera</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:Arial,sans-serif;background:#1a1a1a;color:#fff;padding:20px}
    h1{text-align:center;margin-bottom:20px}
    .container{max-width:1200px;margin:0 auto}
    #stream{width:100%;max-width:800px;height:auto;display:block;margin:20px auto;border:2px solid #333;border-radius:8px}
    .controls{text-align:center;margin:20px 0}
    button{background:#0066cc;color:#fff;border:none;padding:10px 20px;margin:5px;border-radius:5px;cursor:pointer;font-size:14px}
    button:hover{background:#0052a3}
    input[type=range]{width:200px;margin:0 10px}
    .control-group{margin:15px 0}
    .status{color:#0f0;font-size:12px}
  </style>
</head>
<body>
  <div class="container">
    <h1>🎥 ESP32-P4 Camera</h1>
    <img id="stream" src="/stream">
    <div class="controls">
      <button onclick="snapshot()">📸 Snapshot</button>
      <button onclick="toggleStream()">⏯️ Toggle</button>
      <div class="control-group">
        <label>Brightness: <input type="range" min="0" max="10" value="5" oninput="setBrightness(this.value)"><span id="bval">5</span></label>
      </div>
      <div class="status" id="status">Streaming...</div>
    </div>
  </div>
  <script>
    let streaming=true;
    const img=document.getElementById('stream');
    function toggleStream(){
      streaming=!streaming;
      img.style.display=streaming?'block':'none';
      document.getElementById('status').textContent=streaming?'Streaming...':'Paused';
    }
    function snapshot(){window.open('/snapshot','_blank')}
    function setBrightness(v){
      document.getElementById('bval').textContent=v;
      fetch('/control?brightness='+v);
    }
    setInterval(()=>{
      if(streaming){
        const t=Date.now();
        img.src='/stream?t='+t;
      }
    },100);
  </script>
</body>
</html>
)html";

void MipiCameraWebServer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MIPI Camera Web Server...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  // Créer mutex
  this->jpeg_mutex_ = xSemaphoreCreateMutex();
  if (this->jpeg_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create mutex");
    this->mark_failed();
    return;
  }

  // Allouer buffer JPEG en PSRAM
  this->jpeg_buffer_ = (uint8_t *)heap_caps_malloc(
    this->jpeg_buffer_size_, 
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
    this->mark_failed();
    return;
  }

  // Démarrer streaming caméra
  if (!this->camera_->is_streaming()) {
    this->camera_->start_streaming();
  }

  // Créer serveur web
  this->server_ = new AsyncWebServer(this->port_);

  // Routes
  this->server_->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });

  this->server_->on("/stream", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_stream_(request);
  });

  this->server_->on("/snapshot", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_snapshot_(request);
  });

  this->server_->on("/control", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_control_(request);
  });

  this->server_->begin();

  ESP_LOGI(TAG, "Web server started on port %d", this->port_);
}

void MipiCameraWebServer::loop() {
  // AsyncWebServer gère tout
}

void MipiCameraWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI Camera Web Server:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u",
                this->camera_->get_image_width(),
                this->camera_->get_image_height());
}

void MipiCameraWebServer::handle_stream_(AsyncWebServerRequest *request) {
  if (!this->camera_->is_streaming()) {
    request->send(503, "text/plain", "Camera not streaming");
    return;
  }

  if (!this->camera_->capture_frame()) {
    request->send(503, "text/plain", "No frame");
    return;
  }

  uint8_t *rgb565 = this->camera_->get_image_data();
  uint16_t w = this->camera_->get_image_width();
  uint16_t h = this->camera_->get_image_height();

  if (!rgb565) {
    request->send(500, "text/plain", "Invalid data");
    return;
  }

  if (xSemaphoreTake(this->jpeg_mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
    request->send(503, "text/plain", "Busy");
    return;
  }

  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;
  bool ok = this->encode_jpeg_(rgb565, w, h, &jpeg_data, &jpeg_size, 12);

  if (!ok || jpeg_size == 0) {
    xSemaphoreGive(this->jpeg_mutex_);
    request->send(500, "text/plain", "Encoding failed");
    return;
  }

  // Copier JPEG pour envoi async
  uint8_t *jpeg_copy = (uint8_t *)malloc(jpeg_size);
  if (!jpeg_copy) {
    xSemaphoreGive(this->jpeg_mutex_);
    request->send(500, "text/plain", "Malloc failed");
    return;
  }
  memcpy(jpeg_copy, jpeg_data, jpeg_size);
  xSemaphoreGive(this->jpeg_mutex_);

  AsyncWebServerResponse *response = request->beginResponse(
    "image/jpeg", jpeg_size,
    [jpeg_copy, jpeg_size](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
      size_t remain = jpeg_size - index;
      size_t send = (remain < maxLen) ? remain : maxLen;
      if (send > 0) {
        memcpy(buf, jpeg_copy + index, send);
      }
      if (index + send >= jpeg_size) {
        free((void *)jpeg_copy);
      }
      return send;
    }
  );

  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

void MipiCameraWebServer::handle_snapshot_(AsyncWebServerRequest *request) {
  // Identique à stream mais avec header download
  if (!this->camera_->is_streaming() || !this->camera_->capture_frame()) {
    request->send(503, "text/plain", "Not available");
    return;
  }

  uint8_t *rgb565 = this->camera_->get_image_data();
  uint16_t w = this->camera_->get_image_width();
  uint16_t h = this->camera_->get_image_height();

  if (!rgb565) {
    request->send(500, "text/plain", "Invalid data");
    return;
  }

  if (xSemaphoreTake(this->jpeg_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    request->send(503, "text/plain", "Busy");
    return;
  }

  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;
  bool ok = this->encode_jpeg_(rgb565, w, h, &jpeg_data, &jpeg_size, 15);

  if (!ok || jpeg_size == 0) {
    xSemaphoreGive(this->jpeg_mutex_);
    request->send(500, "text/plain", "Encoding failed");
    return;
  }

  uint8_t *jpeg_copy = (uint8_t *)malloc(jpeg_size);
  if (!jpeg_copy) {
    xSemaphoreGive(this->jpeg_mutex_);
    request->send(500, "text/plain", "Malloc failed");
    return;
  }
  memcpy(jpeg_copy, jpeg_data, jpeg_size);
  xSemaphoreGive(this->jpeg_mutex_);

  AsyncWebServerResponse *response = request->beginResponse(
    "image/jpeg", jpeg_size,
    [jpeg_copy, jpeg_size](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
      size_t remain = jpeg_size - index;
      size_t send = (remain < maxLen) ? remain : maxLen;
      if (send > 0) {
        memcpy(buf, jpeg_copy + index, send);
      }
      if (index + send >= jpeg_size) {
        free((void *)jpeg_copy);
      }
      return send;
    }
  );

  response->addHeader("Content-Disposition", "attachment; filename=snapshot.jpg");
  request->send(response);
}

void MipiCameraWebServer::handle_control_(AsyncWebServerRequest *request) {
  if (request->hasParam("brightness")) {
    uint8_t level = request->getParam("brightness")->value().toInt();
    this->camera_->set_brightness_level(level);
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Bad request");
  }
}

bool MipiCameraWebServer::encode_jpeg_(const uint8_t *rgb565, size_t w, size_t h,
                                       uint8_t **jpeg_out, size_t *jpeg_size, int quality) {
  // Allouer RGB888 temporaire
  size_t rgb888_size = w * h * 3;
  uint8_t *rgb888 = (uint8_t *)heap_caps_malloc(rgb888_size, MALLOC_CAP_SPIRAM);
  if (!rgb888) {
    ESP_LOGE(TAG, "RGB888 alloc failed");
    return false;
  }

  // Convertir
  this->rgb565_to_rgb888_(rgb565, rgb888, w * h);

  // Encoder JPEG
  JPEGENC encoder;
  int ret = encoder.open(this->jpeg_buffer_, this->jpeg_buffer_size_);
  if (ret != JPEGE_SUCCESS) {
    heap_caps_free(rgb888);
    return false;
  }

  ret = encoder.encodeBegin(w, h, JPEGE_PIXEL_RGB888, JPEGE_SUBSAMPLE_420, quality);
  if (ret != JPEGE_SUCCESS) {
    heap_caps_free(rgb888);
    return false;
  }

  ret = encoder.addFrame(rgb888, w * 3);
  heap_caps_free(rgb888);

  if (ret != JPEGE_SUCCESS) {
    return false;
  }

  *jpeg_size = encoder.close();
  *jpeg_out = this->jpeg_buffer_;

  return (*jpeg_size > 0 && *jpeg_size <= this->jpeg_buffer_size_);
}

void MipiCameraWebServer::rgb565_to_rgb888_(const uint8_t *rgb565, uint8_t *rgb888, size_t pixels) {
  for (size_t i = 0; i < pixels; i++) {
    uint16_t px = (rgb565[i * 2 + 1] << 8) | rgb565[i * 2];
    uint8_t r = (px >> 11) & 0x1F;
    uint8_t g = (px >> 5) & 0x3F;
    uint8_t b = px & 0x1F;
    rgb888[i * 3 + 0] = (r << 3) | (r >> 2);
    rgb888[i * 3 + 1] = (g << 2) | (g >> 4);
    rgb888[i * 3 + 2] = (b << 3) | (b >> 2);
  }
}

}  // namespace mipi_camera_web_server
}  // namespace esphome

#endif  // USE_ESP32
