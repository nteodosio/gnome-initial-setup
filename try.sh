#!/bin/sh
#If you're on Kinetic, you need to downgrade base-files to Jammy make this work,
#or manually alter /etc/os_release
if ! [ -d build ]; then
    mkdir build
    meson build
fi
ninja -C build || exit 1
if [ "$1" = debug ]; then
    XDG_CURRENT_DESKTOP=ubuntu gdb -ex run --args ./build/gnome-initial-setup/gnome-initial-setup --existing-user
elif [ "$1" = harddebug ]; then
    G_DEBUG=fatal-warnings GTK_THEME=Adwaita XDG_CURRENT_DESKTOP=ubuntu gdb -ex run --args ./build/gnome-initial-setup/gnome-initial-setup --existing-user
else
    XDG_CURRENT_DESKTOP=ubuntu ./build/gnome-initial-setup/gnome-initial-setup --existing-user
fi

