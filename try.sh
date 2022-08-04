#!/bin/sh
#If you're on Kinetic, you need to downgrade base-files to Jammy make this work,
#or manually alter /etc/os_release
if ! [ -d tweakui ]; then
    echo "Error, no tweakui directory!"
    exit 1
fi
if ! [ -d build ]; then
    mkdir build
    meson build
fi
cp gis-ubuntupro-page.ui tweakui
cd tweakui; patch -p0 < template.patch
cd -
cp tweakui/gis-ubuntupro-page.ui gnome-initial-setup/pages/ubuntu-pro/gis-ubuntupro-page.ui
ninja -C build || exit 1
GTK_THEME=Adwaita XDG_CURRENT_DESKTOP=ubuntu ./build/gnome-initial-setup/gnome-initial-setup --existing-user

