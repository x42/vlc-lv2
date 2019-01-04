LD         ?= ld
CXX        ?= c++
STRIP      ?= strip
PKG_CONFIG ?= pkg-config
INSTALL    ?= install

CXXFLAGS = -g -O2 -Wall -Wextra
LDFLAGS =
LIBS =

ifeq ($(shell $(PKG_CONFIG) --atleast-version=3.0.0 vlc-plugin || echo no), no)
  $(error "VLC module SDK > 3.0 was not found, install libvlccore-dev")
endif

VLC_PLUGIN_CFLAGS := $(shell $(PKG_CONFIG) --cflags vlc-plugin)
VLC_PLUGIN_LIBS := $(shell $(PKG_CONFIG) --libs vlc-plugin)

PREFIX    = /usr/local
libdir    = $(PREFIX)/lib
plugindir = $(libdir)/vlc/plugins

###############################################################################

override CPPFLAGS += -DMODULE_STRING=\"lv2\"
override CXXFLAGS += $(VLC_PLUGIN_CFLAGS)
override LIBS     += $(VLC_PLUGIN_LIBS)

ifeq ($(shell $(PKG_CONFIG) --atleast-version=3.0.0 vlc-plugin && echo yes), yes)
  override CPPFLAGS += -DVLC3API
endif

override CXXFLAGS += -Wno-unused-parameter -Wno-deprecated-declarations

###############################################################################

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  STRIPFLAGS=-x
  LIB_EXT=.dylib
  override LIBS     += -ldl
  override CXXFLAGS += -fPIC -Wno-deprecated
  override CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections
  override LDFLAGS  += -dynamiclib -headerpad_max_install_names -Bsymbolic
else
  STRIPFLAGS=-s
  override LDFLAGS += -Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -shared
  override LDFLAGS += -Wl,-no-undefined
  ifneq ($(XWIN),)
    # cross-compile for windows with mingw, `make XWIN=x86_64-w64-mingw32`
    CC=$(XWIN)-gcc
    CXX=$(XWIN)-g++
    STRIP=$(XWIN)-strip
    LIB_EXT=.dll
    override LIBS     += -lm -lws2_32
    override CXXFLAGS += -mstackrealign
    override CXXFLAGS += -fvisibility=hidden -fdata-sections -ffunction-sections
    override LDFLAGS  += -static-libgcc -static-libstdc++
    override LDFLAGS  += -fvisibility=hidden -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed -Wl,--strip-all
  else
    # other unices (Linux, *BSD)
    LIB_EXT=.so
    override LIBS     += -ldl
    override CXXFLAGS += -fPIC
    override CXXFLAGS += -fvisibility=hidden -fdata-sections -ffunction-sections
    override LDFLAGS  += -static-libgcc -static-libstdc++
    override LDFLAGS  += -fvisibility=hidden -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed -Wl,--strip-all
  endif
endif

###############################################################################

MODULE_SRC= \
  src/lv2plugin.cc \
  src/lv2pluginui.cc \
  src/loadlib.cc \
  src/lv2ttl.cc \
  src/lv2vlc.cc \
  src/state.cc \
  src/worker.cc

MODULE_DEP= \
  src/lv2plugin.h \
  src/loadlib.h \
  src/lv2desc.h \
  src/lv2ttl.h \
  src/ringbuffer.h \
  src/uri_map.h \
  src/worker.h

LV2SRC= \
  local/lib/lilv/collections.c \
  local/lib/lilv/instance.c \
  local/lib/lilv/lib.c \
  local/lib/lilv/node.c \
  local/lib/lilv/plugin.c \
  local/lib/lilv/pluginclass.c \
  local/lib/lilv/port.c \
  local/lib/lilv/query.c \
  local/lib/lilv/scalepoint.c \
  local/lib/lilv/state.c \
  local/lib/lilv/ui.c \
  local/lib/lilv/util.c \
  local/lib/lilv/world.c \
  local/lib/lilv/zix/tree.c \
  local/lib/serd/env.c \
  local/lib/serd/node.c \
  local/lib/serd/reader.c \
  local/lib/serd/string.c \
  local/lib/serd/uri.c \
  local/lib/serd/writer.c \
  local/lib/sord/sord.c \
  local/lib/sord/syntax.c \
  local/lib/sord/zix/btree.c \
  local/lib/sord/zix/digest.c \
  local/lib/sord/zix/hash.c \
  local/lib/sratom/sratom.c

INCLUDES= \
  local/include/lilv_config.h \
  local/include/lilv/lilv.h \
  local/include/serd_config.h \
  local/include/serd/serd.h \
  local/include/sord_config.h \
  local/include/sord/sord.h \
  local/include/sratom/sratom.h \
  local/include/lv2/lv2plug.in

###############################################################################

override CPPFLAGS += -I. -Isrc
override CPPFLAGS += -Ilocal/include/ -Ilocal/lib/sord/ -Ilocal/lib/lilv/

all: liblv2_plugin$(LIB_EXT)

install: all
	mkdir -p -- $(DESTDIR)$(plugindir)/misc
	$(INSTALL) --mode 0755 liblv2_plugin$(LIB_EXT) $(DESTDIR)$(plugindir)/misc

uninstall:
	rm -f $(plugindir)/misc/liblv2_plugin$(LIB_EXT)

clean:
	rm -f -- liblv2_plugin$(LIB_EXT)

liblv2_plugin$(LIB_EXT): $(MODULE_SRC) $(MODULE_DEP) $(LV2SRC) $(INCLUDES) Makefile
	$(CXX) $(CPPFLAGS) \
	  $(CXXFLAGS) \
	  -o $@ \
	  $(MODULE_SRC) \
	  $(LV2SRC) \
	  $(LDFLAGS) $(LIBS)
	$(STRIP) $(STRIPFLAGS) $@

.PHONY: all install uninstall clean
