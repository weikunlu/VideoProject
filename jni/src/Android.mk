LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := ffmpegutils

SDL_PATH := ../SDL

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../SDL/include \
	$(LOCAL_PATH)/../ffmpeg/include \

LOCAL_SRC_FILES := $(SDL_PATH)/src/main/android/SDL_android_main.cpp \
	native.c

LOCAL_SHARED_LIBRARIES := SDL

LOCAL_LDLIBS := -L$(ANDROID_NDK)/platforms/android-14/arch-arm/usr/lib -L$(LOCAL_PATH)/../ffmpeg/lib -lavformat -lavcodec -lavdevice -lavfilter -lavutil -lswscale -llog -ljnigraphics -landroid -lz -ldl -lgcc
 
include $(BUILD_SHARED_LIBRARY)