# Compiler definitions
CXX = g++
CXXFLAGS = -Wall -O3 -fdata-sections -ffunction-sections -I./include -I/boot/system/develop/headers/private/shared


# Target binary definitions
GUI_TARGET = HaikuDVR
SERVER_TARGET = dvr_server
VERSION = 1.0.28
PACKAGE_DIR := build/package

# Shared target architectures
UNAME_M := $(shell uname -p)
ifeq ($(UNAME_M), x86)
    CXX = g++-x86 
    ARCH = x86_gcc2
    INCLUDE = -L/boot/system/lib/x86 
else ifeq ($(UNAME_M), x86_64)
    CXX = g++
    ARCH = x86_64
    INCLUDE = -L/boot/system/lib
endif

# Source mapping parameters
GUI_SRCS = HaikuDVR.cpp
GUI_OBJS = $(GUI_SRCS:.cpp=.o)
GUI_RSRCS = HaikuDVR.rsrc

SERVER_SRCS = dvr_server.cpp
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
SERVER_RSRCS = dvr_server.rsrc 

# Shared linking assets
LIBS = -L./lib -lhdhomerun -lbe -ltranslation -lcurl -lnetwork -ltracker -lshared -lsqlite3
RPATH = -Wl,-rpath=$$ORIGIN/lib

# OPTIMIZED: Added garbage collection linking flags and symbol stripping (-s)
LDFLAGS = $(INCLUDE) -Wl,--gc-sections -s

# Master target execution rule
all: $(GUI_TARGET) $(SERVER_TARGET)

# Link the graphical desktop client binary
$(GUI_TARGET): $(GUI_OBJS) $(GUI_RSRCS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(GUI_TARGET) $(GUI_OBJS) $(LIBS) $(RPATH)
	xres -o $(GUI_TARGET) $(GUI_RSRCS)
	mimeset -f $(GUI_TARGET)

# Link the headless background server binary
$(SERVER_TARGET): $(SERVER_OBJS) $(SERVER_RSRCS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(SERVER_TARGET) $(SERVER_OBJS) $(LIBS) $(RPATH)
	xres -o $(SERVER_TARGET) $(SERVER_RSRCS) 
	mimeset -f $(SERVER_TARGET)

# Compile visual layout script components
%.rsrc: %.rdef
	rc -o $@ $<

# General object file compilation hooks
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Deep system cleaning target
clean:
	rm -f *.o *.rsrc $(GUI_TARGET) $(SERVER_TARGET)
	rm -f $(NAME) *.hpkg
	rm -rf build

.PHONY: all clean



release: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(GUI_TARGET)/$(GUI_TARGET)/g' -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(GUI_TARGET).tpl > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
	mkdir -p $(PACKAGE_DIR)/servers
	mkdir -p $(PACKAGE_DIR)/lib
	mkdir -p $(PACKAGE_DIR)/data/launch
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	cp $(GUI_TARGET) $(PACKAGE_DIR)/apps/$(GUI_TARGET)
	cp $(SERVER_TARGET) $(PACKAGE_DIR)/servers/$(SERVER_TARGET)
	cp lib/libhdhomerun.so $(PACKAGE_DIR)/lib/libhdhomerun.so
	cp dvr_server.launch $(PACKAGE_DIR)/data/launch/dvr_server
	ln -s ../apps/$(GUI_TARGET) $(PACKAGE_DIR)/bin/$(GUI_TARGET)
	ln -s ../../../../apps/$(GUI_TARGET) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(GUI_TARGET)
	package create -C $(PACKAGE_DIR) $(GUI_TARGET)-$(VERSION)-1-$(ARCH).hpkg	



