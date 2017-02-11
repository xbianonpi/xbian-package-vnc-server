# Linux Makefile

LOCAL_SRC_FILES := imx-vncserver.c
LOCAL_OBJ_FILES := xxhash.o

LOCAL_SHARED_LIBRARIES := -lz -lpthread -ljpeg -lvncserver -lcrypto -lssl

LOCAL_MODULE := imx-vncserver

PKG_DIR := ../../../content
PKG_PREFIX := /usr/local/sbin

# build 

GCC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)gcc

all: $(LOCAL_OBJ_FILES) $(LOCAL_MODULE)

$(LOCAL_MODULE): $(LOCAL_OBJ_FILES)
	$(GCC) $(CFLAGS) $(LOCAL_SRC_FILES) $(LOCAL_OBJ_FILES) $(LOCAL_SHARED_LIBRARIES) -o $@

$(LOCAL_OBJ_FILES):
	$(GCC) -c $(CFLAGS) -fpic $(LOCAL_SHARED_LIBRARIES) $(patsubst %.o,%.c,$@)

clean:
	rm -rf $(LOCAL_MODULE)

install:
	mkdir -p $(PKG_DIR)$(PKG_PREFIX)
	install -p -m 755 $(LOCAL_MODULE) $(PKG_DIR)$(PKG_PREFIX)
