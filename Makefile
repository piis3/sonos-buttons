#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#
CFLAGS += -D LOG_LOCAL_LEVEL=ESP_LOG_DEBUG -DF_CPU=240000000L -DARDUINO_ESP32_THING -DARDUINO_ARCH_ESP32 -DARDUINO_BOARD=\"ESP32_THING\" -DARDUINO_VARIANT=\"esp32thing\" -DESP32 
PROJECT_NAME := sonos-buttons

include $(IDF_PATH)/make/project.mk

