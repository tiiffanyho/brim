#!/bin/sh
# Runs ON THE PI. Stops supervisor first (so it doesn't restart brim), then the children.
slay -f -s SIGTERM supervisor 2>/dev/null
sleep 1
slay -f brim app_server 2>/dev/null
echo "brim stopped"
