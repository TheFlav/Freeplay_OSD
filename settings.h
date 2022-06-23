/*
FreeplayTech On-screen (heads-up) display overlay

User settings file.
Please refer to fp_osd.h and fp_osd.c for more informations.
*/

//dispmanx
int display_number = 0; //dispmanx display num
int osd_layer = 10000; //dispmanx first layer
int osd_check_rate = 30; //osd check rate in hz
#if !(defined(NO_OSD) && defined(NO_TINYOSD))
    int osd_timeout = 5; //osd displayed timeout in sec
#endif

//signal
#ifndef NO_SIGNAL_FILE
    char signal_path[PATH_MAX] = {'\0'}; //full path to signal file, invalid to disable
#endif

//OSD
#ifndef NO_GPIO
    int osd_gpio = -1; //gpio pin, -1 to disable
    bool osd_gpio_reversed = false; //gpio pin active low
#endif
#if !(defined(NO_OSD) && defined(NO_TINYOSD))
    int osd_max_lines = 16; //max number of lines to display on screen without spacing
    int osd_text_padding = 5; //text distance to screen border
#endif
char osd_color_bg_str[9] = "00000050"; uint32_t osd_color_bg = 0, osd_color_text_bg = 0; //background raw color (rgba)
char osd_color_text_str[9] = "FFFFFF"; uint32_t osd_color_text = 0, osd_color_separator = 0; //text raw color (rgba)
char osd_color_warn_str[9] = "ffa038"/*"FF7F27"*/; uint32_t osd_color_warn = 0; //warning text raw color (rgba)
char osd_color_crit_str[9] = "ff5548"/*"EB3324"*/; uint32_t osd_color_crit = 0; //critical text raw color (rgba)

//tiny OSD
#ifndef NO_GPIO
    int osd_header_gpio = -1; //gpio pin, -1 to disable
    bool osd_header_gpio_reversed = false; //gpio pin active low
#endif
#ifndef NO_TINYOSD
    int osd_header_height_percent = 5; //height percent relative to screen height
    char osd_header_pos_str[2] = "t"; //raw osd position, t,b. Real position computed at runtime
#endif

//warning icons
#if !(defined(NO_BATTERY_ICON) && defined(NO_CPU_ICON))
    int warn_icons_height_percent = 9; //icons percent height relative to screen height
    #define icons_padding 10 //icons distance to display borders
    char warn_icons_pos_str[3] = "tr"; //icons position, tl,tr,bl,br. Real position computed at runtime
    #ifndef NO_BATTERY_ICON
        char* lowbat_img_file = "res/low_battery.png"; //low battery icon filename
    #endif
    #ifndef NO_CPU_ICON
        char* cputemp_img_file = "res/temp_warn.png"; //cpu temp icon filename
    #endif
#endif

//cpu data
char cpu_thermal_path[PATH_MAX] = "/sys/class/thermal/thermal_zone0/temp"; //absolute path to cpu temperature file
uint32_t cpu_thermal_divider = 1000; //divide temp by given value to get celsus
int cputemp_warn = 70, cputemp_crit = 80; //cpu temp icon display threshold

//memory data
#define memory_divider 1024 //divide memory by given value to get mB

//battery data
#ifndef NO_GPIO
int lowbat_gpio = 10; //gpio pin, -1 to disable
bool lowbat_gpio_reversed = true; //gpio pin active low
#endif
char battery_rsoc_path[PATH_MAX] = "/sys/class/power_supply/battery/capacity"; //absolute path to battery rsoc
char battery_volt_path[PATH_MAX] = "/sys/class/power_supply/battery/voltage_now"; //absolute path to battery voltage
uint32_t battery_volt_divider = 1000000; //divide voltage by given value to get volt
int lowbat_limit = 10; //low battery icon display threshold (percent)

//other paths
char rtc_path[PATH_MAX] = "/sys/class/rtc/rtc0/"; //absolute path to rtc class
char backlight_path[PATH_MAX] = "/dev/shm/uhid_i2c_driver/0/backlight"; //absolute path to current backlight file
char backlight_max_path[PATH_MAX] = "/dev/shm/uhid_i2c_driver/0/backlight_max"; //absolute path to max backlight file

//debug
#ifdef CHARSET_EXPORT
    char* charset_raspidmx_path = "res/charset_raspidmx.png"; //relative path to exported Raspidmx character set
    char* charset_icons_path = "res/charset_icons.png"; //relative path to exported icons character set
#endif

