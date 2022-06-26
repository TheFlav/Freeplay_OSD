rm fp_osd

#wiringPi
#gcc -DUSE_WIRINGPI -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lpthread -lwiringPi

#gpiod
#gcc -DUSE_GPIOD -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lpthread -l:libgpiod.a

#no gpio
#gcc -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lpthread

#Freeplay Zero 2 specific
gcc -DUSE_WIRINGPI -DNO_SIGNAL_FILE -DNO_SIGNAL -o fp_osd fp_osd.c -l:libpng.a -l:libz.a -l:libm.a -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/ -lpthread -lwiringPi

sudo ./fp_osd
