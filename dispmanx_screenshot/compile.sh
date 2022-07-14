rm main
gcc -o main main.c -lbcm_host -L/opt/vc/lib/ -I/opt/vc/include/
./main