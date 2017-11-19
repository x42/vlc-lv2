#!/bin/sh
set -e
make
sudo make plugindir=/usr/lib/`dpkg-architecture -q DEB_HOST_MULTIARCH`/vlc/plugins/ "$@"
