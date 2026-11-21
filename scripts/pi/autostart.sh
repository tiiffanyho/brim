#!/bin/sh
# Runs ON THE PI, once: makes Brim start by itself every time the Pi boots.
#     sudo sh ~/brim/scripts/pi/autostart.sh          (turn on)
#     sudo sh ~/brim/scripts/pi/autostart.sh off      (turn off)
#
# QNX's Raspberry Pi image runs /system/etc/startup/post_startup.sh at boot, and that script ends
# with "exit 0", so our line must go just BEFORE it (anything after exit 0 never runs).
# The line waits 60 s (Wi-Fi and the USB camera need time to appear), then runs start.sh in the
# background (&) so the Pi always finishes booting even if Brim has a problem.
# The camera must be plugged in BEFORE the Pi is powered on.
F=/system/etc/startup/post_startup.sh
RUN_DIR="${BRIM_PI_DIR:-/data/home/qnxuser/brim-run}"        # where setup_pi.sh put Brim
LINE="( sleep 60; sh $RUN_DIR/start.sh ) &   # brim-autostart"
TMP=/tmp/post_startup.new

[ -f "$F" ] || { echo "startup file not found: $F"; exit 1; }
[ -f "$F.brim-backup" ] || cp "$F" "$F.brim-backup"        # keep the original once, for undo

# Copy the file without any old Brim line and without its final "exit 0" ...
grep -v "brim-run/start.sh" "$F" | grep -v "^exit 0$" > "$TMP"
# ... then add our line (unless turning off) and put "exit 0" back at the end.
[ "$1" = "off" ] || echo "$LINE" >> "$TMP"
echo "exit 0" >> "$TMP"
cp "$TMP" "$F" || { echo "could not write $F"; exit 1; }

if [ "$1" = "off" ]; then echo "autostart OFF"; else echo "autostart ON: Brim starts about 1 minute after boot"; fi
tail -2 "$F"
