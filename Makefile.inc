MACHINE ?= "x86"
OBJDIR := $(BUILD_TYPE)-$(MACHINE)

# ------------------------------------------------------------------

SOURCES := \
	Logging.cpp \
	LocalePrefsHandler.cpp \
	Main.cpp \
	PrefsDb.cpp \
	PrefsFactory.cpp \
	TimePrefsHandler.cpp \
	UrlRep.cpp \
	WallpaperPrefsHandler.cpp \
	BuildInfoHandler.cpp \
	Utils.cpp \
	SystemRestore.cpp \
	RingtonePrefsHandler.cpp \
	ImageServices.cpp \
	Mainloop.cpp \
	TimeZoneService.cpp \
	TzParser.cpp \
	BackupManager.cpp \
	Settings.cpp \
	NetworkConnectionListener.cpp \
    JSONUtils.cpp \
    ImageHelpers.cpp

TARGET_OBJS := $(SOURCES:%.cpp=$(OBJDIR)/%.o)
TARGET_OBJS += $(C_SOURCES:%.c=$(OBJDIR)/%.o)

TARGET := $(OBJDIR)/LunaSysService

# ------------------------------------------------------------------

ifeq ("$(BUILD_TYPE)", "debug")
FLAGS_OPT := -O0 -g -fno-exceptions -fno-inline
else
FLAGS_OPT := -O2 -g -fno-exceptions -Wno-error
endif

FLAGS_OPT += -DMACHINE_$(MACHINE)=1 -DQT_PLUGIN
INCLUDES := $(INCLUDES) \
	-I./Src \
	-isystem $(INCLUDE_DIR) \
	-isystem $(INCLUDE_DIR)/QtCore \
	-isystem $(INCLUDE_DIR)/QtGui \
	`pkg-config --cflags glib-2.0 freetype2`

LIBS := $(LIBS) \
	-lglib-2.0 -llunaservice -lcjson -lsqlite3 -luriparser -lluna-prefs -lpbnjson_cpp -lQtCore -lQtGui

LOCAL_CFLAGS := $(CFLAGS) -Wall -Werror $(FLAGS_OPT) $(ARCH_CFLAGS)

LOCAL_LFLAGS := $(LDFLAGS) $(FLAGS_OPT) $(LIBS) 

# ------------------------------------------------------------------

all: setup $(TARGET_OBJS) $(TARGET)

setup:
	@mkdir -p $(OBJDIR)

vpath %.cpp Src
vpath %.c Src

$(TARGET): $(TARGET_OBJS)
	$(CXX) -o $(TARGET) $(TARGET_OBJS) $(LOCAL_LFLAGS)

$(OBJDIR)/%.o: %.cpp
	$(CXX) -MMD $(INCLUDES) $(LOCAL_CFLAGS)  -fno-rtti -c $< -o $@

$(OBJDIR)/%.o: %.c
	$(CC) -MMD $(INCLUDES) $(LOCAL_CFLAGS) -c $< -o $@

-include $(SOURCES:%.cpp=$(OBJDIR)/%.d)

clean:
	rm -rf $(OBJDIR)
