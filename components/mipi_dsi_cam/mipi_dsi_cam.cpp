#include "mipi_dsi_cam.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// Inclusion du code généré pour les drivers
// Ce fichier sera créé automatiquement par __init__.py
#include "mipi_dsi_cam_drivers_generated.h"

#ifdef USE_ESP32_VARIANT_ESP32P4

namespace esphome {
namespace mipi_dsi_cam {

static const char *const TAG = "mipi_dsi_cam";

// ============================================================================
// IMPLÉMENTATION MIPI DSI CAM (GÉNÉRIQUE - AUCUNE RÉFÉRENCE AUX SENSORS)
// ============================================================================

void MipiDsiCam::setup() {
  ESP_LOGI(TAG, "🎥 Init MIPI Camera");
  ESP_LOGI(TAG, "   Sensor type: %s", this->sensor_type_.c_str());
  
  // 1. Init pins
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(20);
  }
  
  // 2. Créer le driver du sensor via factory
  if (!this->create_sensor_driver_()) {
    ESP_LOGE(TAG, "❌ Driver creation failed");
    this->mark_failed();
    return;
  }
  
  // 3. Init sensor
  if (!this->init_sensor_()) {
    ESP_LOGE(TAG, "❌ Sensor init failed");
    this->mark_failed();
    return;
  }
  
  // 4. Init LDO
  if (!this->init_ldo_()) {
    ESP_LOGE(TAG, "❌ LDO init failed");
    this->mark_failed();
    return;
  }
  
  // 5. Init CSI
  if (!this->init_csi_()) {
    ESP_LOGE(TAG, "❌ CSI init failed");
    this->mark_failed();
    return;
  }
  
  // 6. Init ISP
  if (!this->init_isp_()) {
    ESP_LOGE(TAG, "❌ ISP init failed");
    this->mark_failed();
    return;
  }
  
  // 7. Allouer buffers
  if (!this->allocate_buffer_()) {
    ESP_LOGE(TAG, "❌ Buffer alloc failed");
    this->mark_failed();
    return;
  }
  
  this->initialized_ = true;
  ESP_LOGI(TAG, "✅ Camera ready (%ux%u)", this->width_, this->height_);
}

bool MipiDsiCam::create_sensor_driver_() {
  ESP_LOGI(TAG, "Creating driver for: %s", this->sensor_type_.c_str());
  
  // Appel à la factory function fournie par le code généré
  // Cette fonction est implémentée dans le code généré par chaque sensor
  this->sensor_driver_ = create_sensor_driver(this->sensor_type_, this);
  
  if (this->sensor_driver_ == nullptr) {
    ESP_LOGE(TAG, "Unknown or unavailable sensor: %s", this->sensor_type_.c_str());
    return false;
  }
  
  ESP_LOGI(TAG, "✓ Driver created for: %s", this->sensor_driver_->get_name());
  return true;
}

bool MipiDsiCam::init_sensor_() {
  if (!this->sensor_driver_) {
    ESP_LOGE(TAG, "No sensor driver");
    return false;
  }
  
  ESP_LOGI(TAG, "Init sensor: %s", this->sensor_driver_->get_name());
  
  // Récupérer les métadonnées du sensor
  this->width_ = this->sensor_driver_->get_width();
  this->height_ = this->sensor_driver_->get_height();
  this->lane_count_ = this->sensor_driver_->get_lane_count();
  this->bayer_pattern_ = this->sensor_driver_->get_bayer_pattern();
  this->lane_bitrate_mbps_ = this->sensor_driver_->get_lane_bitrate_mbps();
  
  ESP_LOGI(TAG, "   Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGI(TAG, "   Lanes: %u", this->lane_count_);
  ESP_LOGI(TAG, "   Bayer: %u", this->bayer_pattern_);
  ESP_LOGI(TAG, "   Bitrate: %u Mbps", this->lane_bitrate_mbps_);
  
  // Vérifier l'ID
  uint16_t pid = 0;
  esp_err_t ret = this->sensor_driver_->read_id(&pid);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read sensor ID");
    return false;
  }
  
  if (pid != this->sensor_driver_->get_pid()) {
    ESP_LOGE(TAG, "Wrong PID: 0x%04X (expected 0x%04X)", 
             pid, this->sensor_driver_->get_pid());
    return false;
  }
  
  ESP_LOGI(TAG, "✓ Sensor ID: 0x%04X", pid);
  
  // Initialiser le sensor
  ret = this->sensor_driver_->init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Sensor init failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "✓ Sensor initialized");
  
  // Attendre que le sensor se stabilise (exposition, gain, etc.)
  delay(200);
  ESP_LOGI(TAG, "✓ Sensor stabilized");
  
  return true;
}

bool MipiDsiCam::init_ldo_() {
  ESP_LOGI(TAG, "Init LDO MIPI");
  
  esp_ldo_channel_config_t ldo_config = {
    .chan_id = 3,
    .voltage_mv = 2500,
  };
  
  esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &this->ldo_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LDO failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "✓ LDO OK (2.5V)");
  return true;
}

bool MipiDsiCam::init_csi_() {
  ESP_LOGI(TAG, "Init MIPI-CSI");
  
  esp_cam_ctlr_csi_config_t csi_config = {};
  csi_config.ctlr_id = 0;
  csi_config.clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT;
  csi_config.h_res = this->width_;
  csi_config.v_res = this->height_;
  csi_config.lane_bit_rate_mbps = this->lane_bitrate_mbps_;
  csi_config.input_data_color_type = CAM_CTLR_COLOR_RAW8;
  csi_config.output_data_color_type = CAM_CTLR_COLOR_RGB565;
  csi_config.data_lane_num = this->lane_count_;
  csi_config.byte_swap_en = false;
  csi_config.queue_items = 10;
  
  esp_err_t ret = esp_cam_new_csi_ctlr(&csi_config, &this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CSI failed: %d", ret);
    return false;
  }
  
  // Callbacks
  esp_cam_ctlr_evt_cbs_t callbacks = {
    .on_get_new_trans = MipiDsiCam::on_csi_new_frame_,
    .on_trans_finished = MipiDsiCam::on_csi_frame_done_,
  };
  
  ret = esp_cam_ctlr_register_event_callbacks(this->csi_handle_, &callbacks, this);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Callbacks failed: %d", ret);
    return false;
  }
  
  ret = esp_cam_ctlr_enable(this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Enable CSI failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "✓ CSI OK (queue_items=10 pour meilleur buffering)");
  return true;
}

bool MipiDsiCam::init_isp_() {
  ESP_LOGI(TAG, "Init ISP");
  
  uint32_t isp_clock_hz = 120000000;
  
  esp_isp_processor_cfg_t isp_config = {};
  isp_config.clk_src = ISP_CLK_SRC_DEFAULT;
  isp_config.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  isp_config.input_data_color_type = ISP_COLOR_RAW8;
  isp_config.output_data_color_type = ISP_COLOR_RGB565;
  isp_config.h_res = this->width_;
  isp_config.v_res = this->height_;
  isp_config.has_line_start_packet = false;
  isp_config.has_line_end_packet = false;
  isp_config.clk_hz = isp_clock_hz;
  isp_config.bayer_order = (color_raw_element_order_t)this->bayer_pattern_;
  
  esp_err_t ret = esp_isp_new_processor(&isp_config, &this->isp_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ISP creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_enable(this->isp_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ISP enable failed: 0x%x", ret);
    esp_isp_del_processor(this->isp_handle_);
    this->isp_handle_ = nullptr;
    return false;
  }
  
  ESP_LOGI(TAG, "✓ ISP OK (bayer=%u, luminosité/contraste via sensor)", this->bayer_pattern_);
  return true;
}

bool MipiDsiCam::allocate_buffer_() {
  this->frame_buffer_size_ = this->width_ * this->height_ * 2;  // RGB565
  
  this->frame_buffers_[0] = (uint8_t*)heap_caps_aligned_alloc(
    64, this->frame_buffer_size_, MALLOC_CAP_SPIRAM
  );
  
  this->frame_buffers_[1] = (uint8_t*)heap_caps_aligned_alloc(
    64, this->frame_buffer_size_, MALLOC_CAP_SPIRAM
  );
  
  if (!this->frame_buffers_[0] || !this->frame_buffers_[1]) {
    ESP_LOGE(TAG, "Buffer alloc failed");
    return false;
  }
  
  this->current_frame_buffer_ = this->frame_buffers_[0];
  
  ESP_LOGI(TAG, "✓ Buffers: 2x%u bytes", this->frame_buffer_size_);
  return true;
}

bool IRAM_ATTR MipiDsiCam::on_csi_new_frame_(
  esp_cam_ctlr_handle_t handle,
  esp_cam_ctlr_trans_t *trans,
  void *user_data
) {
  MipiDsiCam *cam = (MipiDsiCam*)user_data;
  trans->buffer = cam->frame_buffers_[cam->buffer_index_];
  trans->buflen = cam->frame_buffer_size_;
  return false;
}

bool IRAM_ATTR MipiDsiCam::on_csi_frame_done_(
  esp_cam_ctlr_handle_t handle,
  esp_cam_ctlr_trans_t *trans,
  void *user_data
) {
  MipiDsiCam *cam = (MipiDsiCam*)user_data;
  
  if (trans->received_size > 0) {
    cam->frame_ready_ = true;
    cam->buffer_index_ = (cam->buffer_index_ + 1) % 2;
    cam->total_frames_received_++;
  }
  
  return false;
}

bool MipiDsiCam::start_streaming() {
  if (!this->initialized_ || this->streaming_) {
    return false;
  }
  
  ESP_LOGI(TAG, "Start streaming");
  
  // Reset compteurs
  this->total_frames_received_ = 0;
  this->last_frame_log_time_ = millis();
  
  // Démarrer sensor
  if (this->sensor_driver_) {
    esp_err_t ret = this->sensor_driver_->start_stream();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Sensor start failed: %d", ret);
      return false;
    }
    delay(100);
  }
  
  // Démarrer CSI
  esp_err_t ret = esp_cam_ctlr_start(this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CSI start failed: %d", ret);
    return false;
  }
  
  this->streaming_ = true;
  ESP_LOGI(TAG, "✅ Streaming active");
  return true;
}

bool MipiDsiCam::stop_streaming() {
  if (!this->streaming_) {
    return true;
  }
  
  esp_cam_ctlr_stop(this->csi_handle_);
  
  if (this->sensor_driver_) {
    this->sensor_driver_->stop_stream();
  }
  
  this->streaming_ = false;
  ESP_LOGI(TAG, "⏹ Streaming stopped");
  return true;
}

bool MipiDsiCam::capture_frame() {
  if (!this->streaming_) {
    return false;
  }
  
  bool was_ready = this->frame_ready_;
  if (was_ready) {
    this->frame_ready_ = false;
    uint8_t last_buffer = (this->buffer_index_ + 1) % 2;
    this->current_frame_buffer_ = this->frame_buffers_[last_buffer];
  }
  
  return was_ready;
}

void MipiDsiCam::loop() {
  // Géré par callbacks ISR
  
  // Debug: Logger le FPS du sensor ET le flag frame_ready
  if (this->streaming_) {
    static uint32_t ready_count = 0;
    static uint32_t not_ready_count = 0;
    
    if (this->frame_ready_) {
      ready_count++;
    } else {
      not_ready_count++;
    }
    
    uint32_t now = millis();
    if (now - this->last_frame_log_time_ >= 3000) {
      float sensor_fps = this->total_frames_received_ / 3.0f;
      float ready_rate = (float)ready_count / (float)(ready_count + not_ready_count) * 100.0f;
      
      ESP_LOGI(TAG, "📷 Sensor: %.1f fps | frame_ready: %.1f%% du temps", 
               sensor_fps, ready_rate);
      
      this->total_frames_received_ = 0;
      this->last_frame_log_time_ = now;
      ready_count = 0;
      not_ready_count = 0;
    }
  }
}

void MipiDsiCam::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI Camera:");
  if (this->sensor_driver_) {
    ESP_LOGCONFIG(TAG, "  Sensor: %s", this->sensor_driver_->get_name());
    ESP_LOGCONFIG(TAG, "  PID: 0x%04X", this->sensor_driver_->get_pid());
  } else {
    ESP_LOGCONFIG(TAG, "  Sensor: %s (driver not loaded)", this->sensor_type_.c_str());
  }
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Format: RGB565");
  ESP_LOGCONFIG(TAG, "  Lanes: %u", this->lane_count_);
  ESP_LOGCONFIG(TAG, "  Bayer: %u", this->bayer_pattern_);
  ESP_LOGCONFIG(TAG, "  Streaming: %s", this->streaming_ ? "YES" : "NO");
}

}  // namespace mipi_dsi_cam
}  // namespace esphome

#endif  // USE_ESP32_VARIANT_ESP32P4
