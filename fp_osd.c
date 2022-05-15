/*
FreeplayTech On-screen (heads-up) display overlay

This program is meant to run as daemon.
When successufully started, pid.txt file is created, containing main program PID (deleted when program closes).
Depending on default configuration, sending SIGUSR1 or SIGUSR2 signal to program will display the OSD.
If properly configured, it does also display a low battery icon based on a GPIO input or use of a power_supply compatible battery gauge IC.

Default settings mainly target FreeplayTech products but all variables you may want to play with can be set with program arguments.

Important notes:
- This program target Raspberry Pi platforms as it rely on Dispmanx API.
- Tested on Pi3, Zero 2.

Very early version, still under developpement.

Require libpng-dev, zlib1g-dev, libraspberrypi-dev(?).
Depending of wanted GPIO support (is so): wiringpi, libgpiod-dev.
Note on wiringpi: You may need to clone and compile for unofficial github repository as official WiringPi ended development, please refer to: https://github.com/PinkFreud/WiringPi

Credits goes where its due:
- This project is inspirated by Retropie-open-OSD (https://github.com/vascofazza/Retropie-open-OSD).
- Contain modified version of drawStringRGB(), drawCharRGB(), setPixelRGB(), loadPng() functions and fontset from Raspidmx (https://github.com/AndrewFromMelbourne/raspidmx).
*/

#include "fp_osd.h"


//time functions
static double get_time_double(void){ //get time in double (seconds), takes around 82 microseconds to run
    struct timespec tp; int result = clock_gettime(CLOCK_MONOTONIC, &tp);
    if (result == 0) {return tp.tv_sec + (double)tp.tv_nsec/1e9;}
    return -1.; //failed
}

//print functions
#ifndef print_stderr
    #define print_stderr(fmt, ...) do {fprintf(stderr, "%lf: %s:%d: %s(): " fmt, get_time_double() - program_start_time , __FILE__, __LINE__, __func__, ##__VA_ARGS__);} while (0) //Flavor: print advanced debug to stderr
#endif

#ifndef print_stdout
    #define print_stdout(fmt, ...) do {fprintf(stdout, "%lf: %s:%d: %s(): " fmt, get_time_double() - program_start_time , __FILE__, __LINE__, __func__, ##__VA_ARGS__);} while (0) //Flavor: print advanced debug to stderr
#endif


//raspidmx functions
static void raspidmx_setPixelRGBA32(void* buffer, int buffer_width, int32_t x, int32_t y, uint32_t color){ //modified version from Raspidmx
    if (buffer == NULL){return;}
    uint32_t *line = (uint32_t *)(buffer) + (y * buffer_width) + x; *line = color;
}

static int32_t raspidmx_drawCharRGBA32(void* buffer, int buffer_width, int buffer_height, int32_t x, int32_t y, uint8_t c, uint8_t* font_ptr, uint32_t color, uint32_t* bg_color){ //modified version from Raspidmx, return end position of printed char
    if (buffer == NULL){return x;}
    bool fill_bg = (bg_color != NULL);
    for (int j=0; j < RASPIDMX_FONT_HEIGHT; j++){
        int32_t tmp_y = y + j;
        if (tmp_y < 0 || tmp_y > buffer_height-1){continue;} //overflow
        uint8_t byte = *(font_ptr + c * RASPIDMX_FONT_HEIGHT + j);
        if (byte != 0 || (byte == 0 && fill_bg)){
            for (int i=0; i < RASPIDMX_FONT_WIDTH; ++i){
                int32_t tmp_x = x + i;
                if (tmp_x < 0 || tmp_x > buffer_width-1){break;} //overflow
                if ((byte >> (RASPIDMX_FONT_WIDTH - i - 1)) & 1){raspidmx_setPixelRGBA32(buffer, buffer_width, tmp_x, tmp_y, color);
                } else if (fill_bg){raspidmx_setPixelRGBA32(buffer, buffer_width, tmp_x, tmp_y, *bg_color);} //fill background
            }
        }
    }
    return x + RASPIDMX_FONT_WIDTH;
}

static int32_t raspidmx_drawStringRGBA32(void* buffer, int buffer_width, int buffer_height, int32_t x, int32_t y, const char* string, uint8_t* font_ptr, uint32_t color, uint32_t* bg_color){ //modified version from Raspidmx, return end position of printed string
    if (string == NULL || buffer == NULL){return x;}
    int32_t x_first = x, x_last = x;
    while (*string != '\0'){
        if (*string == '\n'){x = x_first; y += RASPIDMX_FONT_HEIGHT;
        } else if (x < buffer_width){
            x = raspidmx_drawCharRGBA32(buffer, buffer_width, buffer_height, x, y, *string, font_ptr, color, bg_color);
            if (x > x_last){x_last = x;}
        }
        ++string;
    }
    return x_last;
}

//dispmanx specific
static void buffer_fill(void* buffer, uint32_t width, uint32_t height, uint32_t rgba_color){ //fill buffer with given color
    if (buffer == NULL){return;}
    uint32_t *ptr = (uint32_t *)buffer, size = width * height;
    for (uint32_t i=0; i < size; i++){ptr[i] = rgba_color;}
}

static void buffer_rectangle_fill(void* buffer, uint32_t width, uint32_t height, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba_color){ //fill rectangle with given color
    if (buffer == NULL){return;}
    uint32_t *ptr = (uint32_t *)buffer;
    for (int32_t ly = (y<0)?0:y; ly <= ((y+h>height-1)?height-1:y+h); ly++){
        uint32_t ptr_y_shift = ly * width;
        for (int32_t lx = (x<0)?0:x; lx <= ((x+w>width-1)?width-1:x+w); lx++){*(ptr + ptr_y_shift + (uint32_t)lx) = rgba_color;}
    }
}

static void buffer_horizontal_line(void* buffer, uint32_t width, uint32_t height, int32_t x1, int32_t x2, int32_t y, uint32_t rgba_color){ //draw horizontal line
    if (buffer == NULL || y < 0 || y > height-1){return;}
    uint32_t *ptr = (uint32_t *)buffer;
    uint32_t ptr_y_shift = y * width;
    for (int32_t lx = (x1<0)?0:x1; lx <= ((x2>width-1)?width-1:x2); lx++){*(ptr + ptr_y_shift + (uint32_t)lx) = rgba_color;}
}

static DISPMANX_RESOURCE_HANDLE_T dispmanx_resource_create_from_png(char* filename, VC_RECT_T* image_rect_ptr){ //create dispmanx ressource from png file, return 0 on failure, ressource handle on success
	FILE* filehandle = fopen(filename, "rb");
	if (filehandle == NULL){print_stderr("failed to read '%s'.\n", filename); return 0;} else {print_stderr("'%s' opened.\n", filename);}

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL); //allocate and initialize a png_struct structure
    if (png_ptr == NULL){print_stderr("failed to init png_struct structure.\n"); fclose(filehandle); return 0;}

	png_infop info_ptr = png_create_info_struct(png_ptr); //allocate and initialize a png_info structure
	if (info_ptr == NULL){print_stderr("failed to init png_info structure (invalid png file?).\n"); png_destroy_read_struct(&png_ptr, 0, 0); fclose(filehandle); return 0;}
	if (setjmp(png_jmpbuf(png_ptr))){print_stderr("setjmp png_jmpbuf failed.\n"); png_destroy_read_struct(&png_ptr, &info_ptr, 0); fclose(filehandle); return 0;}

	png_init_io(png_ptr, filehandle); //initialize input/output for the PNG file
	png_read_info(png_ptr, info_ptr); //read the PNG image information

    int width = png_get_image_width(png_ptr, info_ptr), height = png_get_image_height(png_ptr, info_ptr); //dimensions
    if (width == 0 || height == 0){print_stderr("failed to get image size.\n"); png_destroy_read_struct(&png_ptr, &info_ptr, 0); fclose(filehandle); return 0;}

    png_byte color_type = png_get_color_type(png_ptr, info_ptr), bit_depth = png_get_bit_depth(png_ptr, info_ptr); //color type and depth
    print_stderr("resolution: %dx%d depth:%dbits.\n", width, height, bit_depth*png_get_channels(png_ptr, info_ptr));

	double gamma = .0; if (png_get_gAMA(png_ptr, info_ptr, &gamma)){png_set_gamma(png_ptr, 2.2, gamma);} //gamma correction, useful?

    //convert to rgb/rgba
    if (color_type == PNG_COLOR_TYPE_PALETTE){png_set_palette_to_rgb(png_ptr); //convert palette to rgb
    } else if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA){ //grayscale
        if (bit_depth < 8){png_set_expand_gray_1_2_4_to_8(png_ptr);} //extend to 8bits depth
        png_set_gray_to_rgb(png_ptr); //convert grayscale to rgb
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)){png_set_tRNS_to_alpha(png_ptr);} //convert tRNS chunks to alpha channels
    if (bit_depth == 16){png_set_scale_16(png_ptr);} //scale down 16bits to 8bits depth
	png_read_update_info(png_ptr, info_ptr); //update png info structure

    //read image into memory
    int pitch = ALIGN_TO_16(width) * ((color_type & PNG_COLOR_MASK_ALPHA)?4:3);
    png_bytepp row_pointers = malloc(height * sizeof(png_bytep));
    if (row_pointers == NULL){print_stderr("malloc failed.\n"); fclose(filehandle); png_destroy_read_struct(&png_ptr, &info_ptr, 0); return 0;}

    uint8_t *buffer = calloc(1, pitch * ALIGN_TO_16(height));
    if (buffer == NULL){print_stderr("calloc failed.\n"); fclose(filehandle); free(row_pointers); png_destroy_read_struct(&png_ptr, &info_ptr, 0); return 0;}

    for (png_uint_32 j = 0 ; j < height ; ++j){row_pointers[j] = buffer + (j * pitch);}
    png_read_image(png_ptr, row_pointers);
    fclose(filehandle); free(row_pointers); png_destroy_read_struct(&png_ptr, &info_ptr, 0); //png cleanup

    //dispmanx
    VC_IMAGE_TYPE_T vc_type = (color_type & PNG_COLOR_MASK_ALPHA) ? VC_IMAGE_RGBA32 : VC_IMAGE_RGB888; //vc rgba or rgb format
    uint32_t vc_image_ptr; //no use in this case
    DISPMANX_RESOURCE_HANDLE_T resource = vc_dispmanx_resource_create(vc_type, width, height, &vc_image_ptr);
    if (resource == 0){print_stderr("failed to create dispmanx resource.\n"); free(buffer); return 0;}

    vc_dispmanx_rect_set(image_rect_ptr, 0, 0, width, height); //set rectangle struct data
    if (vc_dispmanx_resource_write_data(resource, vc_type, pitch, buffer, image_rect_ptr) != 0){
        print_stderr("failed to write dispmanx resource.\n");
        vc_dispmanx_resource_delete(resource); free(buffer); return 0;
    } else {free(buffer);}

    print_stderr("dispmanx resource created, handle:%u.\n", resource);
    return resource;
}


//low battery related
static bool lowbat_gpio_init(void){ //init low battery gpio things
    if (lowbat_gpio < 0){print_stderr("invalid gpio low battery pin:%d.\n", lowbat_gpio); return false;} //disabled
    #if defined(USE_WIRINGPI) //wiringPi library
        #define WIRINGPI_CODES 1 //allow error code return
        int err;
        if ((err = wiringPiSetupGpio()) < 0){ //use BCM numbering
            print_stderr("failed to initialize wiringPi, errno:%d.\n", -err);
        } else {
            pinMode(lowbat_gpio, INPUT);
            print_stderr("using wiringPi to poll GPIO%d.\n", lowbat_gpio);
            return true;
        }
    #elif defined(USE_GPIOD) //gpiod library
        if ((gpiod_chip = gpiod_chip_open_lookup("0")) == NULL){
            print_stderr("gpiod_chip_open_lookup failed.\n"); return false;
        } else {
            sprintf(gpiod_consumer_name, "%s %d OSD", program_name, lowbat_gpio);
            if ((gpiod_input_line = gpiod_chip_get_line(gpiod_chip, lowbat_gpio)) == NULL){
                print_stderr("gpiod_chip_get_line failed, pin:%d, consumer:'%s'.\n", lowbat_gpio, gpiod_consumer_name);
            } else if (gpiod_line_request_both_edges_events(gpiod_input_line, gpiod_consumer_name) < 0){
                print_stderr("gpiod_line_request_both_edges_events failed. chip:%s(%s), consumer:'%s'.\n", gpiod_chip_name(gpiod_chip), gpiod_chip_label(gpiod_chip), gpiod_consumer_name);
            } else if ((gpiod_fd = gpiod_line_event_get_fd(gpiod_input_line)) < 0){
                print_stderr("gpiod_line_event_get_fd failed. errno:%d, consumer:'%s'.\n", -gpiod_fd, gpiod_consumer_name);
            } else {
                fcntl(gpiod_fd, F_SETFL, fcntl(gpiod_fd, F_GETFL, 0) | O_NONBLOCK); //set gpiod fd to non blocking
                print_stderr("using libGPIOd to poll GPIO%d, chip:%s(%s), consumer:'%s'.\n", lowbat_gpio, gpiod_chip_name(gpiod_chip), gpiod_chip_label(gpiod_chip), gpiod_consumer_name);
                return true;
            }
        }
    #endif
    return false; //disabled
}

static bool lowbat_gpio_check(void){ //check if low battery gpio is high, false if not (incl. disabled)
    if (!lowbat_gpio_enabled || lowbat_gpio < 0){return false;}
    bool ret = false;
    #ifdef USE_WIRINGPI //wiringPi library
        ret = digitalRead(lowbat_gpio) > 0;
        if (lowbat_gpio_reversed){ret = !ret;} //reverse input
    #elif defined(USE_GPIOD) //gpiod library
        if (gpiod_fd >= 0){
            int gpiod_ret = gpiod_line_get_value(gpiod_input_line);
            if (gpiod_ret >= 0){if (lowbat_gpio_reversed){ret = !gpiod_ret;} else {ret = gpiod_ret;}} //reverse/normal input
        }
    #endif
    return ret;
}

static bool lowbat_sysfs(void){ //read sysfs power_supply battery capacity, return true if threshold, false if under or file not found
    FILE *filehandle = fopen(battery_rsoc_path, "r");
    if (filehandle != NULL){
        char buffer[5]; fgets(buffer, 5, filehandle); fclose(filehandle);
        int percent = atoi(buffer); int_constrain(&percent, 0, 100);
        if (percent <= lowbat_limit){return true;}
    }
    return false;
}


//osd related
void osd_header_build_element(DISPMANX_RESOURCE_HANDLE_T resource, DISPMANX_ELEMENT_HANDLE_T *element, DISPMANX_UPDATE_HANDLE_T update, uint32_t osd_width, uint32_t osd_height, uint32_t x, uint32_t y, uint32_t width, uint32_t height){
    if (osd_header_buffer_ptr == NULL){
        print_stderr("creating header osd bitmap buffer.\n");
        osd_header_buffer_ptr = calloc(1, osd_width * osd_height * 4);
        if (osd_header_buffer_ptr != NULL){buffer_fill(osd_header_buffer_ptr, osd_width, osd_height, osd_color_bg);} //buffer reset
    }

    if (osd_header_buffer_ptr != NULL){ //valid bitmap buffer
        char buffer[256] = {'\0'}; FILE *filehandle;
        bool draw_update = false;
        uint32_t text_column_left = 0, text_column_right = osd_width;

        //system: cpu load data before time to limit displayed time stuttering
        int32_t cpu_load = 0; static double cpu_load_add = 0; static uint32_t cpu_loops = 0;
        filehandle = fopen("/proc/stat", "r");
        if (filehandle != NULL){
            uint64_t a0=0, a1=0, a2=0, a3=0, b0=1, b1=0, b2=0, b3=0; //b0=1 to avoid div/0 if failed
            fscanf(filehandle, "%*s %llu %llu %llu %llu", &a0, &a1, &a2, &a3); usleep(200000); rewind(filehandle);
            fscanf(filehandle, "%*s %llu %llu %llu %llu", &b0, &b1, &b2, &b3); fclose(filehandle);
            double cpu_load_tmp = (double)((b0-a0)+(b1-a1)+(b2-a2)) / ((b0-a0)+(b1-a1)+(b2-a2)+(b3-a3)) * 100;
            if (cpu_load_tmp < 0.){cpu_load_tmp = 0.;} else if (cpu_load_tmp > 100.){cpu_load_tmp = 100.;}
            cpu_load_add += cpu_load_tmp; cpu_loops++;
        }

        //rtc/ntc/uptime data
        static uint32_t uptime_value_prev = UINT32_MAX; //redraw when seconds changes
        static bool time_rtc = false, time_ntc = false;
        if (!time_rtc && access(rtc_path, F_OK) == 0){time_rtc = true; //rtc check
        } else if (!time_ntc && access("/usr/bin/timedatectl", F_OK) == 0){ //ntc check
            filehandle = popen("timedatectl timesync-status", "r");
            if(filehandle != NULL){
                while(fgets(buffer, 255, filehandle) != NULL){
                    if (strstr(buffer, "Packet count") != NULL){uint32_t tmp = 0; sscanf(buffer, "%*[^0123456789]%d", &tmp); time_ntc = (tmp > 0); break;}
                }
                pclose(filehandle);
            }
        }

        if (time_rtc || time_ntc){
            time_t now = time(0); struct tm *ltime = localtime(&now);
            if (ltime->tm_sec != uptime_value_prev){
                strftime(buffer, 255, "%X", ltime);
                uptime_value_prev = ltime->tm_sec; draw_update = true;
            }
        } else { //fall back on uptime
            uint32_t uptime_value = 0;
            filehandle = fopen("/proc/uptime","r"); if (filehandle != NULL){fscanf(filehandle, "%u", &uptime_value);fclose(filehandle);}
            if (uptime_value != uptime_value_prev){
                uint32_t uptime_h = uptime_value/3600; uint16_t uptime_m = (uptime_value-(uptime_h*3600))/60; uint8_t uptime_s = uptime_value-(uptime_h*3600)-(uptime_m*60);
                sprintf(buffer, "%02u:%02u:%02u", uptime_h, uptime_m, uptime_s);
                uptime_value_prev = uptime_value; draw_update = true;
            }
        }

        if (draw_update){ //redraw
            buffer_fill(osd_header_buffer_ptr, osd_width, osd_height, osd_color_bg); //buffer reset

            //clock: right side (done first because buffer)
            text_column_right -= strlen(buffer) * RASPIDMX_FONT_WIDTH;
            raspidmx_drawStringRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, buffer, raspidmx_font_ptr, osd_color_text, NULL);
            text_column_right -= RASPIDMX_FONT_WIDTH;
            raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, 1, osd_icon_font_ptr, osd_color_separator, NULL); //separator

            //battery: left side
            int32_t batt_rsoc = -1; double batt_voltage = -1.;
            filehandle = fopen(battery_rsoc_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &batt_rsoc); fclose(filehandle);} //rsoc
            filehandle = fopen(battery_volt_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%lf", &batt_voltage); fclose(filehandle); batt_voltage /= battery_volt_divider;} //voltage
            //batt_rsoc = 100; batt_voltage = 4.195;
            if (batt_rsoc > -1 || batt_voltage > 0){
                uint32_t tmp_color = osd_color_text;
                if (batt_rsoc > 0){if (batt_rsoc < 10){tmp_color = osd_color_crit;} else if (batt_rsoc < 25){tmp_color = osd_color_warn;}
                } else if (batt_voltage > 0.){if (batt_voltage < 3.4){tmp_color = osd_color_crit;} else if (batt_voltage < 3.55){tmp_color = osd_color_warn;}}
                
                if (batt_rsoc < 0){sprintf(buffer, "%.3lfv", batt_voltage); //invalid rsoc, voltage only
                } else if (batt_voltage < 0.){sprintf(buffer, "%3d%%", batt_rsoc); //invalid voltage, rsoc only
                } else {sprintf(buffer, "%3d%% %.2lfv", batt_rsoc, batt_voltage);} //both

                text_column_left = raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_left, 0, 7, osd_icon_font_ptr, tmp_color, NULL) + 2; //battery icon
                text_column_left = raspidmx_drawStringRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_left, 0, buffer, raspidmx_font_ptr, tmp_color, NULL);
                text_column_left = raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_left, 0, 1, osd_icon_font_ptr, osd_color_separator, NULL); //separator
            }

            //cpu: left side
            int32_t cpu_temp = -1;
            filehandle = fopen(cpu_thermal_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &cpu_temp); fclose(filehandle); cpu_temp /= cpu_thermal_divider;} //temp
            cpu_load = (int32_t)(cpu_load_add / cpu_loops); cpu_load_add = cpu_loops = 0; //load
            if (cpu_temp > -1 || cpu_load > -1){
                uint32_t tmp_color = osd_color_text;
                if (cpu_temp > -1){
                    if (cpu_temp > 85){tmp_color = osd_color_crit;} else if (cpu_temp > 75){tmp_color = osd_color_warn;}
                    sprintf(buffer, "%dC %3d%%", cpu_temp, cpu_load);
                } else {sprintf(buffer, "%3d%%", cpu_load);}

                text_column_left = raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_left, 0, 2, osd_icon_font_ptr, tmp_color, NULL) + 2; //cpu icon
                text_column_left = raspidmx_drawStringRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_left, 0, buffer, raspidmx_font_ptr, tmp_color, NULL);
                text_column_left = raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_left, 0, 1, osd_icon_font_ptr, osd_color_separator, NULL); //separator
            }

            //backlight: right side
            int32_t backlight = -1, backlight_max = -1;
            filehandle = fopen(backlight_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &backlight); fclose(filehandle);}
            filehandle = fopen(backlight_max_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &backlight_max); fclose(filehandle);}
            if (backlight > -1){
                if (backlight_max < 1){sprintf(buffer, "%d", backlight);
                } else {sprintf(buffer, "%.0lf%%", ((double)backlight/backlight_max)*100);}

                text_column_right -= strlen(buffer) * RASPIDMX_FONT_WIDTH;
                raspidmx_drawStringRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, buffer, raspidmx_font_ptr, osd_color_text, NULL);
                text_column_right -= RASPIDMX_FONT_WIDTH + 2;
                raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, 8, osd_icon_font_ptr, osd_color_text, NULL); //backlight icon
                text_column_right -= RASPIDMX_FONT_WIDTH;
                raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, 1, osd_icon_font_ptr, osd_color_separator, NULL); //separator
            }

            //wifi:right side
            
            if (access("/sbin/iw", F_OK) == 0){
                int32_t wifi_speed = 0, wifi_signal = 0;
                struct ifaddrs *ifap, *ifa;
                if (getifaddrs(&ifap) == 0){
                    for (ifa = ifap; ifa; ifa = ifa->ifa_next){
                        if (!(ifa->ifa_flags & IFF_LOOPBACK) && ifa->ifa_addr->sa_family == AF_INET && ifa->ifa_addr){
                            sprintf(buffer, "iw dev %s link 2> /dev/null", ifa->ifa_name); //build commandline
                            filehandle = popen(buffer, "r");
                            if(filehandle != NULL){
                                while(fgets(buffer, 255, filehandle) != NULL){
                                    if(wifi_signal == 0 && strstr(buffer, "signal") != NULL){sscanf(buffer, "%*[^0123456789]%d", &wifi_signal); //signal
                                    }else if(wifi_speed == 0 && strstr(buffer, "bitrate") != NULL){sscanf(buffer, "%*[^0123456789]%d", &wifi_speed);} //speed
                                    if (wifi_signal != 0 && wifi_speed != 0){break;}
                                }
                                pclose(filehandle);
                            }
                            if (wifi_speed != 0){break;}
                        }
                    }
                }
                freeifaddrs(ifap);
            
                if (wifi_speed > 0){
                    const int32_t wifi_signal_steps[2] = {30,60}, wifi_speed_steps[2] = {5,38}; //critical, warn limits
                    uint32_t tmp_color = osd_color_text;
                    if (wifi_speed < wifi_speed_steps[0]){tmp_color = osd_color_crit;} else if (wifi_speed < wifi_speed_steps[1]){tmp_color = osd_color_warn;}
                    sprintf(buffer, "%d", wifi_speed);

                    text_column_right -= 2 * RASPIDMX_FONT_WIDTH;
                    raspidmx_drawStringRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, "\5\6", osd_icon_font_ptr, tmp_color, NULL);
                    text_column_right -= strlen(buffer) * RASPIDMX_FONT_WIDTH;
                    raspidmx_drawStringRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, buffer, raspidmx_font_ptr, tmp_color, NULL);

                    if (wifi_signal > 0){
                        tmp_color = osd_color_text;
                        if (wifi_signal > wifi_signal_steps[1]){tmp_color = osd_color_crit;} else if (wifi_signal > wifi_signal_steps[0]){tmp_color = osd_color_warn;}
                    }

                    text_column_right -= RASPIDMX_FONT_WIDTH + 2;
                    raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, 4, osd_icon_font_ptr, tmp_color, NULL); //wifi icon
                    text_column_right -= RASPIDMX_FONT_WIDTH;
                    raspidmx_drawCharRGBA32(osd_header_buffer_ptr, osd_width, osd_height, text_column_right, 0, 1, osd_icon_font_ptr, osd_color_separator, NULL); //separator
                }
            }

            //line between left and right separator
            buffer_horizontal_line(osd_header_buffer_ptr, osd_width, osd_height, text_column_left - RASPIDMX_FONT_WIDTH/2, text_column_right + RASPIDMX_FONT_WIDTH/2, osd_height/2 - 1, osd_color_separator); 

            VC_RECT_T osd_rect; vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width, osd_height);
            if (vc_dispmanx_resource_write_data(resource, VC_IMAGE_RGBA32, osd_width * 4, osd_header_buffer_ptr, &osd_rect) != 0){
                print_stderr("failed to write dispmanx resource.\n");
            } else {
                vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width << 16, osd_height << 16);
                VC_RECT_T osd_rect_dest; vc_dispmanx_rect_set(&osd_rect_dest, x, y, width, height);
                if (*element == 0){
                    *element = vc_dispmanx_element_add(update, dispmanx_display, osd_layer + 3, &osd_rect_dest, resource, &osd_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);
                    if (*element == 0){print_stderr("failed to add element.\n");}
                } else {vc_dispmanx_element_modified(update, *element, &osd_rect_dest);}
            }
        }
    } else {print_stderr("calloc failed.\n");} //failed to allocate buffer
}

void osd_build_element(DISPMANX_RESOURCE_HANDLE_T resource, DISPMANX_ELEMENT_HANDLE_T *element, DISPMANX_UPDATE_HANDLE_T update, uint32_t osd_width, uint32_t osd_height, uint32_t x, uint32_t y, uint32_t width, uint32_t height){
    if (osd_buffer_ptr == NULL){
        print_stderr("creating osd bitmap buffer.\n");
        osd_buffer_ptr = calloc(1, osd_width * osd_height * 4);
        if (osd_buffer_ptr != NULL){buffer_fill(osd_buffer_ptr, osd_width, osd_height, osd_color_bg);} //buffer reset
    }

    if (osd_buffer_ptr != NULL){ //valid bitmap buffer
        char buffer[256] = {'\0'}; FILE *filehandle;
        uint32_t text_column = osd_text_padding, text_y = osd_text_padding;
        bool draw_update = false;

        //system: cpu load data before time to limit displayed time stuttering
        int32_t cpu_load = 0; static double cpu_load_add = 0; static uint32_t cpu_loops = 0;
        filehandle = fopen("/proc/stat", "r");
        if (filehandle != NULL){
            uint64_t a0=0, a1=0, a2=0, a3=0, b0=1, b1=0, b2=0, b3=0; //b0=1 to avoid div/0 if failed
            fscanf(filehandle, "%*s %llu %llu %llu %llu", &a0, &a1, &a2, &a3); usleep(200000); rewind(filehandle);
            fscanf(filehandle, "%*s %llu %llu %llu %llu", &b0, &b1, &b2, &b3); fclose(filehandle);
            double cpu_load_tmp = (double)((b0-a0)+(b1-a1)+(b2-a2)) / ((b0-a0)+(b1-a1)+(b2-a2)+(b3-a3)) * 100;
            if (cpu_load_tmp < 0.){cpu_load_tmp = 0.;} else if (cpu_load_tmp > 100.){cpu_load_tmp = 100.;}
            cpu_load_add += cpu_load_tmp; cpu_loops++;
        }

        //rtc/ntc/uptime data
        static uint32_t uptime_value_prev = UINT32_MAX; //redraw when seconds changes
        static bool time_rtc = false, time_ntc = false;

        if (!time_rtc && access(rtc_path, F_OK) == 0){time_rtc = true; //rtc check
        } else if (!time_ntc && access("/usr/bin/timedatectl", F_OK) == 0){ //ntc check
            filehandle = popen("timedatectl timesync-status", "r");
            if(filehandle != NULL){
                while(fgets(buffer, 255, filehandle) != NULL){
                    if (strstr(buffer, "Packet count") != NULL){uint32_t tmp = 0; sscanf(buffer, "%*[^0123456789]%d", &tmp); time_ntc = (tmp > 0); break;}
                }
                pclose(filehandle);
            }
        }

        if (time_rtc || time_ntc){
            time_t now = time(0); struct tm *ltime = localtime(&now);
            if (ltime->tm_sec != uptime_value_prev){
                char buffer0[128]; strftime(buffer0, 127, "%X %x", ltime);
                sprintf(buffer, "%s: %s", time_rtc?"RTC":"NTC", buffer0);
                uptime_value_prev = ltime->tm_sec; draw_update = true;
            }
        } else { //fall back on uptime
            uint32_t uptime_value = 0;
            filehandle = fopen("/proc/uptime","r"); if (filehandle != NULL){fscanf(filehandle, "%u", &uptime_value);fclose(filehandle);}
            if (uptime_value != uptime_value_prev){
                uint32_t uptime_h = uptime_value/3600; uint16_t uptime_m = (uptime_value-(uptime_h*3600))/60; uint8_t uptime_s = uptime_value-(uptime_h*3600)-(uptime_m*60);
                sprintf(buffer, "Uptime: %02u:%02u:%02u", uptime_h, uptime_m, uptime_s);
                uptime_value_prev = uptime_value; draw_update = true;
            }
        }

        if (draw_update){ //redraw
            buffer_fill(osd_buffer_ptr, osd_width, osd_height, osd_color_bg); //buffer reset

            //rtc or uptime data
            raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);
            text_y += osd_text_padding + RASPIDMX_FONT_HEIGHT;

            //battery gauge
            int32_t batt_rsoc = -1; double batt_voltage = -1.;
            filehandle = fopen(battery_rsoc_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &batt_rsoc); fclose(filehandle);} //rsoc
            filehandle = fopen(battery_volt_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%lf", &batt_voltage); fclose(filehandle); batt_voltage /= battery_volt_divider;} //voltage
            if (batt_rsoc > -1 || batt_voltage > 0){
                strcpy(buffer, "Battery: "); char buffer0[16] = {'\0'};

                uint32_t tmp_color = osd_color_text;
                if (batt_rsoc > 0){if (batt_rsoc < 10){tmp_color = osd_color_crit;} else if (batt_rsoc < 25){tmp_color = osd_color_warn;}
                } else if (batt_voltage > 0.){if (batt_voltage < 3.4){tmp_color = osd_color_crit;} else if (batt_voltage < 3.55){tmp_color = osd_color_warn;}}

                if (batt_rsoc < 0){sprintf(buffer0, "%.3lfv", batt_voltage); //invalid rsoc, voltage only
                } else if (batt_voltage < 0.){sprintf(buffer0, "%d%%", batt_rsoc); //invalid voltage, rsoc only
                } else {sprintf(buffer0, "%d%% (%.3lfv)", batt_rsoc, batt_voltage);} //both
                strcat(buffer, buffer0);
                raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, tmp_color, &osd_color_text_bg);
                text_y += osd_text_padding + RASPIDMX_FONT_HEIGHT;
            }

            //system: cpu temperature
            int32_t cpu_temp = -1;
            filehandle = fopen(cpu_thermal_path, "r");
            if (filehandle != NULL){fscanf(filehandle, "%d", &cpu_temp); fclose(filehandle); cpu_temp /= cpu_thermal_divider;}

            //system: memory
            static int32_t memory_total = 0; int32_t memory_free = -1, memory_used = -1;
            filehandle = fopen("/proc/meminfo", "r");
            if (filehandle != NULL){
                int32_t free = -1, buffers = -1, chached = -1;
                while (fgets (buffer, 255, filehandle) != NULL){
                    if (memory_total == 0 && strstr(buffer, "MemTotal") != NULL){ //no need to recheck total once done
                        sscanf(buffer, "%*[^0123456789]%d", &memory_total);
                        memory_total /= memory_divider;
                    }
                    if (free == -1 && strstr(buffer, "MemFree") != NULL){sscanf(buffer, "%*[^0123456789]%d", &free);}
                    if (buffers == -1 && strstr(buffer, "Buffers") != NULL){sscanf(buffer, "%*[^0123456789]%d", &buffers);}
                    if (chached == -1 && strstr(buffer, "Cached") != NULL){sscanf(buffer, "%*[^0123456789]%d", &chached);}
                    if (memory_total != 0 && free != -1 && buffers != -1 && chached != -1){break;}
                }
                fclose(filehandle);
                memory_free = (free + buffers + chached) / memory_divider;
                memory_used = memory_total - memory_free;
            }

            //system: gpu memory
            static int32_t gpu_memory_total = 0;
            int32_t gpu_memory_free = -1, gpu_memory_used = -1;
            {
                static char* gpu_mem_cmd[4] = {"malloc_total", "reloc_total", "malloc", "reloc"}; int32_t memory[4] = {0};
                for (int i=0; i<4; i++){
                    if (gpu_mem_cmd[i] != NULL && vc_gencmd(buffer, 255, "get_mem %s", gpu_mem_cmd[i]) == 0){
                        if (strstr(buffer, gpu_mem_cmd[i]) != NULL){sscanf(buffer, "%*[^0123456789]%d", &memory[i]);}
                    }
                }

                if (gpu_memory_total == 0 && memory[0] + memory[1] > 0){ //total: malloc_total + reloc_total
                    gpu_memory_total = memory[0] + memory[1];
                    gpu_mem_cmd[0] = gpu_mem_cmd[1] = NULL; //no need to recheck total once done
                }
                
                if (gpu_memory_total > 0){
                    gpu_memory_free = memory[2] + memory[3]; //free: malloc + reloc
                    gpu_memory_used = gpu_memory_total - gpu_memory_free;
                }
            }

            //system display
            cpu_load = (int32_t)(cpu_load_add / cpu_loops); cpu_load_add = cpu_loops = 0;
            if (cpu_temp > -1 || cpu_load > -1 || memory_total > -1 || gpu_memory_total > -1){
                raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, "System:", raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);
                text_column = osd_text_padding * 2 + RASPIDMX_FONT_WIDTH * 7;

                if (cpu_temp > -1 || cpu_load > -1){
                    uint32_t tmp_color = osd_color_text;
                    if (cpu_temp > -1){
                        if (cpu_temp > 85){tmp_color = osd_color_crit;} else if (cpu_temp > 75){tmp_color = osd_color_warn;}
                        sprintf(buffer, "CPU: %dC (%d%% load)", cpu_temp, cpu_load);
                    } else {sprintf(buffer, "CPU: %d%%", cpu_load);}
                    raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, tmp_color, &osd_color_text_bg);
                    text_y += RASPIDMX_FONT_HEIGHT;
                }

                //ram
                if (memory_total > 0){
                    int32_t memory_load = memory_used * 100 / memory_total;
                    if (memory_load < 0){memory_load = 0;} else if (memory_load > 100){memory_load = 100;}
                    uint32_t tmp_color = (memory_load>95)?osd_color_warn:osd_color_text;
                    sprintf(buffer, "RAM: %d/%dM (%d%% used)\n", memory_used, memory_total, memory_load);
                    raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, tmp_color, &osd_color_text_bg);
                    text_y += RASPIDMX_FONT_HEIGHT;
                }

                //gpu memory
                if (gpu_memory_total > 0){
                    int32_t gpu_memory_load = gpu_memory_used * 100 / gpu_memory_total;
                    uint32_t tmp_color = (gpu_memory_load>95)?osd_color_warn:osd_color_text;
                    sprintf(buffer, "GPU: %d/%dM (%d%% used)\n", gpu_memory_used, gpu_memory_total, gpu_memory_load);
                    raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, tmp_color, &osd_color_text_bg);
                    text_y += RASPIDMX_FONT_HEIGHT;
                }

                text_y += osd_text_padding; text_column = osd_text_padding;
            }

            //backlight
            int32_t backlight = -1, backlight_max = -1;
            filehandle = fopen(backlight_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &backlight); fclose(filehandle);}
            filehandle = fopen(backlight_max_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &backlight_max); fclose(filehandle);}
            if (backlight > -1){
                sprintf(buffer, "Backlight: %d", backlight);
                if (backlight_max > -1){char buffer0[11]; sprintf(buffer0, "/%d", backlight_max); strcat(buffer, buffer0);}
                raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);
                text_y += osd_text_padding + RASPIDMX_FONT_HEIGHT;
            }

            //network
            #define network_data_limit 10
            typedef struct {
                uint8_t count;
                struct osd_if_struct {char name[IF_NAMESIZE]; char ipv4[16]; /*char ipv6[40];*/ int speed, signal; bool up;} interface[network_data_limit];
            } osd_network_data_t;
            osd_network_data_t osd_network_data = {0};

            struct ifaddrs *ifap, *ifa;
            if (getifaddrs(&ifap) == 0){
                for (ifa = ifap; ifa; ifa = ifa->ifa_next){
                    if (!(ifa->ifa_flags & IFF_LOOPBACK) && ifa->ifa_addr->sa_family == AF_INET && ifa->ifa_addr){
                        char* ip = inet_ntoa(((struct sockaddr_in *) ifa->ifa_addr)->sin_addr);
                        struct osd_if_struct *ptr = &osd_network_data.interface[osd_network_data.count];
                        strncpy(ptr->name, ifa->ifa_name, IF_NAMESIZE-1);
                        strncpy(ptr->ipv4, ip, 15);
                        ptr->up = ifa->ifa_flags & IFF_UP;
                        osd_network_data.count++;
                        if (osd_network_data.count > network_data_limit){break;}
                    }
                }
            }
            freeifaddrs(ifap);

            //wifi link speed and signal
            if (access("/sbin/iw", F_OK) == 0){
                for (int i=0; i<osd_network_data.count; i++){
                    int* tmp_speed = &osd_network_data.interface[i].speed;
                    int* tmp_signal = &osd_network_data.interface[i].signal;
                    sprintf(buffer, "iw dev %s link 2> /dev/null", osd_network_data.interface[i].name); //build commandline
                    filehandle = popen(buffer, "r");
                    if(filehandle != NULL){
                        while(fgets(buffer, 255, filehandle) != NULL){
                            if(*tmp_signal == 0 && strstr(buffer, "signal") != NULL){sscanf(buffer, "%*[^0123456789]%d", tmp_signal); //signal
                            }else if(*tmp_speed == 0 && strstr(buffer, "bitrate") != NULL){sscanf(buffer, "%*[^0123456789]%d", tmp_speed);} //speed
                            if (*tmp_signal != 0 && *tmp_speed != 0){break;}
                        }
                        pclose(filehandle);
                    }
                }
            }

            if (osd_network_data.count){
                raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, "Network:", raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);
                text_column = osd_text_padding * 2 + RASPIDMX_FONT_WIDTH * 8;
                for (int i=0; i<osd_network_data.count; i++){
                    struct osd_if_struct *ptr = &osd_network_data.interface[i];
                    sprintf(buffer, "%s: %s", ptr->name, (ptr->ipv4[0]!='\0') ? ptr->ipv4 : "Unknown");
                    raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);
                    text_y += RASPIDMX_FONT_HEIGHT;
                    if (ptr->speed != 0 || ptr->signal != 0){
                        uint32_t column_back = text_column;
                        text_column += (strlen(ptr->name) + 2) * RASPIDMX_FONT_WIDTH;
                        if (ptr->speed != 0 && ptr->signal != 0){sprintf(buffer, "%dMbits, %ddBm", ptr->speed, -(ptr->signal));
                        } else if (ptr->speed != 0){sprintf(buffer, "%dMbits", ptr->speed);
                        } else {sprintf(buffer, "%ddBm", ptr->signal);}
                        raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);
                        text_y += RASPIDMX_FONT_HEIGHT;
                        text_column = column_back;
                    }
                }
                text_y += osd_text_padding; text_column = osd_text_padding;
            }

            VC_RECT_T osd_rect; vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width, osd_height);
            if (vc_dispmanx_resource_write_data(resource, VC_IMAGE_RGBA32, osd_width * 4, osd_buffer_ptr, &osd_rect) != 0){
                print_stderr("failed to write dispmanx resource.\n");
            } else {
                vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width << 16, osd_height << 16);
                VC_RECT_T osd_rect_dest; vc_dispmanx_rect_set(&osd_rect_dest, x, y, width, height);
                if (*element == 0){
                    *element = vc_dispmanx_element_add(update, dispmanx_display, osd_layer + 1, &osd_rect_dest, resource, &osd_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);
                    if (*element == 0){print_stderr("failed to add element.\n");}
                } else {vc_dispmanx_element_modified(update, *element, &osd_rect_dest);}
            }
        }
    } else {print_stderr("calloc failed.\n");} //failed to allocate buffer
}


//integer manipulation functs
int int_constrain(int* val, int min, int max){ //limit int value to given (incl) min and max value, return 0 if val within min and max, -1 under min, 1 over max
    int ret = 0;
    if (*val < min){*val = min; ret = -1;} else if (*val > max){*val = max; ret = 1;}
    return ret;
}


//generic
static bool html_to_uint32_color(char* html_color, uint32_t* rgba){ //convert html color (3/4 or 6/8 hex) to uint32_t (alpha, blue, green, red)
    int len = strlen(html_color);
    if (!(len==3 || len==4 || len==6 || len==8)){ //invalid format
        print_stderr("invalid html color input '%s', length:%d (expect 3,4 for one hex per color. 6,8 for 2 hex).\n", html_color, len);
        *rgba = 0; return false;
    }

    bool hex1 = (len <= 4);
    if (!(len == 4 || len == 8)){ //add alpha
        if (hex1){html_color[3] = '0'; html_color[4] = '\0';} else {html_color[6] = html_color[7] = '0'; html_color[8] = '\0';}
    }

    for (int i=0; i<4; i++){
        char buffer[3]={'\0'}; unsigned int buffer_int = 0;
        if (hex1){buffer[0] = buffer[1] = html_color[i]; //one hex per color
        } else {buffer[0] = html_color[2*i]; buffer[1] = html_color[2*i+1];} //2 hex per color
        sscanf(buffer, "%x", &buffer_int);
        if (i==3){buffer_int = 255 - buffer_int;} //reverse alpha
        *rgba |= (uint32_t)buffer_int << ((8 * i));
    }

    print_stderr("html_color:'%s', uint32:'%x', len:%d, hex per color:%d.\n", html_color, *rgba, len, hex1?1:2);
    return true;
}

static void tty_signal_handler(int sig){ //handle signal func
    if (debug){print_stderr("DEBUG: signal received: %d.\n", sig);}
    if (sig != SIGUSR1 && sig != SIGUSR2){kill_requested = true; return;}
    if (sig == SIGUSR1 && osd_start_time < 0){osd_start_time = get_time_double(); return;} //full osd start time
    if (sig == SIGUSR2 && osd_header_start_time < 0){osd_header_start_time = get_time_double(); return;} //header osd
}

static void program_close(void){ //regroup all close functs
    if (already_killed){return;}
    if (strlen(pid_path) > 0){remove(pid_path);} //delete pid file
    if (osd_buffer_ptr != NULL){free(osd_buffer_ptr);} //free osd buffer
    if (osd_header_buffer_ptr != NULL){free(osd_header_buffer_ptr);} //free osd header buffer
    if (dispmanx_display != 0){vc_dispmanx_display_close(dispmanx_display); print_stderr("dispmanx freed.\n");}
    bcm_host_deinit(); //deinit bcm host when program closes
    already_killed = true;
}

static void program_get_path(char** args, char* path, char* program){ //get current program path based on program argv or getcwd if failed
    char tmp_path[PATH_MAX], tmp_subpath[PATH_MAX];
    struct stat file_stat = {0};
    strcpy(tmp_path, args[0]); if (args[0][0]=='.'){strcpy(path, ".\0");}
    char *tmpPtr = strtok(tmp_path, "/");
    while (tmpPtr != NULL) {
        sprintf(tmp_subpath, "%s/%s", path, tmpPtr);
        if (stat(tmp_subpath, &file_stat) == 0){
            if (S_ISDIR(file_stat.st_mode) != 0){strcpy(path, tmp_subpath);} else {strcpy(program, tmpPtr);} //folder/file
        }
        tmpPtr = strtok(NULL, "/");
    }
    if (strcmp(path, "./.") == 0){getcwd(path, PATH_MAX);}
    chdir(path); //useful?
    if (debug){print_stderr("program path:'%s'.\n", path);}
}

static void program_usage(void){ //display help
    fprintf(stderr,
    "Path: %s\n"
    "Version: %s\n"
    "Dev: %s\n\n"
    , program_path, program_version, dev_webpage);

    fprintf(stderr, "Arguments:\n\t-h or -help: show arguments list.\n");

    fprintf(stderr,"Low battery management:\n");
#if defined(USE_WIRINGPI) || defined(USE_GPIOD)
    fprintf(stderr,
    "\t-lowbat_gpio <PIN> (-1 to disable. Default:%d).\n"
    "\t-lowbat_gpio_reversed <0-1> (1 for active low. Default:%d).\n"
    , lowbat_gpio, lowbat_gpio_reversed?1:0);
#endif
    fprintf(stderr,
    "\t-battery_rsoc <PATH> (file containing battery percentage. Default:'%s').\n"
    "\t-battery_voltage <PATH> (file containing battery voltage. Default:'%s').\n"
    "\t-battery_volt_divider <NUM> (voltage divider to get voltage. Default:'%u').\n"
    "\t-lowbat_pos tl/tr/bl/br (top left,right, bottom left,right. Default:%s).\n"
    "\t-lowbat_width <1-100> (icon width, percent of screen width. Default:%d).\n"
    "\t-lowbat_limit <0-90> (threshold, used with -battery_rsoc. Default:%d).\n"
    "\t-lowbat_blink <0.1-10> (blink interval in sec. Default:%.1lf).\n"
    "\t-lowbat_test (force display of low battery icon, for test purpose).\n"
    , battery_rsoc_path, battery_volt_path, battery_volt_divider, lowbat_pos_str, lowbat_width_percent, lowbat_limit, lowbat_blink);

    fprintf(stderr,
    "\nOSD display:\n"
    "\t-display <0-255> (Dispmanx display. Default:%u).\n"
    "\t-layer <NUM> (Dispmanx layer. Default:%u).\n"
    "\t-timeout <1-20> (Hide OSD after given duration. Default:%d).\n"
    "\t-check <1-120> (check rate in hz. Default:%d).\n"
    "\t-signal_file <PATH> (useful if you can't send signal to program. Should only contain '0', SIGUSR1 or SIGUSR2 value.).\n"
    "\t-osd_test (full OSD display, for test purpose).\n"
    , display_number, osd_layer, osd_timeout, osd_check_rate);

    fprintf(stderr,
    "\nOSD styling:\n"
    "\t-bg_color <RGB,RGBA> (background color. Default:%s).\n"
    "\t-text_color <RGB,RGBA> (text color. Default:%s).\n"
    "\t-warn_color <RGB,RGBA> (warning text color. Default:%s).\n"
    "\t-crit_color <RGB,RGBA> (critical text color. Default:%s).\n"
    "Note: <RGB,RGBA> uses html format (excl. # char.), allow both 1 or 2 hex per channel.\n"
    "\t-max_lines <1-999> (absolute lines count limit on screen. Default:%d).\n"
    "\t-text_padding <0-100> (text distance (px) to screen border. Default:%d).\n"
    , osd_color_bg_str, osd_color_text_str, osd_color_warn_str, osd_color_crit_str, osd_max_lines, osd_text_padding);

    fprintf(stderr,
    "\nHeader OSD specific:\n"
    "\t-header_position <t/b> (top, bottom. Default:%s).\n"
    "\t-header_height <1-100> (OSD height, percent of screen height. Default:%d).\n"
    "\t-osd_header_test (Tiny OSD display, for test purpose).\n"
    , osd_header_pos_str, osd_header_height_percent);

    fprintf(stderr,
    "\nOSD data:\n"
    "\t-rtc <PATH> (if invalid, uptime will be used. Default:'%s').\n"
    "\t-cpu_thermal <PATH> (file containing CPU temperature. Default:'%s').\n"
    "\t-backlight <PATH> (file containing backlight current value. Default:'%s').\n"
    "\t-backlight_max <PATH> (file containing backlight maximum value. Default:'%s').\n"
    , rtc_path, cpu_thermal_path, backlight_path, backlight_max_path);

    fprintf(stderr,
    "\nProgram:\n"
    "\t-debug <0-1> (enable stderr debug output. Default:%d).\n"
    , debug?1:0);
}

int main(int argc, char *argv[]){
    program_start_time = get_time_double(); //program start time, used for detailed outputs

    program_get_path(argv, program_path, program_name); //get current program path and filename

    //program args parse, plus some failsafes
    for(int i=1; i<argc; i++){
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0){program_usage(); return EXIT_SUCCESS;

        //Low battery management
#if defined(USE_WIRINGPI) || defined(USE_GPIOD)
        } else if (strcmp(argv[i], "-lowbat_gpio") == 0){lowbat_gpio = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-lowbat_gpio_reversed") == 0){lowbat_gpio_reversed = atoi(argv[++i]) > 0;
#endif
        } else if (strcmp(argv[i], "-battery_rsoc") == 0){strncpy(battery_rsoc_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-battery_voltage") == 0){strncpy(battery_volt_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-battery_volt_divider") == 0){battery_volt_divider = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-lowbat_pos") == 0){strncpy(lowbat_pos_str, argv[++i], sizeof(lowbat_pos_str));
        } else if (strcmp(argv[i], "-lowbat_width") == 0){lowbat_width_percent = atoi(argv[++i]);
            if (int_constrain(&lowbat_width_percent, 1, 100) != 0){print_stderr("invalid -lowbat_width argument, reset to '%d', allow from '1' to '100' (incl.)\n", lowbat_width_percent);}
        } else if (strcmp(argv[i], "-lowbat_limit") == 0){lowbat_limit = atoi(argv[++i]);
            if (int_constrain(&lowbat_limit, 0, 90) != 0){print_stderr("invalid -lowbat_limit argument, reset to '%d', allow from '0' to '90' (incl.)\n", lowbat_limit);}
        } else if (strcmp(argv[i], "-lowbat_blink") == 0){double tmp = atof(argv[++i]);
            if (tmp < 0.1 || tmp > 10.){print_stderr("invalid -lowbat_blink argument, reset to '%.1lf', allow from '1' to '10' (incl.)\n", lowbat_blink);} else {lowbat_blink = tmp;}
        } else if (strcmp(argv[i], "-lowbat_test") == 0){lowbat_test = true; print_stderr("low battery icon will be displayed until program closes\n");

        //OSD display
        } else if (strcmp(argv[i], "-display") == 0){display_number = atoi(argv[++i]);
            if (int_constrain(&display_number, 0, 255) != 0){print_stderr("invalid -display argument, reset to '%d', allow from '0' to '255' (incl.)\n", display_number);}
        } else if (strcmp(argv[i], "-layer") == 0){osd_layer = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-timeout") == 0){osd_timeout = atoi(argv[++i]);
            if (int_constrain(&osd_timeout, 1, 20) != 0){print_stderr("invalid -timeout argument, reset to '%d', allow from '1' to '20' (incl.)\n", osd_timeout);}
        } else if (strcmp(argv[i], "-check") == 0){osd_check_rate = atoi(argv[++i]);
            if (int_constrain(&osd_check_rate, 1, 120) != 0){print_stderr("invalid -check argument, reset to '%d', allow from '1' to '120' (incl.)\n", osd_check_rate);}
        } else if (strcmp(argv[i], "-signal_file") == 0){strncpy(signal_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-osd_test") == 0){osd_test = true; print_stderr("full OSD will be displayed until program closes\n");

        //OSD styling
        } else if (strcmp(argv[i], "-bg_color") == 0){strncpy(osd_color_bg_str, argv[++i], sizeof(osd_color_bg_str));
        } else if (strcmp(argv[i], "-text_color") == 0){strncpy(osd_color_text_str, argv[++i], sizeof(osd_color_text_str));
        } else if (strcmp(argv[i], "-warn_color") == 0){strncpy(osd_color_warn_str, argv[++i], sizeof(osd_color_warn_str));
        } else if (strcmp(argv[i], "-crit_color") == 0){strncpy(osd_color_crit_str, argv[++i], sizeof(osd_color_crit_str));
        } else if (strcmp(argv[i], "-max_lines") == 0){osd_max_lines = atoi(argv[++i]);
            if (int_constrain(&osd_max_lines, 1, 999) != 0){print_stderr("invalid -max_lines argument, reset to '%d', allow from '1' to '999' (incl.)\n", osd_max_lines);}
        } else if (strcmp(argv[i], "-text_padding") == 0){osd_text_padding = atoi(argv[++i]);
            if (int_constrain(&osd_text_padding, 0, 100) != 0){print_stderr("invalid -text_padding argument, reset to '%d', allow from '0' to '100' (incl.)\n", osd_text_padding);}

        //Tiny OSD specific
        } else if (strcmp(argv[i], "-header_position") == 0){strncpy(osd_header_pos_str, argv[++i], sizeof(osd_header_pos_str));
        } else if (strcmp(argv[i], "-header_height") == 0){osd_header_height_percent = atoi(argv[++i]);
            if (int_constrain(&osd_header_height_percent, 1, 100) != 0){print_stderr("invalid -header_height argument, reset to '%d', allow from '1' to '100' (incl.)\n", osd_header_height_percent);}
        } else if (strcmp(argv[i], "-osd_header_test") == 0){osd_header_test = true; print_stderr("tiny OSD will be displayed until program closes\n");

        //OSD data
        } else if (strcmp(argv[i], "-rtc") == 0){strncpy(rtc_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-cpu_thermal") == 0){strncpy(cpu_thermal_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-backlight") == 0){strncpy(backlight_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-backlight_max") == 0){strncpy(backlight_max_path, argv[++i], PATH_MAX-1);

        //Program
        } else if (strcmp(argv[i], "-debug") == 0){debug = atoi(argv[++i]) > 0;
        }
    }
    
    //pid file
    int pid = (int)getpid();
    if (pid > 0){
        sprintf(pid_path, "%s/pid.txt", program_path);
        FILE *filehandle = fopen(pid_path, "w");
        if (filehandle != NULL) {
            fprintf(filehandle, "%d", pid); fclose(filehandle);
            print_stderr("%s set to '%d'\n", pid_path, pid);
        } else {pid_path[0] = '\0';} //failed to write pid file
    }

    //tty signal handling
    signal(SIGINT, tty_signal_handler); //ctrl-c
    signal(SIGTERM, tty_signal_handler); //SIGTERM from htop or other, SIGKILL not work as program get killed before able to handle
    signal(SIGABRT, tty_signal_handler); //failure
    signal(SIGUSR1, tty_signal_handler); //use signal SIGUSR1 as trigger to display full OSD
    signal(SIGUSR2, tty_signal_handler); //use signal SIGUSR2 as trigger to display header OSD
    atexit(program_close); at_quick_exit(program_close); //run on program exit

    //bcm init
    bcm_host_init();

    //get display handle
    dispmanx_display = vc_dispmanx_display_open(display_number);
    if (dispmanx_display == 0){print_stderr("FATAL: vc_dispmanx_display_open(%d) failed, returned %d.\n", display_number, dispmanx_display); return EXIT_FAILURE;
    } else {print_stderr("dispmanx display %d, handle:%u.\n", display_number, dispmanx_display);}

    //get display infos
    DISPMANX_MODEINFO_T dispmanx_display_info; uint32_t display_width = 0, display_height = 0;
    if (vc_dispmanx_display_get_info(dispmanx_display, &dispmanx_display_info) != 0){
        print_stderr("FATAL: vc_dispmanx_display_get_info() failed.\n"); return EXIT_FAILURE;
    } else {
        display_width = dispmanx_display_info.width, display_height = dispmanx_display_info.height;
        print_stderr("display info: ");
        fprintf(stderr, "width:%d, ", display_width);
        fprintf(stderr, "height:%d, ", display_height);
        fprintf(stderr, "format:%d:", dispmanx_display_info.input_format);
        switch(dispmanx_display_info.input_format){case 1: fputs("RGB888.\n", stderr); break; case 2: fputs("RGB565.\n", stderr); break; default: fputs("INVALID.\n", stderr);}
    }

    //update handle
    DISPMANX_UPDATE_HANDLE_T dispmanx_update = vc_dispmanx_update_start(0);
    if (dispmanx_update == 0){print_stderr("FATAL: vc_dispmanx_update_start() test failed.\n"); return EXIT_FAILURE;}
    if (vc_dispmanx_update_submit_sync(dispmanx_update) != 0){print_stderr("FATAL: vc_dispmanx_update_submit_sync(%u) test failed.\n", dispmanx_update); return EXIT_FAILURE;}
    print_stderr("dispmanx test update successful.\n");

    //convert html colors to usable colors
    if (!html_to_uint32_color(osd_color_bg_str, &osd_color_bg)){print_stderr("warning, invalid -bg_color argument.\n"); //background raw color
    } else { //text color backgound with half transparent to increase text contrast
        osd_color_text_bg = osd_color_bg;
        uint8_t *osd_color_text_bg_ptr = (uint8_t*)&osd_color_text_bg;
        *(osd_color_text_bg_ptr+3) += (255 - *(osd_color_text_bg_ptr + 3)) / 2;
    }
    if (!html_to_uint32_color(osd_color_warn_str, &osd_color_warn)){print_stderr("warning, invalid -warn_color argument.\n");} //warning text raw color
    if (!html_to_uint32_color(osd_color_crit_str, &osd_color_crit)){print_stderr("warning, invalid -crit_color argument.\n");} //critical text raw color
    if (!html_to_uint32_color(osd_color_text_str, &osd_color_text)){print_stderr("warning, invalid -text_color argument.\n"); //text raw color
    } else { //compute separator color text to bg midpoint
        uint8_t *osd_color_separator_ptr = (uint8_t*)&osd_color_separator, *osd_color_text_ptr = (uint8_t*)&osd_color_text, *osd_color_bg_ptr = (uint8_t*)&osd_color_bg;
        for (uint8_t i=0; i<4; i++){
            uint16_t tmp = (*(osd_color_text_ptr+i) + *(osd_color_bg_ptr+i)) / 2;
            *(osd_color_separator_ptr+i) = (uint8_t)tmp;
        }
    }

    //low battery gpio
    lowbat_gpio_enabled = lowbat_gpio_init();

    //low battery icon
    VC_RECT_T lowbat_rect = {0};
    lowbat_resource = dispmanx_resource_create_from_png(lowbat_img_file, &lowbat_rect);
    bool lowbat_displayed = false; //low battery ressource not failed

    DISPMANX_ELEMENT_HANDLE_T lowbat_element = 0;
    VC_RECT_T lowbat_rect_dest = {0};
    if (lowbat_resource > 0){
        //resize
        double ratio = (double)lowbat_rect.height / lowbat_rect.width;
        lowbat_rect_dest.width = (double)display_width * ((double)lowbat_width_percent / 100.);
        lowbat_rect_dest.height = (double)lowbat_rect_dest.width * ratio;

        //alignment
        if (lowbat_pos_str[0]=='t'){lowbat_rect_dest.y = lowbat_padding; //top
        } else {lowbat_rect_dest.y = display_height - lowbat_padding - lowbat_rect_dest.height;} //bottom
        if (lowbat_pos_str[1]=='l'){lowbat_rect_dest.x = lowbat_padding; //left
        } else {lowbat_rect_dest.x = display_width - lowbat_padding - lowbat_rect_dest.width;} //right

	    vc_dispmanx_rect_set(&lowbat_rect, 0, 0, lowbat_rect.width << 16, lowbat_rect.height << 16);
	    vc_dispmanx_rect_set(&lowbat_rect_dest, lowbat_rect_dest.x, lowbat_rect_dest.y, lowbat_rect_dest.width, lowbat_rect_dest.height);
    }

	//osd
    double osd_downsizing = (double)display_height / (osd_text_padding * 2 + osd_max_lines * RASPIDMX_FONT_HEIGHT);
    int osd_width = ALIGN_TO_16((int)(display_width / osd_downsizing)), osd_height = ALIGN_TO_16((int)(display_height / osd_downsizing));
    print_stderr("osd resolution: %dx%d (%.4lfx)\n", osd_width, osd_height, (double)osd_width/display_width);

    DISPMANX_ELEMENT_HANDLE_T osd_element = 0;
    DISPMANX_RESOURCE_HANDLE_T osd_resource = vc_dispmanx_resource_create(VC_IMAGE_RGBA32, osd_width, osd_height, &vc_image_ptr);
    double osd_update_interval = 1. / osd_check_rate;

    //osd header
    int osd_header_y = 0, osd_header_height_dest = (double)display_height * ((double)osd_header_height_percent / 100);
    double osd_header_downsizing = (double)osd_header_height_dest / RASPIDMX_FONT_HEIGHT;
    int osd_header_width = ALIGN_TO_16((int)((double)display_width / osd_header_downsizing)), osd_header_height = ALIGN_TO_16(RASPIDMX_FONT_HEIGHT);
    print_stderr("osd header resolution: %dx%d (%.4lf)\n", osd_header_width, osd_header_height, osd_header_downsizing);

    DISPMANX_ELEMENT_HANDLE_T osd_header_element = 0;
    DISPMANX_RESOURCE_HANDLE_T osd_header_resource = vc_dispmanx_resource_create(VC_IMAGE_RGBA32, osd_header_width, osd_header_height, &vc_image_ptr);
    if (lowbat_resource > 0){
        if (osd_header_pos_str[0]=='b'){osd_header_y = display_height - osd_header_height_dest;} //footer
    }

    //signal file
    bool allow_signal_file = false, signal_file_used = false;
    if (signal_path[0] != '\0' && access(signal_path, R_OK) == 0){
        print_stderr("monitoring file '%s' for signal\n", signal_path);
        allow_signal_file = true;
    }

    //debug
    //unsigned long long bench_loop_count = 0; //debug loop count per 2sec
    //double bench_start_time = -1.;

    print_stderr("starting main loop\n");
    while (!kill_requested){ //main loop
        double loop_start_time = get_time_double(); //loop start time
        dispmanx_update = vc_dispmanx_update_start(0); //start vc update

        if (allow_signal_file && !signal_file_used){ //check signal file value
            int tmp_sig = 0;
            FILE *filehandle = fopen(signal_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &tmp_sig); fclose(filehandle);}
            if (tmp_sig == SIGUSR1 && osd_start_time < 0){osd_start_time = loop_start_time; signal_file_used = true; //full osd start time
            } else if (tmp_sig == SIGUSR2 && osd_header_start_time < 0){osd_header_start_time = loop_start_time; signal_file_used = true;} //header osd
        }

        //full osd
        //osd_test = true; //debug
        if (osd_test){osd_start_time = loop_start_time;}
        if (osd_start_time > 0){
            if (loop_start_time - osd_start_time > (double)osd_timeout){ //osd timeout
                if (osd_element > 0){vc_dispmanx_element_remove(dispmanx_update, osd_element); osd_element = 0;}
                if (signal_file_used){FILE *filehandle = fopen(signal_path, "w"); if (filehandle != NULL){fputc('0', filehandle); fclose(filehandle);} signal_file_used = false;}
                osd_start_time = -1.;
            } else if (osd_header_start_time < 0 && osd_resource > 0){ //only if header osd not displayed
                osd_build_element(osd_resource, &osd_element, dispmanx_update, osd_width, osd_height, 0, 0, display_width, display_height);
            }
        }

        //header osd
        //osd_header_test = true; //debug
        if (osd_header_test){osd_header_start_time = loop_start_time;}
        if (osd_header_start_time > 0){
            if (loop_start_time - osd_header_start_time > (double)osd_timeout){ //osd timeout
                if (osd_header_element > 0){vc_dispmanx_element_remove(dispmanx_update, osd_header_element); osd_header_element = 0;}
                if (signal_file_used){FILE *filehandle = fopen(signal_path, "w"); if (filehandle != NULL){fputc('0', filehandle); fclose(filehandle);} signal_file_used = false;}
                osd_header_start_time = -1.;
            } else if (osd_start_time < 0 && osd_header_resource > 0){ //only if full osd not displayed
                osd_header_build_element(osd_header_resource, &osd_header_element, dispmanx_update, osd_header_width, osd_header_height, 0, osd_header_y, display_width, osd_header_height_dest);
            }
        }

        //low battery icon
        //lowbat_test = true; //debug
        if (lowbat_resource > 0){ //low battery icon, disabled when header osd displayed
            if (loop_start_time - lowbat_blink_start_time > lowbat_blink){
                if (lowbat_displayed || osd_header_start_time > 0){ //remove low battery icon
                    if (lowbat_element > 0){vc_dispmanx_element_remove(dispmanx_update, lowbat_element); lowbat_element = 0;}
                    lowbat_displayed = false;
                } else if (!lowbat_displayed && (lowbat_test || lowbat_gpio_check() || lowbat_sysfs())){ //add low battery icon
                    if (lowbat_element == 0){lowbat_element = vc_dispmanx_element_add(dispmanx_update, dispmanx_display, osd_layer + 2, &lowbat_rect_dest, lowbat_resource, &lowbat_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);}
                    lowbat_displayed = true;
                }
                lowbat_blink_start_time = loop_start_time;
            }
        }

        vc_dispmanx_update_submit_sync(dispmanx_update); //push vc update

        if (kill_requested){break;} //kill requested
        
        double loop_end_time = get_time_double();
        if (loop_end_time - loop_start_time < osd_update_interval){usleep((useconds_t) ((osd_update_interval - (loop_end_time - loop_start_time)) * 1000000.));} //limit update rate

        /*if (loop_end_time - bench_start_time > 1.){ //benchmark
            print_stderr("bench(debug) update rate:%lluhz.\n", bench_loop_count);
            bench_loop_count = 0;
            bench_start_time = loop_end_time;
        } else {bench_loop_count++;}*/
    }

    //free vc ressources
    dispmanx_update = vc_dispmanx_update_start(0); //start vc update
    if (lowbat_element > 0){vc_dispmanx_element_remove(dispmanx_update, lowbat_element);} //remove low battery icon
    if (osd_element > 0){vc_dispmanx_element_remove(dispmanx_update, osd_element);} //remove osd
    vc_dispmanx_update_submit_sync(dispmanx_update); //push vc update
    if (lowbat_resource > 0){vc_dispmanx_resource_delete(lowbat_resource);}
    if (osd_resource > 0){vc_dispmanx_resource_delete(osd_resource);}

	return EXIT_SUCCESS;
}
