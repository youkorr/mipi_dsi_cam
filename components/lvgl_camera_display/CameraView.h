#pragma once
#include "esphome/components/lvgl_camera_display/lvgl_camera_display.h"

class CameraView {
private:
    // Référence au camera display
    lvgl_camera_display::LVGLCameraDisplay* _cam_display{nullptr};
    
    // Objet image LVGL
    lv_obj_t* _camera_img{nullptr};
    
    // État
    bool _is_camera_minimized{true};
    
    // Label de message (optionnel)
    lv_obj_t* _label_msg{nullptr};
   
    // Fonction pour mettre à jour l'affichage
    void update_camera_canvas()
    {
        if (_camera_img == nullptr || _cam_display == nullptr) {
            ESP_LOGE("CameraView", "Image ou camera display non initialisé");
            return;
        }
        
        if (_is_camera_minimized) {
            // ✨ MODE MINIATURE: 760x440 avec scaling automatique
            ESP_LOGI("CameraView", "📸 Mode miniature 760x440");
            
            // Reconfigurer avec scaling
            _cam_display->configure_canvas(_camera_img, 760, 440);
            
            // Positionner (exemple: en haut à droite avec marge)
            lv_obj_set_pos(_camera_img, 500, 20);
            
            // Style: coins arrondis
            lv_obj_set_style_radius(_camera_img, 15, 0);
            
            // Bordure bleue
            lv_obj_set_style_border_width(_camera_img, 2, 0);
            lv_obj_set_style_border_color(_camera_img, lv_color_hex(0x2196F3), 0);
            
        } else {
            // ✨ MODE PLEIN ÉCRAN: 1280x720
            ESP_LOGI("CameraView", "📸 Mode plein écran 1280x720");
            
            // Reconfigurer sans scaling (ou avec dimensions pleines)
            _cam_display->configure_canvas(_camera_img, 1280, 720);
            
            // Position 0,0 (plein écran)
            lv_obj_set_pos(_camera_img, 0, 0);
            
            // Style: pas de radius ni bordure en plein écran
            lv_obj_set_style_radius(_camera_img, 0, 0);
            lv_obj_set_style_border_width(_camera_img, 0, 0);
        }
    }

public:
    // Constructeur
    CameraView() = default;
    
    // Setup initial
    void setup(lvgl_camera_display::LVGLCameraDisplay* cam_display) {
        if (cam_display == nullptr) {
            ESP_LOGE("CameraView", "Camera display est NULL");
            return;
        }
        
        _cam_display = cam_display;
        
        // Créer l'objet Image LVGL (PAS Canvas!)
        _camera_img = lv_img_create(lv_scr_act());
        
        if (_camera_img == nullptr) {
            ESP_LOGE("CameraView", "Échec création image LVGL");
            return;
        }
        
        // Configuration initiale en mode miniature
        update_camera_canvas();
        
        // Créer un label de message (optionnel)
        _label_msg = lv_label_create(lv_scr_act());
        lv_label_set_text(_label_msg, "Camera Ready");
        lv_obj_set_pos(_label_msg, 10, 10);
        lv_obj_set_style_text_color(_label_msg, lv_color_hex(0xFFFFFF), 0);
        
        ESP_LOGI("CameraView", "✅ CameraView setup complete");
    }
    
    // Toggle entre miniature et plein écran
    void toggle_size() {
        _is_camera_minimized = !_is_camera_minimized;
        update_camera_canvas();
        
        // Mettre à jour le label
        if (_label_msg) {
            if (_is_camera_minimized) {
                lv_label_set_text(_label_msg, "📸 Miniature");
            } else {
                lv_label_set_text(_label_msg, "📸 Plein Écran");
            }
        }
    }
    
    // Getter pour l'état
    bool is_minimized() const {
        return _is_camera_minimized;
    }
    
    // Setter pour passer en mode miniature
    void set_minimized(bool minimized) {
        if (_is_camera_minimized != minimized) {
            _is_camera_minimized = minimized;
            update_camera_canvas();
        }
    }
    
    // Setter pour une taille personnalisée
    void set_custom_size(uint16_t width, uint16_t height) {
        if (_camera_img && _cam_display) {
            _cam_display->configure_canvas(_camera_img, width, height);
            
            // Centrer automatiquement
            lv_obj_align(_camera_img, LV_ALIGN_CENTER, 0, 0);
            
            ESP_LOGI("CameraView", "📸 Taille personnalisée: %ux%u", width, height);
        }
    }
    
    // Setter pour la position
    void set_position(int16_t x, int16_t y) {
        if (_camera_img) {
            lv_obj_set_pos(_camera_img, x, y);
        }
    }
    
    // Getter pour l'objet image (pour manipulations avancées)
    lv_obj_t* get_image_object() {
        return _camera_img;
    }
    
    // Afficher/cacher la caméra
    void set_visible(bool visible) {
        if (_camera_img) {
            if (visible) {
                lv_obj_clear_flag(_camera_img, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_camera_img, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    // Vérifier si visible
    bool is_visible() const {
        if (_camera_img) {
            return !lv_obj_has_flag(_camera_img, LV_OBJ_FLAG_HIDDEN);
        }
        return false;
    }
    
    // Mettre à jour le label de message
    void set_message(const char* msg) {
        if (_label_msg) {
            lv_label_set_text(_label_msg, msg);
        }
    }
};
