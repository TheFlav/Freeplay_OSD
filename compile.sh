rm fp_osd

#wiringPi
#gcc -DUSE_WIRINGPI -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lwiringPi

#gpiod
gcc -DUSE_GPIOD -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -l:libgpiod.a

#no gpio
#gcc -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/

sudo ./fp_osd

