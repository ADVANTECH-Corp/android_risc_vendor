# Copyright 2006 The Android Open Source Project

# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    reference-ril.c \
    misc.c \
    serial/atchannel.c \
    serial/at_tok.c \
    serial/ril-serial.c \
    serial/ril-mecontrol.c \
    serial/ril-audio.c \
    serial/ril-dataconnection.c \
    serial/ril-location.c \
    serial/ril-sms.c \
    serial/ril-call.c \
    serial/ril-network.c \
    serial/ril-suppsvc.c \
    serial/ril-pm.c \
    serial/ril-sim.c \
    serial/fcp_parser.c \

LOCAL_SHARED_LIBRARIES := \
    liblog libcutils libutils libril librilutils

# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/

ifeq (foo,foo)
  #build shared library
  LOCAL_SHARED_LIBRARIES += \
      libcutils libutils
  LOCAL_CFLAGS += -DRIL_SHLIB
  LOCAL_MODULE:= libtelit-ril
  include $(BUILD_SHARED_LIBRARY)
else
  #build executable
  LOCAL_SHARED_LIBRARIES += \
      libril
  LOCAL_MODULE:= telit-ril
  include $(BUILD_EXECUTABLE)
endif
