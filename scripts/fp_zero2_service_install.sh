#!/bin/bash

SERVICE_FILE=fp_osd-freeplayzero2.service #service file to check and install

if [ ! -f "$SERVICE_FILE" ]; then
    echo "ERROR: $SERVICE_FILE service file is missing, abort installation."
    exit 0
fi

if [ -f "/lib/systemd/system/$SERVICE_FILE" ]; then
    echo "### Stopping already running service and uninstalling files ###"
    sudo systemctl stop $SERVICE_FILE
    sudo systemctl disable $SERVICE_FILE
    sudo rm /lib/systemd/system/$SERVICE_FILE
fi

echo "### Installing required libraries ###"
sudo apt update
sudo apt install -y libpng-dev zlib1g-dev libraspberrypi-dev wiringpi

echo "### Compiling program ###"
if [ -f "../fp_osd" ]; then
    sudo rm ../fp_osd
    echo "Already compiled program removed."
fi
gcc -DUSE_WIRINGPI -DNO_SIGNAL_FILE -DNO_SIGNAL -o ../fp_osd ../fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lwiringPi

if [ -f "../fp_osd" ]; then
    echo "Program compiled successfully."
    echo "### Installing service files ###"
    sudo cp $SERVICE_FILE /lib/systemd/system/$SERVICE_FILE
    sudo systemctl enable $SERVICE_FILE
    sudo systemctl start $SERVICE_FILE

    systemctl is-active --quiet $SERVICE_FILE
    if [ $? -eq 0 ]; then
        echo "Service installed successfully and running."
    else
        echo "WARNING: Something went wrong, service not running."
    fi
else
    echo "ERROR: Program failed to compile."
fi
