# Linux Makefile

CFLAGS += -O3 -Wno-psabi -Wa,-march=armv7-a -mtune=cortex-a9 -mfpu=vfpv3 -g

LOCAL_SRC_FILES := imx-vncserver.c
LOCAL_OBJ_FILES := xxhash.o

LOCAL_SHARED_LIBRARIES := -lz -lpthread -ljpeg -lvncserver -lcrypto -lssl

LOCAL_MODULE := imx-vncserver

PKG_DIR := ../../content
PKG_PREFIX := /usr/local/sbin

# build 

GCC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)gcc

###LOCAL_OBJ_FILES := $(subst .c,.o,$(LOCAL_SRC_FILES))

all: $(LOCAL_OBJ_FILES) $(LOCAL_MODULE) clean

$(LOCAL_MODULE): $(LOCAL_OBJ_FILES)
	$(GCC) $(CFLAGS) $(LOCAL_SRC_FILES) $(LOCAL_OBJ_FILES) $(LOCAL_SHARED_LIBRARIES) -o $@
	mv -v $(LOCAL_MODULE) ../../content/usr/local/sbin/

$(LOCAL_OBJ_FILES):
	$(GCC) -c $(CFLAGS) -fpic $(LOCAL_SHARED_LIBRARIES) $(patsubst %.o,%.c,$@)

clean:
	rm -rf $(LOCAL_MODULE)
