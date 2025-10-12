# components/mipi_camera_web_server/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT
from esphome.core import coroutine_with_priority

DEPENDENCIES = ["mipi_dsi_cam", "network"]
CODEOWNERS = ["@youkorr"]

CONF_CAMERA_ID = "camera_id"

mipi_camera_web_server_ns = cg.esphome_ns.namespace("mipi_camera_web_server")
MipiCameraWebServer = mipi_camera_web_server_ns.class_(
    "MipiCameraWebServer", cg.Component
)

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDsiCam")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MipiCameraWebServer),
        cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
        cv.Optional(CONF_PORT, default=80): cv.port,
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(60.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))
    cg.add(var.set_port(config[CONF_PORT]))
    
    # Build flags pour PSRAM et JPEG
    cg.add_build_flag("-DBOARD_HAS_PSRAM")# components/mipi_camera_web_server/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT
from esphome.core import coroutine_with_priority

DEPENDENCIES = ["mipi_dsi_cam", "network"]
CODEOWNERS = ["@youkorr"]

CONF_CAMERA_ID = "camera_id"

mipi_camera_web_server_ns = cg.esphome_ns.namespace("mipi_camera_web_server")
MipiCameraWebServer = mipi_camera_web_server_ns.class_(
    "MipiCameraWebServer", cg.Component
)

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDsiCam")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MipiCameraWebServer),
        cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
        cv.Optional(CONF_PORT, default=80): cv.port,
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(60.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))
    cg.add(var.set_port(config[CONF_PORT]))
    
    # Librairie pour encodage JPEG
    cg.add_library("jpegenc", None)
    
    # Build flags pour PSRAM
    cg.add_build_flag("-DBOARD_HAS_PSRAM")# components/mipi_camera_web_server/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT
from esphome.core import coroutine_with_priority

DEPENDENCIES = ["mipi_dsi_cam", "network"]
CODEOWNERS = ["@youkorr"]

CONF_CAMERA_ID = "camera_id"

mipi_camera_web_server_ns = cg.esphome_ns.namespace("mipi_camera_web_server")
MipiCameraWebServer = mipi_camera_web_server_ns.class_(
    "MipiCameraWebServer", cg.Component
)

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDsiCam")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MipiCameraWebServer),
        cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
        cv.Optional(CONF_PORT, default=81): cv.port,
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(60.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))
    cg.add(var.set_port(config[CONF_PORT]))
    
    # Librairies nécessaires
    cg.add_library("ESP Async WebServer", None)
    cg.add_library("AsyncTCP", None)
    cg.add_library("jpegenc", None)
    
    # Build flags
    cg.add_build_flag("-DBOARD_HAS_PSRAM")
