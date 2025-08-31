cmake_minimum_required(VERSION 3.19)

# WiFi credentials and server URL are now configured dynamically via NVS storage
# No need for compile-time environment variables

if(DEFINED ENV{LOG_DATACHANNEL_MESSAGES})
  add_compile_definitions(LOG_DATACHANNEL_MESSAGES="1")
endif()

# For compatibility with older code that might reference these
# They will not be used in the new dynamic configuration system
if(DEFINED ENV{WIFI_SSID})
  message(STATUS "Note: WIFI_SSID environment variable detected but will not be used")
  message(STATUS "Use the new configuration tool: python esp32_voice_keyboard_config.py")
endif()

if(DEFINED ENV{PIPECAT_SMALLWEBRTC_URL})
  message(STATUS "Note: PIPECAT_SMALLWEBRTC_URL environment variable detected but will not be used")  
  message(STATUS "Use the new configuration tool: python esp32_voice_keyboard_config.py")
endif()

set(COMPONENTS src)
set(EXTRA_COMPONENT_DIRS "src" "components/srtp" "components/peer" "components/esp-libopus")

if(IDF_TARGET STREQUAL linux)
  add_compile_definitions(LINUX_BUILD=1)
  list(APPEND EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/examples/protocols/linux_stubs/esp_stubs
    "components/esp-protocols/common_components/linux_compat/esp_timer"
    "components/esp-protocols/common_components/linux_compat/freertos"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
