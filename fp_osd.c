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
Depending of wanted GPIO support (if so): wiringpi, libgpiod-dev.
Note on wiringpi: You may need to clone and compile for unofficial github repository as official WiringPi ended development, please refer to: https://github.com/PinkFreud/WiringPi

Credits goes where its due:
- This project is inspirated by Retropie-open-OSD (https://github.com/vascofazza/Retropie-open-OSD).
- Contain modified version of drawStringRGB(), drawCharRGB(), setPixelRGB(), loadPng(), savePng() functions and font charset from Raspidmx (https://github.com/AndrewFromMelbourne/raspidmx).
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


//debug
#ifdef CHARSET_EXPORT
void charset_export_png(void){ //charset to png export, limited from char 0 to 255
    #define charset_count 2
    uint32_t char_limit[charset_count] = {256, osd_icon_char_count};
    uint8_t* font_ptr[charset_count] = {raspidmx_font_ptr, osd_icon_font_ptr};
    char* filename[charset_count] = {charset_raspidmx_path, charset_icons_path};

    uint32_t txt_col = 0xff000000, grid_col = 0xffff0000;
    for (int cset=0; cset<charset_count; cset++){
        uint32_t tmp_width = RASPIDMX_FONT_WIDTH * 8 + 3, tmp_height = char_limit[cset] * (RASPIDMX_FONT_HEIGHT + 1) + 1;
        void *buffer_ptr = calloc(1, tmp_width * tmp_height * 4);
        if (buffer_ptr != NULL){
            buffer_fill(buffer_ptr, tmp_width, tmp_height, 0xffffffff);
            buffer_horizontal_line(buffer_ptr, tmp_width, tmp_height, 0, tmp_width, 0, grid_col);
            uint32_t tmp_x = 0, tmp_y = 1;
            for (int chr=0; chr<256; chr++){
                char buffer[5];
                tmp_x = 0; sprintf(buffer, "%3d", chr);
                raspidmx_drawStringRGBA32(buffer_ptr, tmp_width, tmp_height, tmp_x, tmp_y, buffer, raspidmx_font_ptr, txt_col, NULL);

                tmp_x += RASPIDMX_FONT_WIDTH * 3 + 1; sprintf(buffer, "0x%02X", chr);
                raspidmx_drawStringRGBA32(buffer_ptr, tmp_width, tmp_height, tmp_x, tmp_y, buffer, raspidmx_font_ptr, txt_col, NULL);

                tmp_x += RASPIDMX_FONT_WIDTH * 4 + 1; sprintf(buffer, "%c", chr);
                raspidmx_drawStringRGBA32(buffer_ptr, tmp_width, tmp_height, tmp_x, tmp_y, buffer, font_ptr[cset], txt_col, NULL);

                tmp_y += RASPIDMX_FONT_HEIGHT + 1;
                buffer_horizontal_line(buffer_ptr, tmp_width, tmp_height, 0, tmp_width, tmp_y - 1, grid_col);
            }

            tmp_x = RASPIDMX_FONT_WIDTH * 3; buffer_vertical_line(buffer_ptr, tmp_width, tmp_height, tmp_x++, 0, tmp_height, grid_col);
            tmp_x += RASPIDMX_FONT_WIDTH * 4; buffer_vertical_line(buffer_ptr, tmp_width, tmp_height, tmp_x++, 0, tmp_height, grid_col);
            tmp_x += RASPIDMX_FONT_WIDTH; buffer_vertical_line(buffer_ptr, tmp_width, tmp_height, tmp_x, 0, tmp_height, grid_col);

            buffer_png_export(buffer_ptr, tmp_width, tmp_height, filename[cset]);
            free(buffer_ptr);
        }
    }
}
#endif


//raspidmx functions
/*
static void raspidmx_setPixelRGBA32(void* buffer, int buffer_width, int32_t x, int32_t y, uint32_t color){ //modified version from Raspidmx
    if (buffer == NULL){return;}
    uint32_t *line = (uint32_t *)(buffer) + (y * buffer_width) + x; *line = color;
}
*/
static int32_t raspidmx_drawCharRGBA32(void* buffer, int buffer_width, int buffer_height, int32_t x, int32_t y, uint8_t c, uint8_t* font_ptr, uint32_t color){ //modified version from Raspidmx, return end position of printed char
    if (buffer == NULL){return x;}
    for (int j=0; j < RASPIDMX_FONT_HEIGHT; j++){
        int32_t tmp_y = y + j;
        if (tmp_y < 0 || tmp_y > buffer_height-1){continue;} //overflow
        uint8_t byte = *(font_ptr + c * RASPIDMX_FONT_HEIGHT + j);
        if (byte != 0){
            for (int i=0; i < RASPIDMX_FONT_WIDTH; ++i){
                int32_t tmp_x = x + i;
                if (tmp_x < 0 || tmp_x > buffer_width-1){break;} //overflow
                if ((byte >> (RASPIDMX_FONT_WIDTH - i - 1)) & 1){
                    //raspidmx_setPixelRGBA32(buffer, buffer_width, tmp_x, tmp_y, color);
                    uint32_t *line = (uint32_t *)(buffer) + (tmp_y * buffer_width) + tmp_x; *line = color;
                }
            }
        }
    }
    return x + RASPIDMX_FONT_WIDTH;
}

static VC_RECT_T raspidmx_drawStringRGBA32(void* buffer, int buffer_width, int buffer_height, int32_t x, int32_t y, const char* str, uint8_t* font_ptr, uint32_t color, uint32_t* outline_color){ //modified version of Raspidmx drawStringRGB() function. Return end position of printed string, text box size
    //todo: optimize?
    if (str == NULL || buffer == NULL){return (VC_RECT_T){.x = x, .y = y};}

    const char* str_back = str;
    int32_t x_back = x, x_last = x, x_end = x, y_back = y;
    
    //detect text box size
    while (*str != '\0'){
        if (*str == '\n'){x = x_back; y += RASPIDMX_FONT_HEIGHT;
        } else if (x < buffer_width){
            if (outline_color == NULL){raspidmx_drawCharRGBA32(buffer, buffer_width, buffer_height, x, y, *str, font_ptr, color);} x += RASPIDMX_FONT_WIDTH;
            if (x > x_last){x_last = x;} x_end = x;
        }
        ++str;
    }
    
    int32_t text_width_return = x_last - x_back, text_height_return = y + RASPIDMX_FONT_HEIGHT - y_back; //box size wo padding
    if (outline_color != NULL){ //outline mode, todo: optimize
        int32_t text_width = text_width_return + 2, text_height = text_height_return + 2; //box size
        uint8_t *text_bitmap_ptr = calloc(1, text_width * text_height), *text_out_bitmap_ptr = calloc(1, text_width * text_height);
        if (text_bitmap_ptr == NULL || text_out_bitmap_ptr == NULL){ //failed to allocate
            if (text_bitmap_ptr != NULL){free(text_bitmap_ptr);}
            if (text_out_bitmap_ptr != NULL){free(text_out_bitmap_ptr);}
        } else {
            x_back -= 1; y_back -= 1; //offset for outline
            int32_t text_x = 1, text_y = 1;
            str = str_back;
            while (*str != '\0'){ //build text and outline bitmap
                if (*str == '\n'){text_x = 1; text_y += RASPIDMX_FONT_HEIGHT;
                } else if (text_x < text_width-1){
                    for (int char_y = 0; char_y < RASPIDMX_FONT_HEIGHT; char_y++){ //char bitmap
                        uint8_t byte = *(font_ptr + *str * RASPIDMX_FONT_HEIGHT + char_y);
                        if (byte != 0){
                            int32_t tmp_y = text_y + char_y;
                            for (int char_x = 0; char_x < RASPIDMX_FONT_WIDTH; char_x++){
                                int32_t tmp_x = text_x + char_x;
                                if ((byte >> (RASPIDMX_FONT_WIDTH - char_x - 1)) & 1){
                                    for (int8_t j_out=-1; j_out<2; j_out++){for (int8_t i_out=-1; i_out<2; i_out++){*(text_out_bitmap_ptr + ((tmp_y + j_out) * text_width) + (tmp_x + i_out)) = 1;}} //outline
                                    *(text_bitmap_ptr + (tmp_y * text_width) + tmp_x) = 1;
                                }
                            }
                        }
                    }
                    text_x += RASPIDMX_FONT_WIDTH;
                }
                ++str;
            }

            for (int buffer_y = y_back, tmp_y = 0; buffer_y < y_back + text_height; buffer_y++, tmp_y++){ //text bitmap to buffer
                if (buffer_y < 0){continue;} else if (buffer_y > buffer_height - 1){break;} //overflow
                for (int buffer_x = x_back, tmp_x = 0; buffer_x < x_back + text_width; buffer_x++, tmp_x++){
                    if (buffer_x < 0){continue;} else if (buffer_x > buffer_width-1){break;} //overflow
                    uint32_t bitmap_offset = (tmp_y * text_width) + tmp_x;
                    uint32_t *pixel = (uint32_t *)(buffer) + ((buffer_y * buffer_width) + buffer_x);
                    if (*(text_bitmap_ptr + bitmap_offset) == 1){*pixel = color; //text
                    } else if (*(text_out_bitmap_ptr + bitmap_offset) == 1){*pixel = *outline_color;} //outline
                }
            }

            free(text_bitmap_ptr); free(text_out_bitmap_ptr); 
        }
    }

    str = str_back; //store string pointer
    return (VC_RECT_T){.x = x_end, .y = y, .width = text_width_return, .height = text_height_return};
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

static void buffer_vertical_line(void* buffer, uint32_t width, uint32_t height, int32_t x, int32_t y1, int32_t y2, uint32_t rgba_color){ //draw vertical line
    if (buffer == NULL || x < 0 || x > width-1){return;}
    uint32_t *ptr = (uint32_t *)buffer;
    for (int32_t ly = (y1<0)?0:y1; ly <= ((y2>height-1)?height-1:y2); ly++){*(ptr + ly * width + x) = rgba_color;}
}

static uint32_t buffer_getcolor_rgba(void* buffer, uint32_t width, uint32_t height, int32_t x, int32_t y){ //get specific color from buffer
    uint32_t color = 0xFF000000;
    if (buffer == NULL || x < 0 || y < 0 || x > width-1 || y > height-1){return color;}
    color = *((uint32_t *)(buffer) + (y * width) + x);
    return color;
}

#ifdef BUFFER_PNG_EXPORT
static bool buffer_png_export(void* buffer, uint32_t width, uint32_t height, const char* filename){ //export buffer to png, modified version of savePng() from Raspidmx
    //WARNING: function doesn't check in any way for buffer size, buffer is supposed to be 4 bytes per pixel, following RGBA dispmanx pixel format (revert).
    if (filename == NULL || filename[0]=='\0'){print_stderr("invalid filename.\n"); return false;}
    if (width == 0 || height == 0){print_stderr("invalid resolution: %dx%d.\n", width, height); return false;}

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL); //allocate and initialize a png_struct write structure
    if (png_ptr == NULL){print_stderr("failed to init png_struct write structure.\n"); return false;}

    png_infop info_ptr = png_create_info_struct(png_ptr); //allocate and initialize a png_info structure
    if (info_ptr == NULL){print_stderr("failed to init png_info structure.\n"); png_destroy_write_struct(&png_ptr, 0); return false;}
    if (setjmp(png_jmpbuf(png_ptr))){print_stderr("setjmp png_jmpbuf failed.\n"); png_destroy_write_struct(&png_ptr, &info_ptr); return false;}

    FILE *filehandle = fopen(filename, "wb");
    if (filehandle == NULL){print_stderr("failed to open file handle for '%s'.\n", filename); png_destroy_write_struct(&png_ptr, &info_ptr); return false;}

    png_init_io(png_ptr, filehandle); //initialize input/output for the PNG file
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    uint32_t pitch = width * 4;
    for (uint32_t y = 0; y < height; y++){png_write_row(png_ptr, buffer + (pitch * y));}

    png_write_end(png_ptr, NULL); //write end of PNG file
    png_destroy_write_struct(&png_ptr, &info_ptr); //free structures memory
    fclose(filehandle);
    print_stderr("data wrote to '%s'.\n", filename);
    return true;
}
#endif

static DISPMANX_RESOURCE_HANDLE_T dispmanx_resource_create_from_png(char* filename, VC_RECT_T* image_rect_ptr){ //create dispmanx ressource from png file, return 0 on failure, ressource handle on success
    FILE* filehandle = fopen(filename, "rb");
    if (filehandle == NULL){print_stderr("failed to read '%s'.\n", filename); return 0;} else {print_stderr("'%s' opened.\n", filename);}

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL); //allocate and initialize a png_struct structure
    if (png_ptr == NULL){print_stderr("failed to init png_struct read structure.\n"); fclose(filehandle); return 0;}

    png_infop info_ptr = png_create_info_struct(png_ptr); //allocate and initialize a png_info structure
    if (info_ptr == NULL){print_stderr("failed to init png_info structure.\n"); png_destroy_read_struct(&png_ptr, 0, 0); fclose(filehandle); return 0;}
    if (setjmp(png_jmpbuf(png_ptr))){print_stderr("setjmp png_jmpbuf failed.\n"); png_destroy_read_struct(&png_ptr, &info_ptr, 0); fclose(filehandle); return 0;}

    png_init_io(png_ptr, filehandle); //initialize input/output for the PNG file
    png_read_info(png_ptr, info_ptr); //read the PNG image information

    int width = png_get_image_width(png_ptr, info_ptr), height = png_get_image_height(png_ptr, info_ptr); //dimensions
    if (width == 0 || height == 0){print_stderr("failed to get image size.\n"); png_destroy_read_struct(&png_ptr, &info_ptr, 0); fclose(filehandle); return 0;}

    png_byte color_type = png_get_color_type(png_ptr, info_ptr), bit_depth = png_get_bit_depth(png_ptr, info_ptr); //color type and depth
    print_stderr("resolution: %dx%d depth:%dbits.\n", width, height, bit_depth*png_get_channels(png_ptr, info_ptr));

    double gamma = .0; if (png_get_gAMA(png_ptr, info_ptr, &gamma)){png_set_gamma(png_ptr, 2.2, gamma);} //gamma correction, useful?

    //convert to rgba
    if (color_type == PNG_COLOR_TYPE_PALETTE){png_set_palette_to_rgb(png_ptr); //convert palette to rgb
    } else if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA){ //grayscale
        if (bit_depth < 8){png_set_expand_gray_1_2_4_to_8(png_ptr);} //extend to 8bits depth
        png_set_gray_to_rgb(png_ptr); //convert grayscale to rgb
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)){png_set_tRNS_to_alpha(png_ptr);} //convert tRNS chunks to alpha channels
    if (bit_depth == 16){png_set_scale_16(png_ptr);} //scale down 16bits to 8bits depth
    if (!(color_type & PNG_COLOR_MASK_ALPHA)){png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER); print_stderr("dummy alpha channel added.\n");} //no alpha channel, add one
    png_read_update_info(png_ptr, info_ptr); //update png info structure
    color_type = png_get_color_type(png_ptr, info_ptr);

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


//gpio functions
#ifndef NO_GPIO
static void gpio_init(void){ //init gpio things
    bool gpio_lib_failed = false;

    #if defined(USE_WIRINGPI) //wiringPi library
        #define WIRINGPI_CODES 1 //allow error code return
        int err; if ((err = wiringPiSetupGpio()) < 0){print_stderr("failed to initialize wiringPi, errno:%d.\n", -err); gpio_lib_failed = true;} //use BCM numbering
    #elif defined(USE_GPIOD) //gpiod library
        if ((gpiod_chip = gpiod_chip_open_lookup("0")) == NULL){print_stderr("gpiod_chip_open_lookup failed.\n"); gpio_lib_failed = true;}
    #else
        gpio_lib_failed = true;
    #endif

    for (int i=0; i<gpio_pins_count; i++){
        gpio_enabled[i] = *gpio_pin[i] > -1;
        if (!gpio_lib_failed && gpio_enabled[i]){
            #if defined(USE_WIRINGPI) //wiringPi library
                pinMode(*gpio_pin[i], INPUT);
                print_stderr("using wiringPi to poll GPIO%d.\n", *gpio_pin[i]);
            #elif defined(USE_GPIOD) //gpiod library
                sprintf(gpiod_consumer_name[i], "%s %d", program_name, *gpio_pin[i]);
                if ((gpiod_input_line[i] = gpiod_chip_get_line(gpiod_chip, *gpio_pin[i])) == NULL){print_stderr("gpiod_chip_get_line failed for pin:%d.\n", *gpio_pin[i]); gpio_lib_failed = true;
                } else if (gpiod_line_request_both_edges_events(gpiod_input_line[i], gpiod_consumer_name[i]) < 0){print_stderr("gpiod_line_request_both_edges_events failed. chip:%s(%s), consumer:'%s'.\n", gpiod_chip_name(gpiod_chip), gpiod_chip_label(gpiod_chip), gpiod_consumer_name[i]); gpio_lib_failed = true;
                } else if ((gpiod_fd[i] = gpiod_line_event_get_fd(gpiod_input_line[i])) < 0){print_stderr("gpiod_line_event_get_fd failed. errno:%d, consumer:'%s'.\n", -gpiod_fd[i], gpiod_consumer_name[i]); gpio_lib_failed = true;
                } else {
                    fcntl(gpiod_fd[i], F_SETFL, fcntl(gpiod_fd[i], F_GETFL, 0) | O_NONBLOCK); //set gpiod fd to non blocking
                    print_stderr("using libGPIOd to poll GPIO%d, chip:%s(%s), consumer:'%s'.\n", *gpio_pin[i], gpiod_chip_name(gpiod_chip), gpiod_chip_label(gpiod_chip), gpiod_consumer_name[i]);
                }
            #endif
        }
    }

    if (gpio_lib_failed){
        #ifdef USE_GPIOD
            for (int i=0; i<gpio_pins_count; i++){
                if (gpiod_input_line[i] != NULL){gpiod_line_release(gpiod_input_line[i]);} gpiod_fd[i] = -1;
            }
        #endif
        if (access("/usr/bin/raspi-gpio", F_OK) == 0){print_stderr("falling back to '/usr/bin/raspi-gpio' program.\n"); gpio_external = true;}
        for (int i=0; i<gpio_pins_count; i++){
            if (gpio_external){
                if (gpio_enabled[i]){print_stderr("gpio%d\n", *gpio_pin[i]);}
            } else {gpio_enabled[i] = false;}
        }
    }
}

static bool gpio_check(int index){ //check if gpio pin state
    if (!gpio_enabled[index]){return false;}
    bool ret = false;

    if (!gpio_external){
        #ifdef USE_WIRINGPI //wiringPi library
            ret = digitalRead(*gpio_pin[index]) > 0;
            if (*gpio_reversed[index]){ret = !ret;} //reverse input
        #elif defined(USE_GPIOD) //gpiod library
            if (gpiod_fd >= 0 && gpiod_line_is_free(gpiod_input_line[index])){
                int gpiod_ret = gpiod_line_get_value(gpiod_input_line[index]);
                if (gpiod_ret >= 0){if (*gpio_reversed[index]){ret = !gpiod_ret;} else {ret = gpiod_ret;}} //reverse/normal input
            }
        #endif
    } else {
        int tmp_gpio = -1, tmp_level = -1;
        char buffer[32]; sprintf(buffer, "raspi-gpio get %d", *gpio_pin[index]);
        FILE *filehandle = popen(buffer, "r");
        if(filehandle != NULL){fscanf(filehandle, "%*[^0123456789]%d%*[^0123456789]%d", &tmp_gpio, &tmp_level); pclose(filehandle);} //GPIO %d: level=%d fsel=1 func=INPUT
        if (tmp_gpio == *gpio_pin[index] && tmp_level >= 0){
            ret = tmp_level > 0;
            if (*gpio_reversed[index]){ret = !ret;} //reverse input
        }
    }
    return ret;
}
#endif

//low battery specific
static bool lowbat_sysfs(void){ //read sysfs power_supply battery capacity, return true if threshold, false if under or file not found
    battery_rsoc_last = battery_rsoc;
    FILE *filehandle = fopen(battery_rsoc_path, "r");
    if (filehandle != NULL){
        char buffer[5]; fgets(buffer, 5, filehandle); fclose(filehandle);
        battery_rsoc = atoi(buffer); int_constrain(&battery_rsoc, 0, 100);
        if (battery_rsoc <= lowbat_limit){return true;}
    } else {battery_rsoc = -1;}
    return false;
}

//cpu temperature specific
static bool cputemp_sysfs(void){ //read sysfs cpu temperature, return true if threshold, false if under or file not found
    cputemp_last = cputemp_curr;
    FILE *filehandle = fopen(cpu_thermal_path, "r");
    if (filehandle != NULL){
        fscanf(filehandle, "%d", &cputemp_curr);
        fclose(filehandle);
        cputemp_curr /= cpu_thermal_divider;
        if (cputemp_curr >= cputemp_crit){return true;}
    }
    return false;
}


//osd related
#ifndef NO_TINYOSD
void tinyosd_build_element(DISPMANX_RESOURCE_HANDLE_T resource, DISPMANX_ELEMENT_HANDLE_T *element, DISPMANX_UPDATE_HANDLE_T update, uint32_t osd_width, uint32_t osd_height, uint32_t x, uint32_t y, uint32_t width, uint32_t height){
    if (tinyosd_buffer_ptr == NULL){
        print_stderr("creating tiny osd bitmap buffer.\n");
        tinyosd_buffer_ptr = calloc(1, osd_width * osd_height * 4);
        if (tinyosd_buffer_ptr != NULL){buffer_fill(tinyosd_buffer_ptr, osd_width, osd_height, osd_color_bg);} //buffer reset
    }

    if (tinyosd_buffer_ptr != NULL){ //valid bitmap buffer
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
            buffer_fill(tinyosd_buffer_ptr, osd_width, osd_height, osd_color_bg); //buffer reset

            //clock: right side (done first because buffer)
            text_column_right -= strlen(buffer) * RASPIDMX_FONT_WIDTH;
            raspidmx_drawStringRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, buffer, raspidmx_font_ptr, osd_color_text, NULL);
            text_column_right -= RASPIDMX_FONT_WIDTH;
            raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, 1, osd_icon_font_ptr, osd_color_separator/*, NULL*/); //separator

            //battery: left side
            double batt_voltage = -1.;
            //filehandle = fopen(battery_rsoc_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &battery_rsoc); fclose(filehandle);} //rsoc
            filehandle = fopen(battery_volt_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%lf", &batt_voltage); fclose(filehandle); batt_voltage /= battery_volt_divider;} //voltage
            //batt_rsoc = 100; batt_voltage = 4.195;
            if (battery_rsoc > -1 || batt_voltage > 0){
                uint32_t tmp_color = osd_color_text;
                if (battery_rsoc > 0){if (battery_rsoc <= lowbat_limit){tmp_color = osd_color_crit;} else if (battery_rsoc <= 25){tmp_color = osd_color_warn;}
                } else if (batt_voltage > 0.){if (batt_voltage < 3.4){tmp_color = osd_color_crit;} else if (batt_voltage < 3.55){tmp_color = osd_color_warn;}}
                
                if (battery_rsoc < 0){sprintf(buffer, "%.3lfv", batt_voltage); //invalid rsoc, voltage only
                } else if (batt_voltage < 0.){sprintf(buffer, "%3d%%", battery_rsoc); //invalid voltage, rsoc only
                } else {sprintf(buffer, "%3d%% %.2lfv", battery_rsoc, batt_voltage);} //both

                text_column_left = raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_left, 0, 7, osd_icon_font_ptr, tmp_color/*, NULL*/) + 2; //battery icon
                text_column_left = raspidmx_drawStringRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_left, 0, buffer, raspidmx_font_ptr, tmp_color, NULL).x;
                text_column_left = raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_left, 0, 1, osd_icon_font_ptr, osd_color_separator/*, NULL*/); //separator
            }

            //cpu: left side
            //int32_t cpu_temp = -1;
            //filehandle = fopen(cpu_thermal_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &cpu_temp); fclose(filehandle); cpu_temp /= cpu_thermal_divider;} //temp
            cpu_load = (int32_t)(cpu_load_add / cpu_loops); cpu_load_add = cpu_loops = 0; //load
            if (cputemp_curr > -1 || cpu_load > -1){
                uint32_t tmp_color = osd_color_text;
                if (cputemp_curr > -1){
                    if (cputemp_curr >= cputemp_crit){tmp_color = osd_color_crit;} else if (cputemp_curr >= cputemp_warn){tmp_color = osd_color_warn;}
                    sprintf(buffer, "%dC %3d%%", cputemp_curr, cpu_load);
                } else {sprintf(buffer, "%3d%%", cpu_load);}

                text_column_left = raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_left, 0, 2, osd_icon_font_ptr, tmp_color/*, NULL*/) + 2; //cpu icon
                text_column_left = raspidmx_drawStringRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_left, 0, buffer, raspidmx_font_ptr, tmp_color, NULL).x;
                text_column_left = raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_left, 0, 1, osd_icon_font_ptr, osd_color_separator/*, NULL*/); //separator
            }

            //backlight: right side
            int32_t backlight = -1, backlight_max = -1;
            filehandle = fopen(backlight_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &backlight); fclose(filehandle);}
            filehandle = fopen(backlight_max_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &backlight_max); fclose(filehandle);}
            if (backlight > -1){
                if (backlight_max < 1){sprintf(buffer, "%d", backlight);
                } else {sprintf(buffer, "%.0lf%%", ((double)backlight/backlight_max)*100);}

                text_column_right -= strlen(buffer) * RASPIDMX_FONT_WIDTH;
                raspidmx_drawStringRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, buffer, raspidmx_font_ptr, osd_color_text, NULL);
                text_column_right -= RASPIDMX_FONT_WIDTH + 2;
                raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, 8, osd_icon_font_ptr, osd_color_text/*, NULL*/); //backlight icon
                text_column_right -= RASPIDMX_FONT_WIDTH;
                raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, 1, osd_icon_font_ptr, osd_color_separator/*, NULL*/); //separator
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
                    raspidmx_drawStringRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, "\5\6", osd_icon_font_ptr, tmp_color, NULL);
                    text_column_right -= strlen(buffer) * RASPIDMX_FONT_WIDTH;
                    raspidmx_drawStringRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, buffer, raspidmx_font_ptr, tmp_color, NULL);

                    if (wifi_signal > 0){
                        tmp_color = osd_color_text;
                        if (wifi_signal > wifi_signal_steps[1]){tmp_color = osd_color_crit;} else if (wifi_signal > wifi_signal_steps[0]){tmp_color = osd_color_warn;}
                    }

                    text_column_right -= RASPIDMX_FONT_WIDTH + 2;
                    raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, 4, osd_icon_font_ptr, tmp_color/*, NULL*/); //wifi icon
                    text_column_right -= RASPIDMX_FONT_WIDTH;
                    raspidmx_drawCharRGBA32(tinyosd_buffer_ptr, osd_width, osd_height, text_column_right, 0, 1, osd_icon_font_ptr, osd_color_separator/*, NULL*/); //separator
                }
            }

            //line between left and right separator
            buffer_horizontal_line(tinyosd_buffer_ptr, osd_width, osd_height, text_column_left - RASPIDMX_FONT_WIDTH/2, text_column_right + RASPIDMX_FONT_WIDTH/2, osd_height/2 - 1, osd_color_separator); 

            //horizontal break line
            buffer_horizontal_line(tinyosd_buffer_ptr, osd_width, osd_height, 0, osd_width, (y == 0)?osd_height-1:0, osd_color_separator); 

            #ifdef BUFFER_PNG_EXPORT
                if (debug_buffer_png_export){buffer_png_export(tinyosd_buffer_ptr, osd_width, osd_height, "debug_export/tiny_osd.png");} //debug png export
            #endif

            VC_RECT_T osd_rect; vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width, osd_height);
            if (vc_dispmanx_resource_write_data(resource, VC_IMAGE_RGBA32, osd_width * 4, tinyosd_buffer_ptr, &osd_rect) != 0){
                if (debug){print_stderr("failed to write dispmanx resource.\n");}
            } else {
                vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width << 16, osd_height << 16);
                VC_RECT_T osd_rect_dest; vc_dispmanx_rect_set(&osd_rect_dest, x, y, width, height);
                if (*element == 0){
                    *element = vc_dispmanx_element_add(update, dispmanx_display, osd_layer + 3, &osd_rect_dest, resource, &osd_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);
                    if (debug && *element == 0){print_stderr("failed to add element.\n");}
                } else {
                    vc_dispmanx_element_modified(update, *element, &osd_rect_dest);
                    vc_dispmanx_element_change_attributes(update, *element, 0, 0, 0, &osd_rect_dest, 0, 0, DISPMANX_NO_ROTATE);
                }
            }
        }
    } else if (debug){print_stderr("calloc failed.\n");} //failed to allocate buffer
}
#endif

#ifndef NO_OSD
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
            double batt_voltage = -1.;
            //filehandle = fopen(battery_rsoc_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &batt_rsoc); fclose(filehandle);} //rsoc
            filehandle = fopen(battery_volt_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%lf", &batt_voltage); fclose(filehandle); batt_voltage /= battery_volt_divider;} //voltage
            if (battery_rsoc > -1 || batt_voltage > 0){
                strcpy(buffer, "Battery: "); char buffer0[16] = {'\0'};

                uint32_t tmp_color = osd_color_text;
                if (battery_rsoc > 0){if (battery_rsoc <= lowbat_limit){tmp_color = osd_color_crit;} else if (battery_rsoc <= 25){tmp_color = osd_color_warn;}
                } else if (batt_voltage > 0.){if (batt_voltage < 3.4){tmp_color = osd_color_crit;} else if (batt_voltage < 3.55){tmp_color = osd_color_warn;}}

                if (battery_rsoc < 0){sprintf(buffer0, "%.3lfv", batt_voltage); //invalid rsoc, voltage only
                } else if (batt_voltage < 0.){sprintf(buffer0, "%d%%", battery_rsoc); //invalid voltage, rsoc only
                } else {sprintf(buffer0, "%d%% (%.3lfv)", battery_rsoc, batt_voltage);} //both
                strcat(buffer, buffer0);
                raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, tmp_color, &osd_color_text_bg);
                text_y += osd_text_padding + RASPIDMX_FONT_HEIGHT;
            }

            //system: cpu temperature
            //int32_t cpu_temp = -1;
            //filehandle = fopen(cpu_thermal_path, "r");
            //if (filehandle != NULL){fscanf(filehandle, "%d", &cpu_temp); fclose(filehandle); cpu_temp /= cpu_thermal_divider;}

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
            if (cputemp_curr > -1 || cpu_load > -1 || memory_total > -1 || gpu_memory_total > -1){
                raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, "System:", raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);
                text_column = osd_text_padding * 2 + RASPIDMX_FONT_WIDTH * 7;

                if (cputemp_curr > -1 || cpu_load > -1){
                    uint32_t tmp_color = osd_color_text;
                    if (cputemp_curr > -1){
                        if (cputemp_curr >= cputemp_crit){tmp_color = osd_color_crit;} else if (cputemp_curr >= cputemp_warn){tmp_color = osd_color_warn;}
                        sprintf(buffer, "CPU: %dC (%d%% load)", cputemp_curr, cpu_load);
                    } else {sprintf(buffer, "CPU: %d%%", cpu_load);}
                    raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, tmp_color, &osd_color_text_bg);
                    text_y += RASPIDMX_FONT_HEIGHT;
                }

                //ram
                if (memory_total > 0){
                    int32_t memory_load = memory_used * 100 / memory_total;
                    if (memory_load < 0){memory_load = 0;} else if (memory_load > 100){memory_load = 100;}
                    uint32_t tmp_color = (memory_load>95)?osd_color_warn:osd_color_text;
                    sprintf(buffer, "RAM: %d/%dM (%d%% used)", memory_used, memory_total, memory_load);
                    raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, buffer, raspidmx_font_ptr, tmp_color, &osd_color_text_bg);
                    text_y += RASPIDMX_FONT_HEIGHT;
                }

                //gpu memory
                if (gpu_memory_total > 0){
                    int32_t gpu_memory_load = gpu_memory_used * 100 / gpu_memory_total;
                    uint32_t tmp_color = (gpu_memory_load>95)?osd_color_warn:osd_color_text;
                    sprintf(buffer, "GPU: %d/%dM (%d%% used)", gpu_memory_used, gpu_memory_total, gpu_memory_load);
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

            //raspidmx_drawStringRGBA32(osd_buffer_ptr, osd_width, osd_height, text_column, text_y, "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\nabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\n", raspidmx_font_ptr, osd_color_text, &osd_color_text_bg);

            #ifdef BUFFER_PNG_EXPORT
                if (debug_buffer_png_export){buffer_png_export(osd_buffer_ptr, osd_width, osd_height, "debug_export/full_osd.png");} //debug png export
            #endif

            VC_RECT_T osd_rect; vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width, osd_height);
            if (vc_dispmanx_resource_write_data(resource, VC_IMAGE_RGBA32, osd_width * 4, osd_buffer_ptr, &osd_rect) != 0){
                if (debug){print_stderr("failed to write dispmanx resource.\n");}
            } else {
                vc_dispmanx_rect_set(&osd_rect, 0, 0, osd_width << 16, osd_height << 16);
                VC_RECT_T osd_rect_dest; vc_dispmanx_rect_set(&osd_rect_dest, x, y, width, height);
                if (*element == 0){
                    *element = vc_dispmanx_element_add(update, dispmanx_display, osd_layer + 1, &osd_rect_dest, resource, &osd_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);
                    if (debug && *element == 0){print_stderr("failed to add element.\n");}
                } else {
                    vc_dispmanx_element_modified(update, *element, &osd_rect_dest);
                    vc_dispmanx_element_change_attributes(update, *element, 0, 0, 0, &osd_rect_dest, 0, 0, DISPMANX_NO_ROTATE);
                }
            }
        }
    } else if (debug){print_stderr("calloc failed.\n");} //failed to allocate buffer
}
#endif

#ifndef NO_BATTERY_ICON
void lowbatt_build_element(DISPMANX_RESOURCE_HANDLE_T resource, DISPMANX_ELEMENT_HANDLE_T *element, DISPMANX_UPDATE_HANDLE_T update, uint32_t icon_width, uint32_t icon_height, uint32_t x, uint32_t y, uint32_t width, uint32_t height){
    uint32_t icon_width_16 = ALIGN_TO_16(icon_width), icon_height_16 = ALIGN_TO_16(icon_height);

    if (lowbat_buffer_ptr == NULL){
        print_stderr("creating low battery bitmap buffer.\n");
        lowbat_buffer_ptr = calloc(1, icon_width_16 * icon_height_16 * 4);
        if (lowbat_buffer_ptr != NULL){
            VC_RECT_T tmp_rect = {.width=icon_width, .height=icon_height};
            vc_dispmanx_resource_read_data(resource, &tmp_rect, lowbat_buffer_ptr, icon_width_16 * 4);
            lowbat_icon_bar_color = buffer_getcolor_rgba(lowbat_buffer_ptr, icon_width_16, icon_height_16, 9, 13);
            lowbat_icon_bar_bg_color = buffer_getcolor_rgba(lowbat_buffer_ptr, icon_width_16, icon_height_16, 34, 13);
        }
    }
    
    if (lowbat_buffer_ptr != NULL){ //valid bitmap buffer
        static uint32_t y_back = UINT32_MAX;
        if (battery_rsoc != battery_rsoc_last || *element == 0 || y_back != y){ //redraw
            uint32_t tmp_color = lowbat_icon_bar_color, tmp_color_bar = lowbat_icon_bar_color;
            if (battery_rsoc <= lowbat_limit){tmp_color = osd_color_crit; tmp_color_bar = osd_color_crit;
            } else if (battery_rsoc <= 25){tmp_color = osd_color_warn; tmp_color_bar = osd_color_warn;}

            buffer_rectangle_fill(lowbat_buffer_ptr, icon_width_16, icon_height_16, 4, 6, 35, 15, lowbat_icon_bar_bg_color); //reset bars background
            buffer_rectangle_fill(lowbat_buffer_ptr, icon_width_16, icon_height_16, 4, 6, 35*battery_rsoc/100, 15, tmp_color_bar); //bars

            char buffer[16]; sprintf(buffer, "%3d%%", battery_rsoc);
            raspidmx_drawStringRGBA32(lowbat_buffer_ptr, icon_width_16, icon_height_16, 6, 6, buffer, raspidmx_font_ptr, tmp_color, &lowbat_icon_bar_bg_color);

            #ifdef BUFFER_PNG_EXPORT
                if (debug_buffer_png_export){buffer_png_export(lowbat_buffer_ptr, icon_width_16, icon_height_16, "debug_export/lowbatt_icon.png");} //debug png export
            #endif

            VC_RECT_T icon_rect; vc_dispmanx_rect_set(&icon_rect, 0, 0, icon_width, icon_height);
            if (vc_dispmanx_resource_write_data(resource, VC_IMAGE_RGBA32, icon_width_16 * 4, lowbat_buffer_ptr, &icon_rect) != 0){
                if (debug){print_stderr("failed to write dispmanx resource.\n");}
            } else {
                vc_dispmanx_rect_set(&icon_rect, 0, 0, icon_width << 16, icon_height << 16);
                VC_RECT_T icon_rect_dest; vc_dispmanx_rect_set(&icon_rect_dest, x, y, width, height);
                if (*element == 0){
                    *element = vc_dispmanx_element_add(update, dispmanx_display, osd_layer + 2, &icon_rect_dest, resource, &icon_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);
                    if (debug && *element == 0){print_stderr("failed to add element.\n");}
                } else {
                    vc_dispmanx_element_modified(update, *element, &icon_rect_dest);
                    vc_dispmanx_element_change_attributes(update, *element, 0, 0, 0, &icon_rect_dest, 0, 0, DISPMANX_NO_ROTATE);
                }
            }
            y_back = y;
        }
    } else if (debug){print_stderr("calloc failed.\n");} //failed to allocate buffer
}
#endif

#ifndef NO_CPU_ICON
void cputemp_build_element(DISPMANX_RESOURCE_HANDLE_T resource, DISPMANX_ELEMENT_HANDLE_T *element, DISPMANX_UPDATE_HANDLE_T update, uint32_t icon_width, uint32_t icon_height, uint32_t x, uint32_t y, uint32_t width, uint32_t height){
    uint32_t icon_width_16 = ALIGN_TO_16(icon_width), icon_height_16 = ALIGN_TO_16(icon_height);

    if (cputemp_buffer_ptr == NULL){
        print_stderr("creating cpu temperature bitmap buffer.\n");
        cputemp_buffer_ptr = calloc(1, icon_width_16 * icon_height_16 * 4);
        if (cputemp_buffer_ptr != NULL){
            VC_RECT_T tmp_rect = {.width=icon_width, .height=icon_height};
            vc_dispmanx_resource_read_data(resource, &tmp_rect, cputemp_buffer_ptr, icon_width_16 * 4);
            cputemp_icon_bg_color = buffer_getcolor_rgba(cputemp_buffer_ptr, icon_width_16, icon_height_16, 30, 13);
        }
    }
    
    if (cputemp_buffer_ptr != NULL){ //valid bitmap buffer
        static uint32_t y_back = UINT32_MAX;
        if (cputemp_curr != cputemp_last || *element == 0 || y_back != y){ //redraw
            
            uint32_t tmp_color = 0xFF000000;
            if (cputemp_curr >= cputemp_crit){tmp_color = osd_color_crit;} else if (cputemp_curr >= cputemp_warn){tmp_color = osd_color_warn;}

            buffer_rectangle_fill(cputemp_buffer_ptr, icon_width_16, icon_height_16, 3, 7, 32, 15, cputemp_icon_bg_color); //reset text background

            char buffer[16]; sprintf(buffer, "%3dC", cputemp_curr);
            raspidmx_drawStringRGBA32(cputemp_buffer_ptr, icon_width_16, icon_height_16, 3, 7, buffer, raspidmx_font_ptr, tmp_color, NULL);

            #ifdef BUFFER_PNG_EXPORT
                if (debug_buffer_png_export){buffer_png_export(cputemp_buffer_ptr, icon_width_16, icon_height_16, "debug_export/cputemp_icon.png");} //debug png export
            #endif

            VC_RECT_T icon_rect; vc_dispmanx_rect_set(&icon_rect, 0, 0, icon_width, icon_height);
            if (vc_dispmanx_resource_write_data(resource, VC_IMAGE_RGBA32, icon_width_16 * 4, cputemp_buffer_ptr, &icon_rect) != 0){
                if (debug){print_stderr("failed to write dispmanx resource.\n");}
            } else {
                vc_dispmanx_rect_set(&icon_rect, 0, 0, icon_width << 16, icon_height << 16);
                VC_RECT_T icon_rect_dest; vc_dispmanx_rect_set(&icon_rect_dest, x, y, width, height);
                if (*element == 0){
                    *element = vc_dispmanx_element_add(update, dispmanx_display, osd_layer + 2, &icon_rect_dest, resource, &icon_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);
                    if (debug && *element == 0){print_stderr("failed to add element.\n");}
                } else {
                    vc_dispmanx_element_modified(update, *element, &icon_rect_dest);
                    vc_dispmanx_element_change_attributes(update, *element, 0, 0, 0, &icon_rect_dest, 0, 0, DISPMANX_NO_ROTATE);
                }
            }
            y_back = y;
        }
    } else if (debug){print_stderr("calloc failed.\n");} //failed to allocate buffer
}
#endif

//integer manipulation functs
int int_constrain(int* val, int min, int max){ //limit int value to given (incl) min and max value, return 0 if val within min and max, -1 under min, 1 over max
    int ret = 0;
    if (*val < min){*val = min; ret = -1;} else if (*val > max){*val = max; ret = 1;}
    return ret;
}


//evdev functs
#ifndef NO_EVDEV
int in_array_int(int* arr, int value, int arr_size){ //search in value in int array, return index or -1 on failure
    for (int i=0; i < arr_size; i++) {if (arr[i] == value) {return i;}}
    return -1;
}

void *evdev_routine(void* arg){ //evdev input thread routine
    if (evdev_path[0] == '\0'){print_stderr("empty event device path, evdev routine disabled\n"); goto thread_close;}

    //input sequence spliting
    int osd_evdev_sequence_limit = 0;
    int tinyosd_evdev_sequence_limit = 0;

    #ifndef NO_OSD
        int osd_evdev_sequence[evdev_sequence_max] = {0}; //int value of sequence, 0 will be interpreted as ignore, computed during runtime
        if (osd_evdev_sequence_char[0] != '\0'){
            print_stderr("osd sequence: ");
            int index = 0;
            char buffer[strlen(osd_evdev_sequence_char) + 1]; strcpy(buffer, osd_evdev_sequence_char);
            char *tmp_ptr = strtok(buffer, ","); //split element
            while (tmp_ptr != NULL){
                if (strchr(tmp_ptr, 'x') == NULL){osd_evdev_sequence[index] = atoi(tmp_ptr);} else {sscanf(tmp_ptr, "0x%X", &osd_evdev_sequence[index]);} //int or hex value
                fprintf(stderr, "%d ", osd_evdev_sequence[index]); index++;
                if (index >= evdev_sequence_max){break;} //avoid overflow
                tmp_ptr = strtok(NULL, ","); //next element
            }
            osd_evdev_sequence_limit = index;
            fprintf(stderr, "(%d)\n", index);
        }
    #endif

    #ifndef NO_TINYOSD
        int tinyosd_evdev_sequence[evdev_sequence_max] = {0}; //int value of sequence, 0 will be interpreted as ignore, computed during runtime
        if (tinyosd_evdev_sequence_char[0] != '\0'){
            print_stderr("tiny osd sequence: ");
            int index = 0;
            char buffer[strlen(tinyosd_evdev_sequence_char) + 1]; strcpy(buffer, tinyosd_evdev_sequence_char);
            char *tmp_ptr = strtok(buffer, ","); //split element
            while (tmp_ptr != NULL){
                if (strchr(tmp_ptr, 'x') == NULL){tinyosd_evdev_sequence[index] = atoi(tmp_ptr);} else {sscanf(tmp_ptr, "0x%X", &tinyosd_evdev_sequence[index]);} //int or hex value
                fprintf(stderr, "%d ", tinyosd_evdev_sequence[index]); index++;
                if (index >= evdev_sequence_max){break;} //avoid overflow
                tmp_ptr = strtok(NULL, ","); //next element
            }
            tinyosd_evdev_sequence_limit = index;
            fprintf(stderr, "(%d)\n", index);
        }
    #endif

    if (osd_evdev_sequence_limit == 0 && tinyosd_evdev_sequence_limit == 0){print_stderr("no valid event sequence detected, evdev routine disabled\n"); goto thread_close;}

    //remove trailing '/' from event path
    int evdev_path_len = strlen(evdev_path);
    if (evdev_path_len > 1 && evdev_path[evdev_path_len - 1] == '/'){evdev_path[evdev_path_len - 1] = '\0'; evdev_path_len--;}

    //event input
    int evdev_fd = -1;
    double update_interval = 1. / osd_check_rate, recheck_start_time = -1.;
    double evdev_detected_start = -1.; //time of first detected input
    #define input_event_count 64 //absolute limit simultanious event report
    struct input_event events[input_event_count];
    int input_event_size = (int) sizeof(struct input_event), events_size = input_event_size * input_event_count;
    int evdev_detected_sequence[evdev_sequence_max * 2] = {0}; //list of detect sequence (incl osd and tiny osd)
    int evdev_detected_sequence_index = 0;

    evdev_sequence_detect_interval = (double)evdev_sequence_detect_interval_ms / 1000.;

    while (!kill_requested){ //thread main loop
        double loop_start_time = get_time_double(); //loop start time

        if (evdev_fd == -1 && loop_start_time - recheck_start_time > (double)evdev_check_interval){ //device has failed or not started
            bool evdev_retry = true;
            char evdev_name[255] = ""; //store temporary device name
            if (evdev_path_used[0] == '\0'){ //initial device detection
                struct stat evdev_stat;
                if (stat(evdev_path, &evdev_stat) == 0){
                    if (S_ISDIR(evdev_stat.st_mode)){ //given path is a folder, scan for proper event file
                        struct dirent **folder_list;
                        int folder_files = scandir(evdev_path, &folder_list, 0, 0);
                        if (folder_files != -1){
                            bool scan_mode = evdev_name_search[0] == '\0';
                            if (scan_mode){print_stderr("no event device name provided, falling back to scan mode\n");}
                            for (int i = 0; i < folder_files; i++){
                                char* evdev_file_tmp = folder_list[i]->d_name;
                                char evdev_path_tmp[evdev_path_len + strlen(evdev_file_tmp) + 2]; sprintf(evdev_path_tmp, "%s/%s", evdev_path, evdev_file_tmp);
                                evdev_fd = open(evdev_path_tmp, O_RDONLY);
                                if (evdev_fd < 0){continue;} //failed to open file
                                evdev_name[0] = '\0'; ioctl(evdev_fd, EVIOCGNAME(sizeof(evdev_name)), evdev_name); close(evdev_fd); //get device name
                                if (evdev_name[0] != '\0'){
                                    if (scan_mode){print_stderr("'%s' : '%s'\n", evdev_path_tmp, evdev_name); //no device name provided, just output all devices and paths
                                    } else if (strcmp(evdev_name_search, evdev_name) == 0){strncpy(evdev_path_used, evdev_path_tmp, sizeof(evdev_path_used)); break;} //proper device found
                                }
                            }
                            free(folder_list);
                            if (scan_mode){print_stderr("scan finished\n"); evdev_retry = false;}
                        } else if (debug){print_stderr("'%s' folder is empty\n", evdev_path);}
                    } else if (S_ISREG(evdev_stat.st_mode)){ //given path is a file
                        evdev_fd = open(evdev_path, O_RDONLY);
                        if (evdev_fd < 0){if (debug){print_stderr("failed to open '%s'\n", evdev_path);} //failed to open file
                        } else {
                            ioctl(evdev_fd, EVIOCGNAME(sizeof(evdev_name)), evdev_name); close(evdev_fd); //get device name
                            if (evdev_name[0] != '\0'){
                                strncpy(evdev_path_used, evdev_path, sizeof(evdev_path_used));
                                strncpy(evdev_name_search, evdev_name, sizeof(evdev_name_search));
                            } else {if (debug){print_stderr("failed to detect device name for '%s'\n", evdev_path);} evdev_retry = false;}
                        }
                    } else if (debug){print_stderr("invalid file type for '%s'\n", evdev_path); evdev_retry = false;}
                } else if (debug){print_stderr("failed to open '%s'\n", evdev_path);}
            }
            evdev_fd = -1;

            if (evdev_path_used[0] != '\0'){ //"valid" device found
                evdev_fd = open(evdev_path_used, O_RDONLY);
                if (evdev_fd < 0){if (debug){print_stderr("failed to open '%s'\n", evdev_path_used);} //failed to open file
                } else {
                    evdev_name[0] = '\0'; ioctl(evdev_fd, EVIOCGNAME(sizeof(evdev_name)), evdev_name); //get device name
                    if (evdev_name[0] == '\0'){if (debug){print_stderr("failed to get device name for '%s'\n", evdev_path_used);} close(evdev_fd); evdev_fd = -1;
                    } else {
                        fcntl(evdev_fd, F_SETFL, fcntl(evdev_fd, F_GETFL) | O_NONBLOCK); //set fd to non blocking
                        if (debug){print_stderr("'%s' will be used for '%s' device\n", evdev_path_used, evdev_name);}
                    }
                }
            }

            if (evdev_fd == -1){
                if (evdev_name_search[0] != '\0' && debug){print_stderr("can't poll from '%s' device\n", evdev_name_search);}
                if (evdev_retry){if (debug){print_stderr("retry in %ds\n", evdev_check_interval);}} else {print_stderr("evdev routine disabled\n"); goto thread_close;}
            }

            recheck_start_time = loop_start_time;
        }

        if (evdev_fd != -1){
            int events_read = read(evdev_fd, &events, events_size);
            if (errno == ENODEV || errno == ENOENT || errno == EBADF){
                if (debug){print_stderr("failed to read from device '%s' (%s), try to reopen in %ds\n", evdev_name_search, evdev_path_used, evdev_check_interval);}
                close(evdev_fd); evdev_fd = -1; continue;
            } else if (events_read >= input_event_size){
                if (evdev_detected_start > 0. && loop_start_time - evdev_detected_start > evdev_sequence_detect_interval){ //reset sequence
                    memset(evdev_detected_sequence, 0, sizeof(evdev_detected_sequence));
                    evdev_detected_sequence_index = 0; evdev_detected_start = -1.;
                    if (debug){print_stderr("sequence timer reset\n");}
                }

                for (int i = 0; i < events_read / input_event_size; i++){
                    //printf("%ld.%06ld, type:%u, code:%u, value:%d\n", events[i].time.tv_sec, events[i].time.tv_usec, events[i].type, events[i].code, events[i].value);
                    int tmp_code = events[i].code;
                    #ifndef NO_OSD
                        bool code_osd_detect = (osd_evdev_sequence_limit == 0) ? false : in_array_int(osd_evdev_sequence, tmp_code, osd_evdev_sequence_limit) != -1;
                    #else
                        bool code_osd_detect = false;
                    #endif
                    #ifndef NO_TINYOSD
                        bool code_tinyosd_detect = (tinyosd_evdev_sequence_limit == 0) ? false : in_array_int(tinyosd_evdev_sequence, tmp_code, tinyosd_evdev_sequence_limit) != -1;
                    #else
                        bool code_tinyosd_detect = false;
                    #endif

                    if (tmp_code != 0 && events[i].value != 0 && (code_osd_detect || code_tinyosd_detect)){ //keycode in osd or tiny osd sequence
                        if (evdev_detected_start < 0){ //check not started
                            evdev_detected_start = loop_start_time;
                            if (debug){print_stderr("sequence timer start\n");}
                        }
                        if (loop_start_time - evdev_detected_start < evdev_sequence_detect_interval && in_array_int(evdev_detected_sequence, tmp_code, evdev_sequence_max * 2) == -1){ //still in detection interval and not in detected sequence
                            evdev_detected_sequence[evdev_detected_sequence_index++] = tmp_code;
                            if (debug){print_stderr("%d added to detected sequence\n", tmp_code);}
                        }
                    }
                }

                #if !(defined(NO_OSD) && defined(NO_TINYOSD))
                int tmp_detected_count = 0;
                #endif
                #ifndef NO_OSD
                for (int i = 0; i < osd_evdev_sequence_limit; i++){if (osd_evdev_sequence[i] != 0 && in_array_int(evdev_detected_sequence, osd_evdev_sequence[i], evdev_sequence_max * 2) != -1){tmp_detected_count++;}} //check osd trigger
                if (tmp_detected_count == osd_evdev_sequence_limit){
                    if (debug){print_stderr("osd triggered\n");}
                    osd_start_time = loop_start_time;
                    evdev_detected_start = 1.;
                } else {
                    tmp_detected_count = 0;
                #endif
                #ifndef NO_TINYOSD
                    for (int i = 0; i < tinyosd_evdev_sequence_limit; i++){if (tinyosd_evdev_sequence[i] != 0 && in_array_int(evdev_detected_sequence, tinyosd_evdev_sequence[i], evdev_sequence_max * 2) != -1){tmp_detected_count++;}} //check tiny osd trigger
                    if (tmp_detected_count == tinyosd_evdev_sequence_limit){
                        if (debug){print_stderr("tiny osd triggered\n");}
                        tinyosd_start_time = loop_start_time;
                        evdev_detected_start = 1.;
                    }
                #endif
                #ifndef NO_OSD
                }
                #endif
            }
        }

        double loop_end_time = get_time_double();
        if (loop_end_time - loop_start_time < update_interval){usleep((useconds_t) ((update_interval - (loop_end_time - loop_start_time)) * 1000000.));} //limit update rate
    }

    thread_close:;
    if (evdev_fd != -1){close(evdev_fd);} //close opened fd
    print_stderr("thread closed\n"); evdev_thread_started = false; pthread_cancel(evdev_thread); //close thread
    return NULL;
}
#endif


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
        if (hex1){buffer[0] = buffer[1] = html_color[i];} else {buffer[0] = html_color[2*i]; buffer[1] = html_color[2*i+1];} //1 - 2 hex per color
        sscanf(buffer, "%x", &buffer_int);
        if (i==3){buffer_int = 255 - buffer_int;} //reverse alpha
        *rgba |= (uint32_t)buffer_int << ((8 * i));
    }

    print_stderr("html_color:'%s', uint32:'%x', len:%d, hex per color:%d.\n", html_color, *rgba, len, hex1?1:2);
    return true;
}

static void tty_signal_handler(int sig){ //handle signal func
    if (debug){print_stderr("DEBUG: signal received: %d.\n", sig);}
    if (sig == SIGUSR1){
        #if !defined(NO_SIGNAL) && !defined(NO_OSD)
            osd_start_time = get_time_double(); //full osd start time
        #endif
    } else if (sig == SIGUSR2){
        #if !defined(NO_SIGNAL) && !defined(NO_TINYOSD)
            tinyosd_start_time = get_time_double(); //tiny osd
        #endif
    } else {kill_requested = true;}
}

static void program_close(void){ //regroup all close functs
    if (already_killed){return;}
    if (strlen(pid_path) > 0){remove(pid_path);} //delete pid file

    #ifndef NO_OSD
        if (osd_buffer_ptr != NULL){free(osd_buffer_ptr); osd_buffer_ptr = NULL;} //free osd buffer
    #endif
    #ifndef NO_TINYOSD
        if (tinyosd_buffer_ptr != NULL){free(tinyosd_buffer_ptr); tinyosd_buffer_ptr = NULL;} //free tiny osd buffer
    #endif
    #ifndef NO_BATTERY_ICON
        if (lowbat_buffer_ptr != NULL){free(lowbat_buffer_ptr); lowbat_buffer_ptr = NULL;} //free low batt buffer
    #endif
    #ifndef NO_CPU_ICON
        if (cputemp_buffer_ptr != NULL){free(cputemp_buffer_ptr); cputemp_buffer_ptr = NULL;} //free cpu temp buffer
    #endif

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

#if !(defined(NO_BATTERY_ICON) && defined(NO_CPU_ICON))
    fprintf(stderr,"\nWarning icons:\n"
    #ifndef NO_BATTERY_ICON
        "\t-lowbat_test (force display of low battery icon, for test purpose).\n"
    #endif
    #ifndef NO_CPU_ICON
        "\t-cputemp_test (force display of CPU temperature warning icon, for test purpose).\n"
    #endif
    "\t-icons_pos tl/tr/bl/br (top left,right, bottom left,right. Default:%s).\n"
    "\t-icons_height <1-100> (icon height, percent of screen height. Default:%d).\n"
    , warn_icons_pos_str, warn_icons_height_percent);
#endif

    fprintf(stderr,"\nLow battery management:\n"
    "\t-battery_rsoc <PATH> (file containing battery percentage. Default:'%s').\n"
    "\t-battery_voltage <PATH> (file containing battery voltage. Default:'%s').\n"
    "\t-battery_volt_divider <NUM> (voltage divider to get voltage. Default:'%u').\n"
    "\t-lowbat_limit <0-90> (threshold, used with -battery_rsoc. Default:%d).\n"
    , battery_rsoc_path, battery_volt_path, battery_volt_divider, lowbat_limit);
#ifndef NO_GPIO
    fprintf(stderr,
    "\t-lowbat_gpio <PIN> (low battery gpio pin, -1 to disable. Default:%d).\n"
    "\t-lowbat_gpio_reversed <0-1> (1 for active low. Default:%d).\n"
    , lowbat_gpio, lowbat_gpio_reversed?1:0);
#endif

#ifndef NO_EVDEV
    fprintf(stderr,"\nEVDEV input:\n"
    "\t-evdev_path <PATH> (folder or file to use as input device. Default:'%s').\n"
    "\t-evdev_device <NAME> (device to search if -evdev_path is a folder. Default:'%s').\n"
    "\t-evdev_failure_interval <NUM> (retry interval if input device failed. Default:'%d').\n"
    "\t-evdev_detect_interval <NUM> (input sequence detection timeout in millisec. Default:'%d').\n"
    , evdev_path, evdev_name_search, evdev_check_interval, evdev_sequence_detect_interval_ms);
#ifndef NO_OSD
    fprintf(stderr,"\t-evdev_osd_sequence <KEYCODE,KEYCODE,...> (OSD trigger sequence. Default:'%s').\n", osd_evdev_sequence_char);
#endif
#ifndef NO_TINYOSD
    fprintf(stderr,"\t-evdev_tinyosd_sequence <KEYCODE,KEYCODE,...> (Tiny OSD trigger sequence. Default:'%s').\n", tinyosd_evdev_sequence_char);
#endif
#endif

#ifndef NO_OSD
    fprintf(stderr,
    "\nOSD display:\n"
    "\t-osd_test (full OSD display, for test purpose).\n"
    #ifndef NO_SIGNAL_FILE
    "\t-signal_file <PATH> (useful if you can't send signal to program. Should only contain '0', SIGUSR1 or SIGUSR2 value.).\n"
    #endif
    );
    #ifndef NO_GPIO
    fprintf(stderr,
    "\t-osd_gpio <PIN> (OSD trigger gpio pin, -1 to disable. Default:%d).\n"
    "\t-osd_gpio_reversed <0-1> (1 for active low. Default:%d).\n"
    , osd_gpio, osd_gpio_reversed?1:0);
    #endif
    fprintf(stderr, 
    "\t-osd_max_lines <1-999> (absolute lines count limit on screen. Default:%d).\n"
    "\t-osd_text_padding <0-100> (text distance (px) to screen border. Default:%d).\n"
    "\t-osd_test (full OSD display, for test purpose).\n"
    , osd_max_lines, osd_text_padding);
#endif

#if !(defined(NO_OSD) && defined(NO_TINYOSD))
    fprintf(stderr,
    "\nOSD styling:\n"
    "\t-timeout <1-20> (Hide OSD after given duration. Default:%d).\n"
    "\t-bg_color <RGB,RGBA> (background color. Default:%s).\n"
    "\t-text_color <RGB,RGBA> (text color. Default:%s).\n"
    "\t-warn_color <RGB,RGBA> (warning text color. Default:%s).\n"
    "\t-crit_color <RGB,RGBA> (critical text color. Default:%s).\n"
    "Note: <RGB,RGBA> uses html format (excl. # char.), allow both 1 or 2 hex per channel.\n"
    , osd_timeout, osd_color_bg_str, osd_color_text_str, osd_color_warn_str, osd_color_crit_str);
#endif

#ifndef NO_TINYOSD
    fprintf(stderr,
    "\tTiny OSD specific:\n"
    "\t-tinyosd_test (Tiny OSD display, for test purpose).\n"
    "\t-tinyosd_position <t/b> (top, bottom. Default:%s).\n"
    "\t-tinyosd_height <1-100> (OSD height, percent of screen height. Default:%d).\n"
    , tinyosd_pos_str, tinyosd_height_percent);
    #ifndef NO_GPIO
    fprintf(stderr,
    "\t-tinyosd_gpio <PIN> (OSD trigger gpio pin, -1 to disable. Default:%d).\n"
    "\t-tinyosd_gpio_reversed <0-1> (1 for active low. Default:%d).\n"
    , tinyosd_gpio, tinyosd_gpio_reversed?1:0);
    #endif
#endif

    fprintf(stderr,
    "\nOSD data:\n"
    "\t-rtc <PATH> (if invalid, uptime will be used. Default:'%s').\n"
    "\t-cpu_thermal <PATH> (file containing CPU temperature. Default:'%s').\n"
    "\t-cpu_thermal_divider <NUM> (divider to get actual temperature. Default:'%u').\n"
    "\t-backlight <PATH> (file containing backlight current value. Default:'%s').\n"
    "\t-backlight_max <PATH> (file containing backlight maximum value. Default:'%s').\n"
    , rtc_path, cpu_thermal_path, cpu_thermal_divider, backlight_path, backlight_max_path);

    fprintf(stderr,
    "\nProgram:\n"
    "\t-check <1-120> (check rate in hz. Default:%d).\n"
    "\t-display <0-255> (Dispmanx display. Default:%u).\n"
    "\t-layer <NUM> (Dispmanx layer. Default:%u).\n"
    "\t-debug <0-1> (enable stderr debug output. Default:%d).\n"
    , osd_check_rate, display_number, osd_layer, debug?1:0);

#ifdef BUFFER_PNG_EXPORT
    fprintf(stderr,
    "\t-buffer_png_export (export all drawn buffers to png files into debug_export folder. Default:%s).\n"
    , debug_buffer_png_export?"on":"off");
#endif
}

int main(int argc, char *argv[]){
    program_start_time = get_time_double(); //program start time, used for detailed outputs

    //test vars
    #ifndef NO_OSD
        bool osd_test = false;
    #endif
    #ifndef NO_TINYOSD
        bool tinyosd_test = false;
    #endif
    #ifndef NO_BATTERY_ICON
        bool lowbat_test = false;
    #endif
    #ifndef NO_CPU_ICON
        bool cputemp_test = false;
    #endif

    program_get_path(argv, program_path, program_name); //get current program path and filename

    //program args parse, plus some failsafes
    for(int i=1; i<argc; i++){
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0){program_usage(); return EXIT_SUCCESS;

        //Warning icons
#if !(defined(NO_BATTERY_ICON) && defined(NO_CPU_ICON))
        } else if (strcmp(argv[i], "-icons_pos") == 0){strncpy(warn_icons_pos_str, argv[++i], sizeof(warn_icons_pos_str));
        } else if (strcmp(argv[i], "-icons_height") == 0){warn_icons_height_percent = atoi(argv[++i]);
            if (int_constrain(&warn_icons_height_percent, 1, 100) != 0){print_stderr("invalid -icons_height argument, reset to '%d', allow from '1' to '100' (incl.)\n", warn_icons_height_percent);}
    #ifndef NO_BATTERY_ICON
        } else if (strcmp(argv[i], "-lowbat_test") == 0){lowbat_test = true; print_stderr("low battery icon will be displayed until program closes\n");
    #endif
    #ifndef NO_CPU_ICON
        } else if (strcmp(argv[i], "-cputemp_test") == 0){cputemp_test = true; print_stderr("cpu temperature warning icon will be displayed until program closes\n");
    #endif
#endif

        //Low battery management
        } else if (strcmp(argv[i], "-battery_rsoc") == 0){strncpy(battery_rsoc_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-battery_voltage") == 0){strncpy(battery_volt_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-battery_volt_divider") == 0){battery_volt_divider = atoi(argv[++i]);
            if (battery_volt_divider == 0){print_stderr("invalid -battery_volt_divider argument, reset to '1', value needs to be over 0\n"); battery_volt_divider = 1;}
        } else if (strcmp(argv[i], "-lowbat_limit") == 0){lowbat_limit = atoi(argv[++i]);
            if (int_constrain(&lowbat_limit, 0, 90) != 0){print_stderr("invalid -lowbat_limit argument, reset to '%d', allow from '0' to '90' (incl.)\n", lowbat_limit);}
#ifndef NO_GPIO
        } else if (strcmp(argv[i], "-lowbat_gpio") == 0){lowbat_gpio = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-lowbat_gpio_reversed") == 0){lowbat_gpio_reversed = atoi(argv[++i]) > 0;
#endif

        //EVDEV input
#ifndef NO_EVDEV
        } else if (strcmp(argv[i], "-evdev_path") == 0){strncpy(evdev_path, argv[++i], sizeof(evdev_path));
        } else if (strcmp(argv[i], "-evdev_device") == 0){strncpy(evdev_name_search, argv[++i], sizeof(evdev_name_search));
        } else if (strcmp(argv[i], "-evdev_failure_interval") == 0){evdev_check_interval = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-evdev_detect_interval") == 0){evdev_sequence_detect_interval_ms = atoi(argv[++i]);
    #ifndef NO_OSD
        } else if (strcmp(argv[i], "-evdev_osd_sequence") == 0){strncpy(osd_evdev_sequence_char, argv[++i], sizeof(osd_evdev_sequence_char));
    #endif
    #ifndef NO_TINYOSD
        } else if (strcmp(argv[i], "-evdev_tinyosd_sequence") == 0){strncpy(tinyosd_evdev_sequence_char, argv[++i], sizeof(tinyosd_evdev_sequence_char));
    #endif
#endif

        //OSD display
#ifndef NO_OSD
        } else if (strcmp(argv[i], "-osd_test") == 0){osd_test = true; print_stderr("full OSD will be displayed until program closes\n");
    #ifndef NO_SIGNAL_FILE
        } else if (strcmp(argv[i], "-signal_file") == 0){strncpy(signal_path, argv[++i], PATH_MAX-1);
    #endif
    #ifndef NO_GPIO
        } else if (strcmp(argv[i], "-osd_gpio") == 0){osd_gpio = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-osd_gpio_reversed") == 0){osd_gpio_reversed = atoi(argv[++i]) > 0;
    #endif
        } else if (strcmp(argv[i], "-osd_max_lines") == 0){osd_max_lines = atoi(argv[++i]);
            if (int_constrain(&osd_max_lines, 1, 999) != 0){print_stderr("invalid -osd_max_lines argument, reset to '%d', allow from '1' to '999' (incl.)\n", osd_max_lines);}
        } else if (strcmp(argv[i], "-osd_text_padding") == 0){osd_text_padding = atoi(argv[++i]);
            if (int_constrain(&osd_text_padding, 0, 100) != 0){print_stderr("invalid -osd_text_padding argument, reset to '%d', allow from '0' to '100' (incl.)\n", osd_text_padding);}
#endif

        //OSD styling
#if !(defined(NO_OSD) && defined(NO_TINYOSD))
        } else if (strcmp(argv[i], "-timeout") == 0){osd_timeout = atoi(argv[++i]);
            if (int_constrain(&osd_timeout, 1, 20) != 0){print_stderr("invalid -timeout argument, reset to '%d', allow from '1' to '20' (incl.)\n", osd_timeout);}
        } else if (strcmp(argv[i], "-bg_color") == 0){strncpy(osd_color_bg_str, argv[++i], sizeof(osd_color_bg_str));
        } else if (strcmp(argv[i], "-text_color") == 0){strncpy(osd_color_text_str, argv[++i], sizeof(osd_color_text_str));
        } else if (strcmp(argv[i], "-warn_color") == 0){strncpy(osd_color_warn_str, argv[++i], sizeof(osd_color_warn_str));
        } else if (strcmp(argv[i], "-crit_color") == 0){strncpy(osd_color_crit_str, argv[++i], sizeof(osd_color_crit_str));
#endif

        //Tiny OSD specific
#ifndef NO_TINYOSD
        } else if (strcmp(argv[i], "-tinyosd_test") == 0){tinyosd_test = true; print_stderr("tiny OSD will be displayed until program closes\n");
        } else if (strcmp(argv[i], "-tinyosd_position") == 0){strncpy(tinyosd_pos_str, argv[++i], sizeof(tinyosd_pos_str));
        } else if (strcmp(argv[i], "-tinyosd_height") == 0){tinyosd_height_percent = atoi(argv[++i]);
            if (int_constrain(&tinyosd_height_percent, 1, 100) != 0){print_stderr("invalid -tinyosd_height argument, reset to '%d', allow from '1' to '100' (incl.)\n", tinyosd_height_percent);}
    #ifndef NO_GPIO
        } else if (strcmp(argv[i], "-tinyosd_gpio") == 0){tinyosd_gpio = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-tinyosd_gpio_reversed") == 0){tinyosd_gpio_reversed = atoi(argv[++i]) > 0;
    #endif
#endif

        //OSD data
        } else if (strcmp(argv[i], "-rtc") == 0){strncpy(rtc_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-cpu_thermal") == 0){strncpy(cpu_thermal_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-cpu_thermal_divider") == 0){cpu_thermal_divider = atoi(argv[++i]);
            if (cpu_thermal_divider == 0){print_stderr("invalid -cpu_thermal_divider argument, reset to '1', value needs to be over 0\n"); cpu_thermal_divider = 1;}
        } else if (strcmp(argv[i], "-backlight") == 0){strncpy(backlight_path, argv[++i], PATH_MAX-1);
        } else if (strcmp(argv[i], "-backlight_max") == 0){strncpy(backlight_max_path, argv[++i], PATH_MAX-1);

        //Program
        } else if (strcmp(argv[i], "-display") == 0){display_number = atoi(argv[++i]);
            if (int_constrain(&display_number, 0, 255) != 0){print_stderr("invalid -display argument, reset to '%d', allow from '0' to '255' (incl.)\n", display_number);}
        } else if (strcmp(argv[i], "-layer") == 0){osd_layer = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-check") == 0){osd_check_rate = atoi(argv[++i]);
            if (int_constrain(&osd_check_rate, 1, 120) != 0){print_stderr("invalid -check argument, reset to '%d', allow from '1' to '120' (incl.)\n", osd_check_rate);}
        } else if (strcmp(argv[i], "-debug") == 0){debug = atoi(argv[++i]) > 0;
#ifdef BUFFER_PNG_EXPORT
        } else if (strcmp(argv[i], "-buffer_png_export") == 0){debug_buffer_png_export = true;
#endif
        }
    }
    
    //charset to png export
    #ifdef CHARSET_EXPORT
        charset_export_png();
    #endif

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
    signal(SIGUSR2, tty_signal_handler); //use signal SIGUSR2 as trigger to display tiny OSD
    atexit(program_close); at_quick_exit(program_close); //run on program exit

    #ifdef NO_SIGNAL
        print_stderr("osd trigger using signal disabled at compilation time.\n");
    #endif
    #ifdef NO_SIGNAL_FILE
        print_stderr("osd trigger using file disabled at compilation time.\n");
    #endif
    #ifndef NO_EVDEV
        evdev_thread_started = pthread_create(&evdev_thread, NULL, evdev_routine, NULL) == 0; //create evdev routine thread
    #else
        print_stderr("osd trigger using evdev disabled at compilation time.\n");
    #endif

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
    print_stderr("dispmanx update test successful.\n");

    //convert html colors to usable colors
    if (!html_to_uint32_color(osd_color_bg_str, &osd_color_bg)){print_stderr("warning, invalid -bg_color argument.\n"); //background raw color
    } else { //text color backgound with half transparent to increase text contrast
        osd_color_text_bg = osd_color_bg;
        uint8_t *osd_color_text_bg_ptr = (uint8_t*)&osd_color_text_bg;
        *(osd_color_text_bg_ptr + 3) += (255 - *(osd_color_text_bg_ptr + 3)) / 2;
    }
    if (!html_to_uint32_color(osd_color_warn_str, &osd_color_warn)){print_stderr("warning, invalid -warn_color argument.\n");} //warning text raw color
    if (!html_to_uint32_color(osd_color_crit_str, &osd_color_crit)){print_stderr("warning, invalid -crit_color argument.\n");} //critical text raw color
    if (!html_to_uint32_color(osd_color_text_str, &osd_color_text)){print_stderr("warning, invalid -text_color argument.\n"); //text raw color
    } else { //compute separator color text to bg midpoint
        uint8_t *osd_color_separator_ptr = (uint8_t*)&osd_color_separator, *osd_color_text_ptr = (uint8_t*)&osd_color_text, *osd_color_bg_ptr = (uint8_t*)&osd_color_bg;
        for (uint8_t i=0; i<4; i++){
            uint16_t tmp = (*(osd_color_text_ptr + i) + *(osd_color_bg_ptr + i)) / 2;
            *(osd_color_separator_ptr+i) = (uint8_t)tmp;
        }
    }

    #ifndef NO_GPIO //gpio
        if (lowbat_gpio > -1 || osd_gpio > -1 || tinyosd_gpio > -1){gpio_init();}
    #else
        print_stderr("gpio features disabled at compilation time.\n");
    #endif

    //warning icons
    #if !(defined(NO_BATTERY_ICON) && defined(NO_CPU_ICON))
        #define icons_count 2 //max amount of displayed icons at once
        int32_t icons_height = (double)display_height * (double)warn_icons_height_percent / 100.; //final icon height
        int32_t icons_org_width[icons_count]={0}, icons_org_height[icons_count]={0}, icons_x[icons_count]={0}, icons_width[icons_count]={0}; //original size, final x, width
        int32_t icons_y = icons_padding, icons_y_dir = 1; //start y position, y direction
        if (warn_icons_pos_str[0]!='t'){icons_y = display_height - icons_padding - icons_height; icons_y_dir = -1;} //bottom alignment
        VC_RECT_T icons_dest_rect = {.height = icons_height};
        uint8_t icon_index = 0;
    #endif

    #ifndef NO_BATTERY_ICON //low battery icon:0
        bool lowbat_trigger = false, lowbat_displayed = false; //low battery icon displayed
        VC_RECT_T lowbat_rect = {0};
        DISPMANX_ELEMENT_HANDLE_T lowbat_element = 0;
        DISPMANX_RESOURCE_HANDLE_T lowbat_resource = dispmanx_resource_create_from_png(lowbat_img_file, &lowbat_rect);
        if (lowbat_resource > 0){
            icons_width[icon_index] = (double)icons_height * ((double)lowbat_rect.width / (double)lowbat_rect.height); //final width
            if (warn_icons_pos_str[1]=='l'){icons_x[icon_index] = icons_padding; //left alignement
            } else {icons_x[icon_index] = display_width - icons_padding - icons_width[icon_index];} //right alignement
            icons_org_width[icon_index] = lowbat_rect.width; icons_org_height[icon_index] = lowbat_rect.height; //backup original resolution
            vc_dispmanx_rect_set(&lowbat_rect, 0, 0, lowbat_rect.width << 16, lowbat_rect.height << 16);
        } else {print_stderr("low battery icon disabled\n");}
        icon_index++;
    #else
        print_stderr("low battery icon disabled at compilation time.\n");
    #endif

    #ifndef NO_CPU_ICON //cpu temperature icon:1
        bool cputemp_trigger = false, cputemp_displayed = false; //cpu temp icon displayed
        VC_RECT_T cputemp_rect = {0};
        DISPMANX_ELEMENT_HANDLE_T cputemp_element = 0;
        DISPMANX_RESOURCE_HANDLE_T cputemp_resource = dispmanx_resource_create_from_png(cputemp_img_file, &cputemp_rect);
        if (cputemp_resource > 0){
            icons_width[icon_index] = (double)icons_height * ((double)cputemp_rect.width / (double)cputemp_rect.height); //final width
            if (warn_icons_pos_str[1]=='l'){icons_x[icon_index] = icons_padding; //left alignement
            } else {icons_x[icon_index] = display_width - icons_padding - icons_width[icon_index];} //right alignement
            icons_org_width[icon_index] = cputemp_rect.width; icons_org_height[icon_index] = cputemp_rect.height; //backup original resolution
            vc_dispmanx_rect_set(&cputemp_rect, 0, 0, cputemp_rect.width << 16, cputemp_rect.height << 16);
        } else {print_stderr("cpu temperature warning icon disabled\n");}
        icon_index++;
    #else
        print_stderr("cpu overheat icon disabled at compilation time.\n");
    #endif

    //osd
    #ifndef NO_OSD
        double osd_scaling = (double)display_height / (osd_text_padding * 2 + osd_max_lines * RASPIDMX_FONT_HEIGHT);
        int osd_width = ALIGN_TO_16((int)(display_width / osd_scaling)), osd_height = ALIGN_TO_16((int)(display_height / osd_scaling));
        print_stderr("osd resolution: %dx%d (%.4lfx)\n", osd_width, osd_height, (double)osd_width/display_width);
        DISPMANX_ELEMENT_HANDLE_T osd_element = 0;
        DISPMANX_RESOURCE_HANDLE_T osd_resource = vc_dispmanx_resource_create(VC_IMAGE_RGBA32, osd_width, osd_height, &vc_image_ptr);
    #else
        print_stderr("full screen osd disabled at compilation time.\n");
    #endif

    //tiny osd
    #ifndef NO_TINYOSD
        int tinyosd_y = 0, tinyosd_height_dest = (double)display_height * ((double)tinyosd_height_percent / 100);
        double tinyosd_downsizing = (double)tinyosd_height_dest / RASPIDMX_FONT_HEIGHT;
        int tinyosd_width = ALIGN_TO_16((int)((double)display_width / tinyosd_downsizing)), tinyosd_height = ALIGN_TO_16(RASPIDMX_FONT_HEIGHT);
        print_stderr("tiny osd resolution: %dx%d (%.4lf)\n", tinyosd_width, tinyosd_height, tinyosd_downsizing);
        DISPMANX_ELEMENT_HANDLE_T tinyosd_element = 0;
        DISPMANX_RESOURCE_HANDLE_T tinyosd_resource = vc_dispmanx_resource_create(VC_IMAGE_RGBA32, tinyosd_width, tinyosd_height, &vc_image_ptr);
        if (tinyosd_resource > 0 && tinyosd_pos_str[0]=='b'){tinyosd_y = display_height - tinyosd_height_dest;} //footer alignment
    #else
        print_stderr("tiny osd disabled at compilation time.\n");
    #endif

    //main loop
    print_stderr("starting main loop\n");

    double osd_update_interval = 1. / osd_check_rate;
    double gpio_check_start_time = -1, sec_check_start_time = -1.; //gpio, seconds interval check start time
    #if !defined(NO_SIGNAL_FILE) && !(defined(NO_OSD) && defined(NO_TINYOSD))
        bool signal_file_used = false; //signal read from a file
    #endif
    bool icon_update = false; //warning icon update trigger

    while (!kill_requested){ //main loop
        double loop_start_time = get_time_double(); //loop start time
        dispmanx_update = vc_dispmanx_update_start(0); //start vc update
        #if !defined(NO_SIGNAL_FILE) && !(defined(NO_OSD) && defined(NO_TINYOSD))
            if (!signal_file_used && signal_path[0] != '\0' && access(signal_path, R_OK) && osd_start_time < 0. && tinyosd_start_time < 0.){ //check signal file value
                int tmp_sig = 0; FILE *filehandle = fopen(signal_path, "r"); if (filehandle != NULL){fscanf(filehandle, "%d", &tmp_sig); fclose(filehandle);}
                if (tmp_sig == SIGUSR1){osd_start_time = loop_start_time; signal_file_used = true; //full osd start time
                } else if (tmp_sig == SIGUSR2){tinyosd_start_time = loop_start_time; signal_file_used = true;} //tiny osd
            }
        #endif

        if (loop_start_time - gpio_check_start_time > 0.25){ //check gpio 4 times a sec
            #if !defined(NO_GPIO) && !(defined(NO_OSD) && defined(NO_TINYOSD))
                if (osd_start_time < 0 && gpio_check(1)){osd_start_time = loop_start_time;} //osd gpio trigger
                if (tinyosd_start_time < 0 && gpio_check(2)){tinyosd_start_time = loop_start_time;} //tiny osd gpio trigger
            #endif
            if (loop_start_time - sec_check_start_time > 1.){ //warning trigger every seconds
                #if !defined(NO_GPIO) && !defined(NO_BATTERY_ICON)
                    lowbat_trigger = gpio_check(0);
                #endif

                #ifndef NO_BATTERY_ICON
                    lowbat_trigger = lowbat_trigger || lowbat_sysfs() || lowbat_test;
                #elif !(defined(NO_OSD) && defined(NO_TINYOSD))
                    lowbat_sysfs();
                #endif

                #ifndef NO_CPU_ICON
                    cputemp_trigger = cputemp_sysfs() || cputemp_test;
                #elif !(defined(NO_OSD) && defined(NO_TINYOSD))
                    cputemp_sysfs();
                #endif

                icon_update = true;
            }
            gpio_check_start_time = loop_start_time;
        }

        //full osd
        #ifndef NO_OSD
            if (osd_test){osd_start_time = loop_start_time;}
            if (osd_resource > 0 && osd_start_time > 0){
                if (loop_start_time - osd_start_time > (double)osd_timeout){ //osd timeout
                    if (osd_element > 0){vc_dispmanx_element_remove(dispmanx_update, osd_element); osd_element = 0;}
                    #ifndef NO_SIGNAL_FILE
                        if (signal_file_used){FILE *filehandle = fopen(signal_path, "w"); if (filehandle != NULL){fputc('0', filehandle); fclose(filehandle);} signal_file_used = false;}
                    #endif
                    osd_start_time = -1.;
                } else if (tinyosd_start_time < 0){ //only if tiny osd not displayed
                    osd_build_element(osd_resource, &osd_element, dispmanx_update, osd_width, osd_height, 0, 0, display_width, display_height);
                }
            }
        #endif

        //tiny osd
        #ifndef NO_TINYOSD
            if (tinyosd_test){tinyosd_start_time = loop_start_time;}
            if (tinyosd_resource > 0 && tinyosd_start_time > 0){
                if (loop_start_time - tinyosd_start_time > (double)osd_timeout){ //osd timeout
                    if (tinyosd_element > 0){vc_dispmanx_element_remove(dispmanx_update, tinyosd_element); tinyosd_element = 0;}
                    #ifndef NO_SIGNAL_FILE
                        if (signal_file_used){FILE *filehandle = fopen(signal_path, "w"); if (filehandle != NULL){fputc('0', filehandle); fclose(filehandle);} signal_file_used = false;}
                    #endif
                    tinyosd_start_time = -1.;
                } else if (osd_start_time < 0 || tinyosd_test){ //only if full osd not displayed
                    tinyosd_build_element(tinyosd_resource, &tinyosd_element, dispmanx_update, tinyosd_width, tinyosd_height, 0, tinyosd_y, display_width, tinyosd_height_dest);
                }
            }
        #endif

        //warning icons
        #if !(defined(NO_BATTERY_ICON) && defined(NO_CPU_ICON))
            icon_index = 0;
            icons_dest_rect.y = icons_y; //reset final y position
        #endif

        #ifndef NO_BATTERY_ICON
            if (lowbat_resource > 0){ //low battery icon
                if (lowbat_trigger){
                    if (icon_update){
                        if (battery_rsoc >= 0){ //update dynamic icon
                            lowbatt_build_element(lowbat_resource, &lowbat_element, dispmanx_update, icons_org_width[icon_index], icons_org_height[icon_index], icons_x[icon_index], icons_dest_rect.y, icons_width[icon_index], icons_height);
                        } else if (!lowbat_displayed && lowbat_element == 0){ //display static icon once
                            icons_dest_rect.x = icons_x[icon_index]; icons_dest_rect.width = icons_width[icon_index]; //icon x/width
                            lowbat_element = vc_dispmanx_element_add(dispmanx_update, dispmanx_display, osd_layer + 2, &icons_dest_rect, lowbat_resource, &lowbat_rect, DISPMANX_PROTECTION_NONE, &dispmanx_alpha_from_src, NULL, DISPMANX_NO_ROTATE);
                        }
                        lowbat_displayed = true;
                    }
                    icons_dest_rect.y += icons_height * icons_y_dir; //next icon y position
                } else if (lowbat_displayed){ //remove icon
                    if (lowbat_element > 0){vc_dispmanx_element_remove(dispmanx_update, lowbat_element); lowbat_element = 0;}
                    lowbat_displayed = false;
                }
            }
            icon_index++;
        #endif

        #ifndef NO_CPU_ICON
            if (cputemp_resource > 0){ //cpu temp icon
                if (cputemp_trigger){
                    if (icon_update){
                        cputemp_build_element(cputemp_resource, &cputemp_element, dispmanx_update, icons_org_width[icon_index], icons_org_height[icon_index], icons_x[icon_index], icons_dest_rect.y, icons_width[icon_index], icons_height);
                        cputemp_displayed = true;
                    }
                    icons_dest_rect.y += icons_height * icons_y_dir; //next icon y position
                } else if (cputemp_displayed){ //remove icon
                    if (cputemp_element > 0){vc_dispmanx_element_remove(dispmanx_update, cputemp_element); cputemp_element = 0;}
                    cputemp_displayed = false;
                }
            }
            icon_index++;
        #endif

        if (icon_update){sec_check_start_time = loop_start_time; icon_update = false;} //disable icon update until next loop

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
    #ifndef NO_BATTERY_ICON
        if (lowbat_element > 0){vc_dispmanx_element_remove(dispmanx_update, lowbat_element);} //remove low battery icon
    #endif
    #ifndef NO_CPU_ICON
        if (cputemp_element > 0){vc_dispmanx_element_remove(dispmanx_update, cputemp_element);} //remove cpu icon
    #endif
    #ifndef NO_OSD
        if (osd_element > 0){vc_dispmanx_element_remove(dispmanx_update, osd_element);} //remove osd
    #endif
    #ifndef NO_TINYOSD
        if (tinyosd_element > 0){vc_dispmanx_element_remove(dispmanx_update, tinyosd_element);} //remove tiny osd
    #endif
    vc_dispmanx_update_submit_sync(dispmanx_update); //push vc update
    #ifndef NO_BATTERY_ICON
        if (lowbat_resource > 0){vc_dispmanx_resource_delete(lowbat_resource);}
    #endif
    #ifndef NO_CPU_ICON
        if (cputemp_resource > 0){vc_dispmanx_resource_delete(cputemp_resource);}
    #endif
    #ifndef NO_OSD
        if (osd_resource > 0){vc_dispmanx_resource_delete(osd_resource);}
    #endif
    #ifndef NO_TINYOSD
        if (tinyosd_resource > 0){vc_dispmanx_resource_delete(tinyosd_resource);}
    #endif

    //gpiod
    #ifdef USE_GPIOD
        for (int i=0; i<gpio_pins_count; i++){if (gpiod_input_line[i] != NULL){gpiod_line_release(gpiod_input_line[i]);}}
    #endif

    return EXIT_SUCCESS;
}
