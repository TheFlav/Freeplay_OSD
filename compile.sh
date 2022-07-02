rm fp_osd

#wiringPi
#gcc -DUSE_WIRINGPI -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lwiringPi

#gpiod
#gcc -DUSE_GPIOD -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -l:libgpiod.a

#no gpio
#gcc -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/

#Freeplay Zero 2 specific
gcc -DUSE_WIRINGPI -DNO_SIGNAL_FILE -DNO_SIGNAL -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lwiringPi

sudo ./fp_osd

#test all functions based on ./test folder files
#sudo ./fp_osd -osd_test -tinyosd_test -lowbat_test -cputemp_test -cpu_thermal test/thermal_zone0_temp.txt -battery_rsoc test/battery_capacity.txt -battery_voltage test/battery_voltage_now.txt -backlight test/backlight.txt -backlight_max test/backlight_max.txt
