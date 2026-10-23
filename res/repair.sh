#!/bin/sh
# Repair a mod left behind by NickelHook's fail-safe: if Nickel died inside the
# start-up window, the library stays renamed to *.failsafe and Qt loads that
# copy, which NickelHook then refuses to start. Since v0.2.1 the mod repairs
# this by itself at the next start; this script does the same by hand.
# Run it from NickelMenu with a line like
#   menu_item :main :TouchRemote repair :cmd_output :9000 :sh /mnt/onboard/.adds/touchremote/repair.sh
# in a file under .adds/nm, then restart the Kobo.
dir=/usr/local/Kobo/imageformats
lib=$dir/libtouchremote.so
if [ -e "$lib.failsafe" ]; then
    if [ -e "$lib" ]; then
        rm -f "$lib.failsafe" && echo "Removed the stale fail-safe copy."
    else
        mv "$lib.failsafe" "$lib" && echo "Restored the library from its fail-safe copy."
    fi
    sync
    echo "Restart the Kobo now (hold the power button)."
else
    echo "Nothing to repair: no fail-safe copy present."
fi
ls -l $dir/libtouchremote.so* 2>&1
