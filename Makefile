# Device makefile
BUILD_TYPE := release
PLATFORM   := arm

INCLUDES += \
		-DHAVE_LUNA_PREFS=1

LIBS := -L$(LIB_DIR) -ljemalloc_mt -llunaservice -lpthread

include Makefile.inc

#install:
#	mkdir -p $(INSTALL_DIR)/usr/bin
#	install -m 0775 release-arm/LunaSysService $(INSTALL_DIR)/usr/bin/LunaSysService

stage:
	echo "nothing to do"
