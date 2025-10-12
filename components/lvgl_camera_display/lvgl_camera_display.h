#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "../mipi_dsi_cam/mipi_dsi_cam.h"

namespace esphome {
namespace lvgl_camera_display {

class LVGLCameraDisplay : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void set_camera(mipi_dsi_cam::MipiDsiCam *camera) { this->camera_ = camera; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }
  
  // Configuration du canvas - à appeler depuis YAML on_boot
  void configure_canvas(lv_obj_t *canvas);
  
  // Configuration avec dimensions personnalisées (scaling automatique)
  void configure_canvas(lv_obj_t *canvas, uint16_t target_width, uint16_t target_height);
  
  // Vérifier si le canvas est configuré
  bool is_canvas_configured() const { return this->img_obj_ != nullptr; }
  
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  mipi_dsi_cam::MipiDsiCam *camera_{nullptr};
  lv_obj_t *img_obj_{nullptr};  // Utiliser lv_img au lieu de canvas pour le scaling
  lv_img_dsc_t img_dsc_{};      // Description de l'image pour LVGL
  
  // Dimensions cibles pour le scaling
  uint16_t target_width_{0};
  uint16_t target_height_{0};
  bool use_scaling_{false};
  
  uint32_t update_interval_{20};  // 50 FPS par défaut
  uint32_t last_update_{0};
  
  // Statistiques
  uint32_t frame_count_{0};
  uint32_t dropped_frames_{0};
  bool first_update_{true};
  bool canvas_warning_shown_{false};
  
  void update_canvas_();
};

}  // namespace lvgl_camera_display
}  // namespace esphome
