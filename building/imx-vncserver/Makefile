# Linux Makefile

CFLAGS := -O2 -Wno-psabi -Wa,-march=armv7a -mtune=cortex-a9 -mfpu=vfpv3-d16 -mthumb-interwork -g

LOCAL_SRC_FILES := imx-vncserver.c

LOCAL_SHARED_LIBRARIES := -lz -lpthread -ljpeg -lvncserver

LOCAL_MODULE := imx-vncserver

PKG_DIR := ../../content
PKG_PREFIX := /usr/local/sbin

# build 

GCC := gcc
LD := gcc

LOCAL_OBJ_FILES := $(subst .c,.o,$(LOCAL_SRC_FILES))

all: $(LOCAL_MODULE) clean

$(LOCAL_MODULE): $(LOCAL_OBJ_FILES)
	$(LD) $(CFLAGS) -o $@ $(LOCAL_OBJ_FILES) $(LOCAL_SHARED_LIBRARIES)
	mv $(LOCAL_MODULE) $(PKG_DIR)$(PKG_PREFIX)

$(LOCAL_OBJ_FILES): %.o:%.c
	$(GCC) $(CFLAGS) -c -o $@ $(LOCAL_SRC_FILES)

clean:
	rm -rf $(LOCAL_OBJ_FILES) $(LOCAL_MODULE)
