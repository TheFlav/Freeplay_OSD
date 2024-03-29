/*
FreeplayTech On-screen (heads-up) display overlay

Main header file.
Please refer to fp_osd.c for more informations.
*/

//#define NO_GPIO 1 //debug
//#define NO_SIGNAL 1 //debug
//#define NO_SIGNAL_FILE 1 //debug
//#define NO_EVDEV 1 //debug
//#define BUFFER_PNG_EXPORT 1 //debug
//#define NO_OSD 1 //debug
//#define NO_TINYOSD 1 //debug
//#define NO_BATTERY_ICON 1 //debug
//#define NO_CPU_ICON 1 //debug

#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <png.h>
#include "bcm_host.h"
#include "font.h"

#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#if defined(NO_OSD) && defined(NO_TINYOSD)
    #define NO_EVDEV 1
    #define NO_SIGNAL 1
    #define NO_SIGNAL_FILE 1
#endif
#ifndef NO_EVDEV
    #include <linux/input.h>
    #include <dirent.h>
#endif

#include "settings.h" //user settings vars

#if defined(NO_BATTERY_ICON) && defined(NO_CPU_ICON) && defined(NO_OSD) && defined(NO_TINYOSD)
    #error With currently defined preprocessor variables, program will never display anything...
#endif

#if defined(CHARSET_EXPORT) && !defined(BUFFER_PNG_EXPORT)
    #define BUFFER_PNG_EXPORT 1
#endif

//gpio library
#ifndef NO_GPIO
    #define gpio_pins_count 3 //amount of pins to monitor
    #if defined(USE_WIRINGPI) && defined(USE_GPIOD) 
        #error "Only one kind of gpio library allowed at once, please refer to README.md for more informations."
    #elif defined(USE_WIRINGPI)
        #include <wiringPi.h>
    #elif defined(USE_GPIOD)
        #include <gpiod.h>
        struct gpiod_chip *gpiod_chip;
        struct gpiod_line *gpiod_input_line[gpio_pins_count];
        int gpiod_fd[gpio_pins_count];
        char gpiod_consumer_name[gpio_pins_count][128];
    #endif
#else
    #undef USE_WIRINGPI
    #undef USE_GPIOD
#endif

//prototypes
static double get_time_double(void); //get time in double (seconds), takes around 82 microseconds to run

//static void raspidmx_setPixelRGBA32(void* /*buffer*/, int /*buffer_width*/, int32_t /*x*/, int32_t /*y*/, uint32_t /*color*/); //modified version from Raspidmx
static int32_t raspidmx_drawCharRGBA32(void* /*buffer*/, int /*buffer_width*/, int /*buffer_height*/, int32_t /*x*/, int32_t /*y*/, uint8_t /*c*/, uint8_t* /*font_ptr*/, uint32_t /*color*/); //modified version from Raspidmx, return end position of printed char
static VC_RECT_T raspidmx_drawStringRGBA32(void* /*buffer*/, int /*buffer_width*/, int /*buffer_height*/, int32_t /*x*/, int32_t /*y*/, const char* /*string*/, uint8_t* /*font_ptr*/, uint32_t /*color*/, uint32_t* /*outline_color*/); //modified version of Raspidmx drawStringRGB() function. Return end position of printed string, text box size

static void buffer_fill(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, uint32_t /*rgba_color*/); //fill buffer with given color
static void buffer_rectangle_fill(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, int32_t /*x*/, int32_t /*y*/, int32_t /*w*/, int32_t /*h*/, uint32_t /*rgba_color*/); //fill rectangle with given color
static void buffer_horizontal_line(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, int32_t /*x1*/, int32_t /*x2*/, int32_t /*y*/, uint32_t /*rgba_color*/); //draw horizontal line
static void buffer_vertical_line(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, int32_t /*x*/, int32_t /*y1*/, int32_t /*y2*/, uint32_t /*rgba_color*/); //draw vertical line
static uint32_t buffer_getcolor_rgba(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, int32_t /*x*/, int32_t /*y*/); //get specific color from buffer

#ifdef BUFFER_PNG_EXPORT
    static bool buffer_png_export(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, const char* /*filename*/); //export buffer to png, modified version of savePng() from Raspidmx
#endif

static DISPMANX_RESOURCE_HANDLE_T dispmanx_resource_create_from_png(char* /*filename*/, VC_RECT_T* /*image_rect_ptr*/); //create dispmanx ressource from png file, return 0 on failure, ressource handle on success

#ifndef NO_GPIO
    static void gpio_init(void); //init gpio things
    static bool gpio_check(int /*index*/); //check if gpio pin state
#endif

static bool lowbat_sysfs(void); //read sysfs power_supply battery capacity, return true if threshold, false if under or file not found
static bool cputemp_sysfs(void); //read sysfs cpu temperature, return true if threshold, false if under or file not found

static int int_constrain(int* /*val*/, int /*min*/, int /*max*/); //limit int value to given (incl) min and max value, return 0 if val within min and max, -1 under min, 1 over max
static bool html_to_uint32_color(char* /*html_color*/, uint32_t* /*rgba*/); //convert html color (3/4 or 6/8 hex) to uint32_t (alpha, blue, green, red)

static void tty_signal_handler(int /*sig*/); //handle signal func
static void program_close(void); //regroup all close functs
static void program_get_path(char** /*args*/, char* /*path*/, char* /*program*/); //get current program path based on program argv or getcwd if failed
static void program_usage(void); //display help

#ifndef NO_EVDEV
    static int in_array_int(int* /*arr*/, int /*value*/, int /*arr_size*/); //search in value in int array, return index or -1 on failure
    static void evdev_check(double /*loop_start_time*/); //evdev input check
#endif


//generic
const char program_version[] = "0.1a"; //program version
const char dev_webpage[] = "https://github.com/TheFlav/Freeplay_OSD"; //dev website
bool kill_requested = false, already_killed = false;
char program_path[PATH_MAX] = {'\0'}, program_name[PATH_MAX] = {'\0'}; //full path to this program
char pid_path[PATH_MAX] = {'\0'}; //full path to program pid file

//debug
#ifdef BUFFER_PNG_EXPORT
    bool debug_buffer_png_export = false; //export rgba buffers to png files, leave as is, use -buffer_png_export argument instead
#endif

//start time
double program_start_time = .0; //used for print output
#if !(defined(NO_OSD) && defined(NO_TINYOSD))
    double osd_start_time = -1., tinyosd_start_time = -1.; //osd start time
    bool osd_hold = false, tinyosd_hold = false; //one kind already displayed but other has to be displayed as well
#endif

//evdev thread specific
#ifndef NO_EVDEV
    int evdev_fd = -1;
    bool evdev_enabled = true; //event detection still enabled, defined during runtime
    char evdev_path_used[PATH_MAX] = ""; //event device path used, done that way to allow disconnect and reconnect of controller without failing evdev routine
#endif

//cpu data
int32_t cputemp_curr = -1, cputemp_disp = -1, cputemp_last = -2; //current cpu temperature
#ifndef NO_CPU_ICON
    uint32_t cputemp_icon_bg_color = 0xFF000000;
#endif

//battery data
#ifndef NO_BATTERY_ICON
    uint32_t lowbat_icon_bar_color = 0xFF000000, lowbat_icon_bar_bg_color = 0xFF000000;
#endif
int32_t battery_rsoc = -1, battery_rsoc_last = -2; //current battery percentage

//gpio, order:lowbatt, osd, tiny osd
#ifndef NO_GPIO
    bool gpio_external = false; //use external program to read gpio state
    bool gpio_enabled[gpio_pins_count] = {0}; //use gpio triggers, leave as is, defined during runtime
    int *gpio_pin[gpio_pins_count] = {&lowbat_gpio, &osd_gpio, &tinyosd_gpio}; //gpio pins, leave as is, defined during runtime
    bool *gpio_reversed[gpio_pins_count] = {&lowbat_gpio_reversed, &osd_gpio_reversed, &tinyosd_gpio_reversed}; //gpio signal reversed, leave as is, defined during runtime
#endif

//bitmap buffers
#ifndef NO_OSD
    void *osd_buffer_ptr = NULL; //bitmap buffer pointer
#endif
#ifndef NO_TINYOSD
    void *tinyosd_buffer_ptr = NULL; //bitmap buffer pointer
#endif
#ifndef NO_BATTERY_ICON
    void *lowbat_buffer_ptr = NULL; //bitmap buffer pointer
#endif
#ifndef NO_CPU_ICON
    void *cputemp_buffer_ptr = NULL; //bitmap buffer pointer
#endif

//dispmanx
#ifndef ALIGN_TO_16
	#define ALIGN_TO_16(x)((x + 15) & ~15)
#endif
DISPMANX_DISPLAY_HANDLE_T dispmanx_display = 0; //display handle
VC_DISPMANX_ALPHA_T dispmanx_alpha_from_src = {DISPMANX_FLAGS_ALPHA_FROM_SOURCE, 255, 0};
uint32_t vc_image_ptr; //only here because of how dispmanx works, not used
