LV2 Audio Plugin Host for VLC
=============================

A VLC module to load LV2 Plugins for audio processing,
Still in development/experimental state.

Install
-------

Compiling these plugin requires the VST Module SDK, gnu-make and a c-compiler
(on debian/ubuntu: libvlccore-dev, build-essential packages).

```bash
git clone git://github.com/x42/vlc-lv2.git
cd vlc-lv2.lv2
make
sudo make plugindir=/usr/lib/`dpkg-architecture -q DEB_HOST_MULTIARCH`/vlc/plugins/ install
```

Usage
-----

* Launch VLC, open the preferences (Tools > Preferences) and Show "All" settings (bottom left).
* Under Audio -> Filters -> LV2, select a plugin.
  Note: currently the channel-count of the plugin needs to match the played file.
* Play an audio-file


Supported Plugins
-----------------

Any LV2 Audio Plugin with a native UI (X11, Cocoa, WindowsUI).

There is no generic control-ui. Plugins which do not provice a GUI or only provide
a toolkit specific UI are not supported.

Supported LV2 Features
----------------------
* LV2:ui, native UI only: X11UI on Linux, CocoaUI on OSX and WindowsUI on Windows.
* LV2 Atom ports (currently at most only 1 atom in, 1 atom output)
* LV2 URI map
* LV2 Worker thread extension
* LV2 State extension
