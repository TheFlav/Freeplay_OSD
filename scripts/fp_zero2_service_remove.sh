#!/bin/bash

SERVICE_FILE=fp_osd-freeplayzero2.service
if [ ! -f "/lib/systemd/system/$SERVICE_FILE" ]; then
    echo "/lib/systemd/system/$SERVICE_FILE is missing, nothing to uninstall."
    exit 0
fi

echo "### Uninstalling service ###"
sudo systemctl stop fp_osd-freeplayzero2.service
sudo systemctl disable fp_osd-freeplayzero2.service
sudo rm /lib/systemd/system/fp_osd-freeplayzero2.service
