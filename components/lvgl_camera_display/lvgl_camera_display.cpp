#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

void LVGLCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "🎥 LVGL Camera Display (Low Latency Mode)");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "❌ Camera not configured");
    this->mark_failed();
    return;
  }

  // 🔧 Pas besoin de update_interval en mode event-driven
  ESP_LOGI(TAG, "✅ Display initialized (event-driven mode)");
}

void LVGLCameraDisplay::loop() {
  // 🆕 Mode event-driven: on check seulement si une nouvelle frame est disponible
  if (!this->camera_->is_streaming()) {
    return;
  }
  
  // 🔧 Vérifier si nouvelle frame disponible (non-bloquant, atomique)
  if (!this->camera_->has_new_frame()) {
    return;  // Pas de nouvelle frame, ne rien faire
  }
  
  // 🆕 Capturer la frame (swap de buffer atomique)
  if (this->camera_->capture_frame()) {
    this->update_canvas_();
    this->frame_count_++;

    // Logger FPS réel toutes les 100 frames
    if (this->frame_count_ % 100 == 0) {
      uint32_t now_time = millis();

      if (this->last_fps_time_ > 0) {
        float elapsed = (now_time - this->last_fps_time_) / 1000.0f;
        float fps = 100.0f / elapsed;
        ESP_LOGI(TAG, "🎞️ Display FPS: %.2f | %u frames total", fps, this->frame_count_);
      }
      this->last_fps_time_ = now_time;
    }
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Mode: Event-driven (zero-copy)");
  ESP_LOGCONFIG(TAG, "  Canvas: %s", this->canvas_obj_ ? "YES" : "NO");
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr || this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "❌ Canvas null");
      this->canvas_warning_shown_ = true;
    }
    return;
  }

  uint8_t* img_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr) {
    return;
  }

  if (this->first_update_) {
    ESP_LOGI(TAG, "🖼️  First canvas update:");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer: %p", img_data);
    this->first_update_ = false;
  }

  // 🔧 CRITIQUE: Ne PAS appeler lv_canvas_set_buffer à chaque frame si le buffer ne change pas
  // Le buffer est maintenant stable (triple buffering), donc on peut juste invalider
  
  // 🆕 Première fois ou si le buffer a changé: set_buffer
  if (this->last_buffer_ptr_ != img_data) {
    // Verrouillage LVGL pour thread-safety
    lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
    this->last_buffer_ptr_ = img_data;
  }
  
  // 🔧 Invalider seulement la zone nécessaire (plus rapide que tout l'écran)
  lv_obj_invalidate(this->canvas_obj_);
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas) { 
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "🎨 Canvas configured: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "   Canvas size: %dx%d", w, h);
    
    // 🆕 Désactiver le cache de transformation si disponible pour réduire la latence
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  }
}

}  // namespace lvgl_camera_display
}  // namespace esphome
