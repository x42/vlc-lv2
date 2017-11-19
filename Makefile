PREFIX = /usr/local
LD = ld
CXX = c++
PKG_CONFIG = pkg-config
INSTALL = install
CXXFLAGS = -g -O2 -Wall -Wextra
LDFLAGS =
LIBS =
VLC_PLUGIN_CFLAGS := $(shell $(PKG_CONFIG) --cflags vlc-plugin)
VLC_PLUGIN_LIBS := $(shell $(PKG_CONFIG) --libs vlc-plugin)

libdir = $(PREFIX)/lib
plugindir = $(libdir)/vlc/plugins

override CPPFLAGS += -DPIC -I. -Isrc
override CXXFLAGS += -fPIC
override LDFLAGS += -Wl,-no-undefined,-z,defs

override CPPFLAGS += -DMODULE_STRING=\"lv2\"
override CXXFLAGS += $(VLC_PLUGIN_CFLAGS)
override LIBS += $(VLC_PLUGIN_LIBS)

TARGETS = liblv2_plugin.so

all: liblv2_plugin.so

install: all
	mkdir -p -- $(DESTDIR)$(plugindir)/misc
	$(INSTALL) --mode 0755 liblv2_plugin.so $(DESTDIR)$(plugindir)/misc

install-strip:
	$(MAKE) install INSTALL="$(INSTALL) -s"

uninstall:
	rm -f $(plugindir)/misc/liblv2_plugin.so

clean:
	rm -f -- liblv2_plugin.so

mostlyclean: clean

liblv2_plugin.so: src/lv2vlc.c
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

.PHONY: all install install-strip uninstall clean mostlyclean
