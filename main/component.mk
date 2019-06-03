#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

ULP_APP_NAME ?= ulp_$(COMPONENT_NAME)
ULP_S_SOURCES = $(COMPONENT_PATH)/ulp/button_wakeup.S
ULP_EXP_DEP_OBJECTS := sonos_buttons.o
include $(IDF_PATH)/components/ulp/component_ulp_common.mk
