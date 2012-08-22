ifeq ($(BOARD_USES_GPS_PROXY),true)

#==============================================================================
# wrapper library for the gps proxy
#==============================================================================
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := gps.proxy
#$(TARGET_BOOTLOADER_BOARD_NAME)

LOCAL_SHARED_LIBRARIES:= \
	libcutils \
	libutils \
	liblog \
	libdl

LOCAL_STATIC_LIBRARIES := libstc-rpc

LOCAL_SRC_FILES += \
    gps_library.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += hardware/stc/libstc-rpc/include

LOCAL_CFLAGS += \
    -fno-short-enums -DDEBUG -DVERBOSE 

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

include $(BUILD_SHARED_LIBRARY)

#==============================================================================
# daemon executable for the gps proxy
#==============================================================================
include $(CLEAR_VARS)

LOCAL_MODULE:= gps_proxy
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils

LOCAL_STATIC_LIBRARIES := libstc-rpc

ifeq ($(TARGET_ARCH),arm)
LOCAL_SHARED_LIBRARIES += libdl
endif # arm

LOCAL_SRC_FILES += gps_proxy.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += hardware/stc/libstc-rpc/include

include $(BUILD_EXECUTABLE)

endif # BOARD_USES_GPS_PROXY
