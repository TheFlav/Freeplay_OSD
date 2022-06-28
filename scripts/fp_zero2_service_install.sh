#!/bin/bash

SERVICE_FILE=fp_osd-freeplayzero2.service
if [ ! -f "../service_sample/$SERVICE_FILE" ]; then
    echo "../service_sample/$SERVICE_FILE is missing, abort installation."
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
fi
gcc -DUSE_WIRINGPI -DNO_SIGNAL_FILE -DNO_SIGNAL -o ../fp_osd ../fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lwiringPi

if [ -f "../fp_osd" ]; then
    echo "### Installing service files ###"
    sudo cp ../service_sample/$SERVICE_FILE /lib/systemd/system/$SERVICE_FILE
    sudo systemctl enable $SERVICE_FILE
    sudo systemctl start $SERVICE_FILE

    systemctl is-active --quiet $SERVICE_FILE
    if [ $? -eq 0 ]; then
        echo "Service installed successfully and running."
    else
        echo "Something went wrong, service not running."
    fi
else
    echo "Something went wrong, program failed to compile."
fi
