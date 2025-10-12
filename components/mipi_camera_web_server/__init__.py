import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base
from esphome.const import CONF_ID, CONF_MODE
from esphome.core import coroutine_with_priority

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["mipi_dsi_cam"]

CONF_CAMERA_ID = "camera_id"

mipi_camera_web_server_ns = cg.esphome_ns.namespace("mipi_camera_web_server")
MipiCameraWebServer = mipi_camera_web_server_ns.class_(
    "MipiCameraWebServer", cg.Component
)

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDsiCam")

# Modes de streaming comme dans esp32_camera_web_server
MODE_STREAM = "stream"
MODE_SNAPSHOT = "snapshot"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MipiCameraWebServer),
            cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
            cv.Optional(CONF_MODE, default=MODE_STREAM): cv.enum(
                {MODE_STREAM: MODE_STREAM, MODE_SNAPSHOT: MODE_SNAPSHOT}, 
                upper=False
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(web_server_base.WEBSERVER_BASE_SCHEMA),
)


@coroutine_with_priority(65.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))
    
    # Configuration du mode
    if config[CONF_MODE] == MODE_STREAM:
        cg.add(var.set_mode(True))  # True = stream
    else:
        cg.add(var.set_mode(False))  # False = snapshot only
    
    # Intégration avec web_server_base
    paren = await cg.get_variable(config[web_server_base.CONF_WEB_SERVER_BASE_ID])
    cg.add(paren.add_handler(var))
    
    # Librairie pour encodage JPEG
    cg.add_library("jpegenc", None)
