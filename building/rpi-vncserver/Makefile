# Linux Makefile

CFLAGS += -O3 -Wno-psabi -I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -L/opt/vc/lib/

LOCAL_SRC_FILES := rpi-vncserver.c
LOCAL_OBJ_FILES := xxhash.o

LOCAL_SHARED_LIBRARIES := -lz -lpthread -ljpeg -lvncserver -lopenmaxil -lbcm_host

LOCAL_MODULE := rpi-vncserver

PKG_DIR := ../../content
PKG_PREFIX := /usr/local/sbin

# build 

GCC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)gcc

all: $(LOCAL_OBJ_FILES) $(LOCAL_MODULE) clean

$(LOCAL_MODULE): $(LOCAL_OBJ_FILES)
	$(GCC) $(CFLAGS) $(LOCAL_SRC_FILES) $(LOCAL_OBJ_FILES) -g $(LOCAL_SHARED_LIBRARIES) -o $@
	mv -v $(LOCAL_MODULE) ../../content/usr/local/sbin/

$(LOCAL_OBJ_FILES):
	$(GCC) -c $(CFLAGS) -fpic $(LOCAL_SHARED_LIBRARIES) $(patsubst %.o,%.c,$@)

clean:
	rm -rf $(LOCAL_MODULE)
