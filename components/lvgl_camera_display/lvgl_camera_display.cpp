#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

void LVGLCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "🎥 Configuration LVGL Camera Display...");
  
  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "❌ Camera non configurée");
    this->mark_failed();
    return;
  }
  
  ESP_LOGI(TAG, "✅ LVGL Camera Display initialisé");
  ESP_LOGI(TAG, "   Update interval: %u ms (~%d FPS)", 
           this->update_interval_, 1000 / this->update_interval_);
  ESP_LOGI(TAG, "   ⚠️  Canvas sera configuré via on_boot lambda");
}

void LVGLCameraDisplay::loop() {
  uint32_t now = millis();
  
  // Vérifier si c'est le moment de mettre à jour
  if (now - this->last_update_ < this->update_interval_) {
    return;
  }
  
  this->last_update_ = now;
  
  // Vérifier que l'objet image est configuré
  if (this->img_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "❌ Image object non configuré - utilisez on_boot pour appeler configure_canvas()");
      this->canvas_warning_shown_ = true;
    }
    return;
  }
  
  // Si la caméra est en streaming, capturer ET mettre à jour l'image
  if (this->camera_->is_streaming()) {
    bool frame_captured = this->camera_->capture_frame();
    
    if (frame_captured) {
      this->update_canvas_();
      this->frame_count_++;
      
      // Logger FPS réel toutes les 100 frames
      if (this->frame_count_ % 100 == 0) {
        static uint32_t last_time = 0;
        uint32_t now_time = millis();
        if (last_time > 0) {
          float elapsed = (now_time - last_time) / 1000.0f;
          float fps = 100.0f / elapsed;
          ESP_LOGI(TAG, "🎞️  Frames: %u - FPS moyen: %.2f - Dropped: %u", 
                   this->frame_count_, fps, this->dropped_frames_);
        }
        last_time = now_time;
      }
    } else {
      this->dropped_frames_++;
    }
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGCONFIG(TAG, "  FPS cible: ~%d", 1000 / this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Image configurée: %s", this->img_obj_ ? "OUI" : "NON");
  ESP_LOGCONFIG(TAG, "  Caméra liée: %s", this->camera_ ? "OUI" : "NON");
  
  if (this->use_scaling_) {
    ESP_LOGCONFIG(TAG, "  Scaling activé: %ux%u → %ux%u", 
                  this->camera_->get_image_width(), 
                  this->camera_->get_image_height(),
                  this->target_width_,
                  this->target_height_);
  }
  
  if (this->camera_) {
    ESP_LOGCONFIG(TAG, "  Résolution caméra: %ux%u", 
                  this->camera_->get_image_width(), 
                  this->camera_->get_image_height());
  }
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr || this->img_obj_ == nullptr) {
    return;
  }
  
  uint8_t* img_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();
  
  if (img_data == nullptr) {
    ESP_LOGW(TAG, "⚠️  Données image nulles");
    return;
  }
  
  if (this->first_update_) {
    ESP_LOGI(TAG, "🖼️  Premier update image:");
    ESP_LOGI(TAG, "   Source: %ux%u", width, height);
    if (this->use_scaling_) {
      ESP_LOGI(TAG, "   Affichage: %ux%u (scaling actif)", this->target_width_, this->target_height_);
    }
    ESP_LOGI(TAG, "   Buffer: %p", img_data);
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X", 
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);
    
    this->first_update_ = false;
  }
  
  // Configurer la description de l'image
  this->img_dsc_.header.always_zero = 0;
  this->img_dsc_.header.w = width;
  this->img_dsc_.header.h = height;
  this->img_dsc_.data_size = width * height * 2;  // RGB565 = 2 bytes par pixel
  this->img_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
  this->img_dsc_.data = img_data;
  
  // Mettre à jour l'image
  lv_img_set_src(this->img_obj_, &this->img_dsc_);
  
  // Si scaling activé, l'objet image a déjà la bonne taille via lv_obj_set_size
  // LVGL fait le scaling automatiquement
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas) { 
  if (canvas == nullptr) {
    ESP_LOGE(TAG, "❌ Canvas fourni est NULL");
    return;
  }
  
  this->img_obj_ = canvas;
  this->use_scaling_ = false;
  
  ESP_LOGI(TAG, "🎨 Image configurée (sans scaling): %p", canvas);
  
  lv_coord_t w = lv_obj_get_width(canvas);
  lv_coord_t h = lv_obj_get_height(canvas);
  ESP_LOGI(TAG, "   Taille affichage: %dx%d", w, h);
  
  if (this->camera_) {
    uint16_t cam_w = this->camera_->get_image_width();
    uint16_t cam_h = this->camera_->get_image_height();
    
    if (w != cam_w || h != cam_h) {
      ESP_LOGW(TAG, "⚠️  ATTENTION: Dimensions différentes!");
      ESP_LOGW(TAG, "   Affichage: %dx%d, Caméra: %dx%d", w, h, cam_w, cam_h);
      ESP_LOGW(TAG, "   Utilisez configure_canvas(canvas, w, h) pour activer le scaling");
    }
  }
  
  // Reset du flag d'avertissement
  this->canvas_warning_shown_ = false;
  
  ESP_LOGI(TAG, "✅ Image prête pour l'affichage");
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas, uint16_t target_width, uint16_t target_height) {
  if (canvas == nullptr) {
    ESP_LOGE(TAG, "❌ Canvas fourni est NULL");
    return;
  }
  
  this->img_obj_ = canvas;
  this->target_width_ = target_width;
  this->target_height_ = target_height;
  this->use_scaling_ = true;
  
  ESP_LOGI(TAG, "🎨 Image configurée avec scaling: %p", canvas);
  ESP_LOGI(TAG, "   Dimensions cibles: %ux%u", target_width, target_height);
  
  if (this->camera_) {
    uint16_t cam_w = this->camera_->get_image_width();
    uint16_t cam_h = this->camera_->get_image_height();
    
    ESP_LOGI(TAG, "   Résolution caméra: %ux%u", cam_w, cam_h);
    
    // Calculer le ratio pour garder l'aspect
    float cam_aspect = (float)cam_w / (float)cam_h;
    float target_aspect = (float)target_width / (float)target_height;
    
    uint16_t final_w, final_h;
    
    if (cam_aspect > target_aspect) {
      // Image plus large que la cible - limiter par la largeur
      final_w = target_width;
      final_h = (uint16_t)((float)target_width / cam_aspect);
    } else {
      // Image plus haute que la cible - limiter par la hauteur
      final_h = target_height;
      final_w = (uint16_t)((float)target_height * cam_aspect);
    }
    
    ESP_LOGI(TAG, "   Scaling avec aspect ratio: %ux%u", final_w, final_h);
    
    // Définir la taille de l'objet image (LVGL fera le scaling)
    lv_obj_set_size(this->img_obj_, final_w, final_h);
    
    // Centrer l'image dans l'espace cible
    lv_coord_t x_offset = (target_width - final_w) / 2;
    lv_coord_t y_offset = (target_height - final_h) / 2;
    
    if (x_offset > 0 || y_offset > 0) {
      ESP_LOGI(TAG, "   Centrage: offset X=%d, Y=%d", x_offset, y_offset);
      lv_obj_set_pos(this->img_obj_, x_offset, y_offset);
    }
  } else {
    // Caméra pas encore initialisée, juste définir la taille
    lv_obj_set_size(this->img_obj_, target_width, target_height);
  }
  
  // Reset du flag d'avertissement
  this->canvas_warning_shown_ = false;
  
  ESP_LOGI(TAG, "✅ Image prête avec scaling automatique");
}

}  // namespace lvgl_camera_display
}  // namespace esphome
