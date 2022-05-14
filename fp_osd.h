/*
FreeplayTech On-screen (heads-up) display overlay

Main header file.
Please refer to fp_osd.c for more informations.

*/

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

//gpio library
#if defined(USE_WIRINGPI) && defined(USE_GPIOD) 
    #error "Only one kind of gpio library allowed at once, please refer to README.md for more informations."
#elif defined(USE_WIRINGPI)
    #include <wiringPi.h>
#elif defined(USE_GPIOD)
    #include <gpiod.h>
	struct gpiod_chip *gpiod_chip;
	struct gpiod_line *gpiod_input_line;
    int gpiod_fd = -1;
    char gpiod_consumer_name[128];
#endif


//prototypes
static double get_time_double(void); //get time in double (seconds), takes around 82 microseconds to run

static void raspidmx_setPixelRGBA32(void* /*buffer*/, int /*buffer_width*/, int32_t /*x*/, int32_t /*y*/, uint32_t /*color*/); //modified version from Raspidmx
static int32_t raspidmx_drawCharRGBA32(void* /*buffer*/, int /*buffer_width*/, int /*buffer_height*/, int32_t /*x*/, int32_t /*y*/, uint8_t /*c*/, uint8_t* /*font_ptr*/, uint32_t /*color*/, uint32_t* /*bg_color*/); //modified version from Raspidmx, return end position of printed char
static int32_t raspidmx_drawStringRGBA32(void* /*buffer*/, int /*buffer_width*/, int /*buffer_height*/, int32_t /*x*/, int32_t /*y*/, const char* /*string*/, uint8_t* /*font_ptr*/, uint32_t /*color*/, uint32_t* /*bg_color*/); //modified version from Raspidmx, return end position of printed string

static void buffer_fill(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, uint32_t /*rgba_color*/); //fill buffer with given color
static void buffer_rectangle_fill(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, int32_t /*x*/, int32_t /*y*/, int32_t /*w*/, int32_t /*h*/, uint32_t /*rgba_color*/); //fill rectangle with given color
static void buffer_horizontal_line(void* /*buffer*/, uint32_t /*width*/, uint32_t /*height*/, int32_t /*x1*/, int32_t /*x2*/, int32_t /*y*/, uint32_t /*rgba_color*/); //draw horizontal line

static DISPMANX_RESOURCE_HANDLE_T dispmanx_resource_create_from_png(char* /*filename*/, VC_RECT_T* /*image_rect_ptr*/); //create dispmanx ressource from png file, return 0 on failure, ressource handle on success

static bool lowbat_gpio_init(void); //init low battery gpio things
static bool lowbat_gpio_check(void); //check if low battery gpio is high, false if not (incl. disabled)
static bool lowbat_sysfs(void); //read sysfs power_supply battery capacity, return true if threshold, false if under or file not found

void osd_build_element(DISPMANX_RESOURCE_HANDLE_T /*resource*/, DISPMANX_ELEMENT_HANDLE_T */*element*/, DISPMANX_UPDATE_HANDLE_T /*update*/, uint32_t /*osd_width*/, uint32_t /*osd_height*/, uint32_t /*x*/, uint32_t /*y*/, uint32_t /*width*/, uint32_t /*height*/);

int int_constrain(int* /*val*/, int /*min*/, int /*max*/); //limit int value to given (incl) min and max value, return 0 if val within min and max, -1 under min, 1 over max
static bool html_to_uint32_color(char* /*html_color*/, uint32_t* /*rgba*/); //convert html color (3/4 or 6/8 hex) to uint32_t (alpha, blue, green, red)

static void tty_signal_handler(int /*sig*/); //handle signal func
static void program_close(void); //regroup all close functs
static void program_get_path(char** /*args*/, char* /*path*/, char* /*program*/); //get current program path based on program argv or getcwd if failed
static void program_usage(void); //display help


//generic
const char program_version[] = "0.1a"; //program version
const char dev_webpage[] = "https://github.com/TheFlav/Freeplay_OSD"; //dev website
bool debug = true, kill_requested = false, already_killed = false;
double program_start_time = .0;
char program_path[PATH_MAX] = {'\0'}, program_name[PATH_MAX] = {'\0'}; //full path to this program
char pid_path[PATH_MAX] = {'\0'}; //full path to program pid file

//osd
int display_number = 0; //dispmanx display num
int osd_layer = 10000; //dispmanx layer
int osd_check_rate = 30; //osd check rate in hz
//int osd_signal = SIGUSR1; //trigger signal
int osd_timeout = 5; //timeout in sec
int osd_max_lines = 15; //max number of lines to display on screen without spacing
int osd_text_padding = 5; //text distance to screen border
double osd_start_time = -1.; //osd start time
void *osd_buffer_ptr = NULL; //bitmap buffer pointer
char osd_color_bg_str[9] = "00000050"; uint32_t osd_color_bg = 0, osd_color_text_bg = 0; //background raw color (rgba)
char osd_color_text_str[9] = "FFFFFF"; uint32_t osd_color_text = 0, osd_color_separator = 0; //text raw color (rgba)
char osd_color_warn_str[9] = "FF7F27"; uint32_t osd_color_warn = 0; //warning text raw color (rgba)
char osd_color_crit_str[9] = "EB3324"; uint32_t osd_color_crit = 0; //critical text raw color (rgba)

//header osd
int osd_header_height_percent = 6; //height percent relative to screen height
double osd_header_start_time = -1.; //osd start time
char osd_header_pos_str[2] = "t"; //raw osd position, t,b. Real position computed at runtime
void *osd_header_buffer_ptr = NULL; //bitmap buffer pointer

//osd: general
char rtc_path[PATH_MAX] = "/sys/class/rtc/rtc0/"; //absolute path to rtc class
char cpu_thermal_path[PATH_MAX] = "/sys/class/thermal/thermal_zone0/temp"; //absolute path to cpu temperature file
char backlight_path[PATH_MAX] = "/dev/shm/uhid_i2c_driver/0/backlight"; //absolute path to current backlight file
char backlight_max_path[PATH_MAX] = "/dev/shm/uhid_i2c_driver/0/backlight_max"; //absolute path to max backlight file
#define cpu_thermal_divider 1000 //divide temp by given value to get celsus
#define memory_divider 1024 //divide memory by given value to get mB

//osd: low battery
bool lowbat_gpio_enabled = false; //use gpio pin, leave as is, defined during runtime
#if defined(USE_WIRINGPI) || defined(USE_GPIOD) //low bat gpio
    bool lowbat_gpio_reversed = false; //gpio pin active low
    int lowbat_gpio = 10; //gpio pin, -1 to disable
#else
    int lowbat_gpio = -1; //disabled
#endif

DISPMANX_RESOURCE_HANDLE_T lowbat_resource = 0;
char* lowbat_img_file = "res/low_battery.png"; //low battery icon filename
char lowbat_pos_str[3] = "tr"; //raw image position, tl,tr,bl,br. Real position computed at runtime
int lowbat_width_percent = 8; //low battery icon width percent relative to screen width
#define lowbat_padding 10 //icon distance to display borders

int lowbat_limit = 10; //low battery icon display threshold (percent)
double lowbat_blink = .5; //low battery icon blink interval in sec
double lowbat_blink_start_time = -1.; //low battery icon blink start time

char battery_rsoc_path[PATH_MAX] = "/sys/class/power_supply/battery/capacity"; //absolute path to battery rsoc
char battery_volt_path[PATH_MAX] = "/sys/class/power_supply/battery/voltage_now"; //absolute path to battery voltage
uint32_t battery_volt_divider = 1000000; //divide voltage by given value to get volt

//dispmanx
#ifndef ALIGN_TO_16
	#define ALIGN_TO_16(x)((x + 15) & ~15)
#endif
DISPMANX_DISPLAY_HANDLE_T dispmanx_display = 0; //display handle
VC_DISPMANX_ALPHA_T dispmanx_alpha_from_src = {DISPMANX_FLAGS_ALPHA_FROM_SOURCE, 255, 0};
uint32_t vc_image_ptr; //only here because of how dispmanx works, not used





