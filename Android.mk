ifeq ($(BOARD_USES_GPS_PROXY),true)

#==============================================================================
# wrapper library for the gps proxy
#==============================================================================
ifeq (,true)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := gps.$(TARGET_BOOTLOADER_BOARD_NAME)

LOCAL_SHARED_LIBRARIES:= \
	liblog \
    $(BOARD_GPS_LIBRARIES) \

LOCAL_STATIC_LIBRARIES := libstc-rpc

LOCAL_SRC_FILES += \
    gps-library.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += hardware/stc/libstc-rpc/include

LOCAL_CFLAGS += \
    -fno-short-enums 

ifneq ($(BOARD_GPS_BAD_AGPS),)
LOCAL_CFLAGS += \
    -DNO_AGPS
endif

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

include $(BUILD_SHARED_LIBRARY)
endif

#==============================================================================
# daemon executable for the gps proxy
#==============================================================================
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	gps-proxy.c

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils

ifeq ($(TARGET_ARCH),arm)
LOCAL_SHARED_LIBRARIES += libdl
endif # arm

LOCAL_STATIC_LIBRARIES := libstc-rpc

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += hardware/stc/libstc-rpc/include

LOCAL_CFLAGS :=

LOCAL_MODULE:= gps-proxy
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

endif # BOARD_USES_GPS_PROXY
