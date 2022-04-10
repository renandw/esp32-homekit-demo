PROJECT_NAME = main

CFLAGS += -I$(abspath ../../) -DHOMEKIT_SHORT_APPLE_UUIDS

EXTRA_COMPONENT_DIRS += \
  $(abspath ../../components/common) \
  $(abspath ../../components/esp-idf) \
	$(abspath ../../components/esp-idf-lib) \

include $(IDF_PATH)/make/project.mk
