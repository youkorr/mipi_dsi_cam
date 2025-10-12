#include "mipi_camera_web_server.h"

#ifdef USE_ESP32_VARIANT_ESP32P4

#include "esphome/core/log.h"
#include "esphome/core/application.h"

// Inclure l'encodeur JPEG ESP-IDF si disponible
//#include "esp_jpeg_common.h"
//#include "jpeg_encoder.h"

namespace esphome {
namespace mipi_camera_web_server {

static const char *const TAG = "mipi_camera_web_server";

// Page HTML simple et efficace
static const char INDEX_HTML[] = R"html(
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

  // Configuration du serveur HTTP ESP-IDF
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = this->port_;
  config.ctrl_port = this->port_;
  config.max_open_sockets = 7;
  config.lru_purge_enable = true;

  // Démarrer le serveur
  esp_err_t ret = httpd_start(&this->server_, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %d", ret);
    this->mark_failed();
    return;
  }

  // Enregistrer les handlers
  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = MipiCameraWebServer::index_handler_,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &index_uri);

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = MipiCameraWebServer::stream_handler_,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &stream_uri);

  httpd_uri_t snapshot_uri = {
    .uri = "/snapshot",
    .method = HTTP_GET,
    .handler = MipiCameraWebServer::snapshot_handler_,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &snapshot_uri);

  httpd_uri_t control_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = MipiCameraWebServer::control_handler_,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &control_uri);

  ESP_LOGI(TAG, "Web server started on port %d", this->port_);
}

void MipiCameraWebServer::loop() {
  // Le serveur HTTP ESP-IDF gère tout automatiquement
}

void MipiCameraWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI Camera Web Server:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  if (this->camera_) {
    ESP_LOGCONFIG(TAG, "  Resolution: %ux%u",
                  this->camera_->get_image_width(),
                  this->camera_->get_image_height());
  }
}

esp_err_t MipiCameraWebServer::index_handler_(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "identity");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t MipiCameraWebServer::stream_handler_(httpd_req_t *req) {
  MipiCameraWebServer *server = (MipiCameraWebServer *)req->user_ctx;

  if (!server->camera_ || !server->camera_->is_streaming()) {
    httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Camera not streaming");
    return ESP_FAIL;
  }

  if (!server->camera_->capture_frame()) {
    httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "No frame");
    return ESP_FAIL;
  }

  uint8_t *rgb565 = server->camera_->get_image_data();
  uint16_t w = server->camera_->get_image_width();
  uint16_t h = server->camera_->get_image_height();

  if (!rgb565) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid data");
    return ESP_FAIL;
  }

  if (xSemaphoreTake(server->jpeg_mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
    httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Busy");
    return ESP_FAIL;
  }

  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;
  bool ok = server->encode_jpeg_(rgb565, w, h, &jpeg_data, &jpeg_size, 80);

  if (!ok || jpeg_size == 0) {
    xSemaphoreGive(server->jpeg_mutex_);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Encoding failed");
    return ESP_FAIL;
  }

  // Envoyer l'image JPEG
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Expires", "0");
  
  esp_err_t ret = httpd_resp_send(req, (const char *)jpeg_data, jpeg_size);
  
  xSemaphoreGive(server->jpeg_mutex_);
  
  return ret;
}

esp_err_t MipiCameraWebServer::snapshot_handler_(httpd_req_t *req) {
  MipiCameraWebServer *server = (MipiCameraWebServer *)req->user_ctx;

  if (!server->camera_ || !server->camera_->is_streaming()) {
    httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Camera not streaming");
    return ESP_FAIL;
  }

  if (!server->camera_->capture_frame()) {
    httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "No frame");
    return ESP_FAIL;
  }

  uint8_t *rgb565 = server->camera_->get_image_data();
  uint16_t w = server->camera_->get_image_width();
  uint16_t h = server->camera_->get_image_height();

  if (!rgb565) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid data");
    return ESP_FAIL;
  }

  if (xSemaphoreTake(server->jpeg_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Busy");
    return ESP_FAIL;
  }

  uint8_t *jpeg_data = nullptr;
  size_t jpeg_size = 0;
  bool ok = server->encode_jpeg_(rgb565, w, h, &jpeg_data, &jpeg_size, 90);

  if (!ok || jpeg_size == 0) {
    xSemaphoreGive(server->jpeg_mutex_);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Encoding failed");
    return ESP_FAIL;
  }

  // Envoyer l'image avec header de téléchargement
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=snapshot.jpg");
  
  esp_err_t ret = httpd_resp_send(req, (const char *)jpeg_data, jpeg_size);
  
  xSemaphoreGive(server->jpeg_mutex_);
  
  return ret;
}

esp_err_t MipiCameraWebServer::control_handler_(httpd_req_t *req) {
  MipiCameraWebServer *server = (MipiCameraWebServer *)req->user_ctx;

  // Parser les paramètres de l'URL
  char buf[100];
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  
  if (buf_len > 1 && buf_len <= sizeof(buf)) {
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      char param[32];
      
      // Chercher le paramètre brightness
      if (httpd_query_key_value(buf, "brightness", param, sizeof(param)) == ESP_OK) {
        uint8_t level = atoi(param);
        server->camera_->set_brightness_level(level);
        
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
      }
    }
  }

  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid parameter");
  return ESP_FAIL;
}

bool MipiCameraWebServer::encode_jpeg_(const uint8_t *rgb565, size_t w, size_t h,
                                       uint8_t **jpeg_out, size_t *jpeg_size, int quality) {
  // Allouer RGB888 temporaire en PSRAM
  size_t rgb888_size = w * h * 3;
  uint8_t *rgb888 = (uint8_t *)heap_caps_malloc(rgb888_size, MALLOC_CAP_SPIRAM);
  if (!rgb888) {
    ESP_LOGE(TAG, "RGB888 alloc failed");
    return false;
  }

  // Convertir RGB565 -> RGB888
  this->rgb565_to_rgb888_(rgb565, rgb888, w * h);

  // Configuration de l'encodeur JPEG ESP-IDF
  jpeg_encode_config_t encode_config = {
    .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
    .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
    .image_quality = quality,
    .width = (int)w,
    .height = (int)h,
  };

  jpeg_encode_engine_cfg_t engine_cfg = {
    .timeout_ms = 1000,
  };

  jpeg_encoder_handle_t encoder_handle = nullptr;
  
  // Créer l'encodeur
  esp_err_t ret = jpeg_new_encoder_engine(&engine_cfg, &encoder_handle);
  if (ret != ESP_OK || encoder_handle == nullptr) {
    ESP_LOGE(TAG, "Failed to create JPEG encoder: 0x%x", ret);
    heap_caps_free(rgb888);
    return false;
  }

  // Préparer la structure pour l'encodage
  jpeg_encode_picture_t encode_pic = {
    .width = (int)w,
    .height = (int)h,
    .format = JPEG_ENCODE_IN_FORMAT_RGB888,
    .color_space = JPEG_ENC_COLOR_SPACE_RGB,
  };

  uint32_t out_size = 0;
  
  // Encoder l'image
  ret = jpeg_encoder_process(
    encoder_handle,
    &encode_config,
    rgb888,
    rgb888_size,
    this->jpeg_buffer_,
    this->jpeg_buffer_size_,
    &out_size
  );

  // Libérer les ressources
  jpeg_del_encoder_engine(encoder_handle);
  heap_caps_free(rgb888);

  if (ret != ESP_OK || out_size == 0) {
    ESP_LOGE(TAG, "JPEG encoding failed: 0x%x, size: %u", ret, out_size);
    return false;
  }

  *jpeg_size = out_size;
  *jpeg_out = this->jpeg_buffer_;

  ESP_LOGV(TAG, "JPEG encoded: %ux%u -> %u bytes (quality: %d)", w, h, out_size, quality);
  
  return true;
}

void MipiCameraWebServer::rgb565_to_rgb888_(const uint8_t *rgb565, uint8_t *rgb888, size_t pixels) {
  for (size_t i = 0; i < pixels; i++) {
    // Lire pixel RGB565 (little-endian)
    uint16_t px = (rgb565[i * 2 + 1] << 8) | rgb565[i * 2];
    
    // Extraire les composantes RGB565
    uint8_t r5 = (px >> 11) & 0x1F;
    uint8_t g6 = (px >> 5) & 0x3F;
    uint8_t b5 = px & 0x1F;
    
    // Convertir en RGB888 avec expansion
    rgb888[i * 3 + 0] = (r5 << 3) | (r5 >> 2);  // R
    rgb888[i * 3 + 1] = (g6 << 2) | (g6 >> 4);  // G
    rgb888[i * 3 + 2] = (b5 << 3) | (b5 >> 2);  // B
  }
}

}  // namespace mipi_camera_web_server
}  // namespace esphome
