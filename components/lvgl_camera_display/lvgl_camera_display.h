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

  void set_camera(tab5_camera::Tab5Camera *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }

  // Permet de régler l'intervalle d'update, utile pour passer à 30 FPS
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }

  void configure_canvas(lv_obj_t *canvas);

  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  tab5_camera::Tab5Camera *camera_{nullptr};
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  // Intervalle pour mise à jour (en ms) ; 33 ms pour ~30 FPS
  uint32_t update_interval_{33};  // 33ms = 30 FPS
  uint32_t last_update_{0};

  // Variables pour contrôle des logs et mesure FPS réel
  uint32_t frame_count_{0};
  bool first_update_{true};
  bool canvas_warning_shown_{false};

  // Variable pour calcul du FPS réel
  uint32_t last_fps_time_{0};

  void update_canvas_();
};

}  // namespace lvgl_camera_display
}  // namespace esphome
