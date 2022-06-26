/*
Test program for fp_osd.
Partialy based on linux evtest (Vojtech Pavlik) source code.

This program is just kept here as archive.

If you plan for whatever reason to compile this program (sudo may be needed depending on event file rights):
gcc -o evtest evtest.c -lpthread
sudo ./evtest
*/

#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <limits.h>
#include <string.h>

#include <linux/input.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>

//generic from fp_osd.h/c
bool debug = true;
bool kill_requested = false, already_killed = false;
double program_start_time = .0; //used for print output
int osd_check_rate = 30; //osd check rate in hz

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




//#define NO_OSD 1
//#define NO_TINYOSD 1
//#define NO_EVDEV 1

#ifndef NO_EVDEV
    //evdev thread specific
    pthread_t evdev_thread = 0; //event detection thread
    bool evdev_thread_started = false; //event detection thread is running
    double evdev_check_interval = 10.; //recheck interval if event detection failed in seconds
    char evdev_path[PATH_MAX] = "/dev/input/"; //event device path, will search for evdev_name if folder provided
    char evdev_path_used[PATH_MAX] = ""; //event device path used, done that way to allow disconnect and reconnect of controller without failing evdev routine
    char evdev_name_search[255] = "Freeplay Gamepad 0"; //event device name to search

    //input sequence to detect
    double evdev_sequence_detect_interval = 0.2; //max interval between first and last input detected in seconds
    #define evdev_sequence_max 5 //maximum keys to detect at once for a osd version
    #ifndef NO_OSD
        char osd_evdev_sequence_char[1024] = "0x13c,0x136,0x137"; //each key separated by ',' charater, For reference: https://elixir.bootlin.com/linux/latest/source/include/uapi/linux/input-event-codes.h
    #endif
    #ifndef NO_TINYOSD
        char tinyosd_evdev_sequence_char[1024] = "0x13c,0x138,0x139"; //each key separated by ',' charater, For reference: https://elixir.bootlin.com/linux/latest/source/include/uapi/linux/input-event-codes.h
    #endif
#endif










#ifndef NO_EVDEV
int in_array_int(int* arr, int value, int arr_size){ //search in value in int array, return index or -1 on failure
    for (int i=0; i < arr_size; i++) {if (arr[i] == value) {return i;}}
    return -1;
}

void *evdev_routine(){ //evdev input thread routine
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

    while (!kill_requested){ //thread main loop
        double loop_start_time = get_time_double(); //loop start time

        if (evdev_fd == -1 && loop_start_time - recheck_start_time > evdev_check_interval){ //device has failed or not started
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
                        } else {print_stderr("'%s' folder is empty\n", evdev_path);}
                    } else if (S_ISREG(evdev_stat.st_mode)){ //given path is a file
                        evdev_fd = open(evdev_path, O_RDONLY);
                        if (evdev_fd < 0){print_stderr("failed to open '%s'\n", evdev_path); //failed to open file
                        } else {
                            ioctl(evdev_fd, EVIOCGNAME(sizeof(evdev_name)), evdev_name); close(evdev_fd); //get device name
                            if (evdev_name[0] != '\0'){
                                strncpy(evdev_path_used, evdev_path, sizeof(evdev_path_used));
                                strncpy(evdev_name_search, evdev_name, sizeof(evdev_name_search));
                            } else {print_stderr("failed to detect device name for '%s'\n", evdev_path); evdev_retry = false;}
                        }
                    } else {print_stderr("invalid file type for '%s'\n", evdev_path); evdev_retry = false;}
                } else {print_stderr("failed to open '%s'\n", evdev_path);}
            }
            evdev_fd = -1;

            if (evdev_path_used[0] != '\0'){ //"valid" device found
                evdev_fd = open(evdev_path_used, O_RDONLY);
                if (evdev_fd < 0){print_stderr("failed to open '%s'\n", evdev_path_used); //failed to open file
                } else {
                    evdev_name[0] = '\0'; ioctl(evdev_fd, EVIOCGNAME(sizeof(evdev_name)), evdev_name); //get device name
                    if (evdev_name[0] == '\0'){print_stderr("failed to get device name for '%s'\n", evdev_path_used); close(evdev_fd); evdev_fd = -1;
                    } else {
                        fcntl(evdev_fd, F_SETFL, fcntl(evdev_fd, F_GETFL) | O_NONBLOCK); //set fd to non blocking
                        print_stderr("'%s' will be used for '%s' device\n", evdev_path_used, evdev_name);
                    }
                }
            }

            if (evdev_fd == -1){
                if (evdev_name_search[0] != '\0'){print_stderr("can't poll from '%s' device\n", evdev_name_search);}
                if (evdev_retry){print_stderr("retry in %.0lfs\n", evdev_check_interval);} else {print_stderr("evdev routine disabled\n"); goto thread_close;}
            }

            recheck_start_time = loop_start_time;
        }

        if (evdev_fd != -1){
            int events_read = read(evdev_fd, &events, events_size);
            if (errno == ENODEV || errno == ENOENT || errno == EBADF){
                print_stderr("failed to read from device '%s' (%s), try to reopen in %.0lfs\n", evdev_name_search, evdev_path_used, evdev_check_interval);
                close(evdev_fd); evdev_fd = -1; continue;
            } else if (events_read >= input_event_size){
                if (evdev_detected_start > 0. && loop_start_time - evdev_detected_start > evdev_sequence_detect_interval){ //reset sequence
                    memset(evdev_detected_sequence, 0, sizeof(evdev_detected_sequence));
                    evdev_detected_sequence_index = 0; evdev_detected_start = -1.;
                    print_stderr("sequence timer reset\n");
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
                        if (evdev_detected_start < 0){evdev_detected_start = loop_start_time; print_stderr("sequence timer start\n");} //check not started
                        if (loop_start_time - evdev_detected_start < evdev_sequence_detect_interval && in_array_int(evdev_detected_sequence, tmp_code, evdev_sequence_max * 2) == -1){ //still in detection interval and not in detected sequence
                            evdev_detected_sequence[evdev_detected_sequence_index++] = tmp_code;
                            print_stderr("%d added to detected sequence\n", tmp_code);
                        }
                    }
                }

                #if !(defined(NO_OSD) && defined(NO_TINYOSD))
                int tmp_detected_count = 0;
                #endif
                #ifndef NO_OSD
                for (int i = 0; i < osd_evdev_sequence_limit; i++){if (osd_evdev_sequence[i] != 0 && in_array_int(evdev_detected_sequence, osd_evdev_sequence[i], evdev_sequence_max * 2) != -1){tmp_detected_count++;}} //check osd trigger
                if (tmp_detected_count == osd_evdev_sequence_limit){
                    print_stderr("osd triggered\n");
                    evdev_detected_start = 1.;
                } else {
                    tmp_detected_count = 0;
                #endif
                #ifndef NO_TINYOSD
                    for (int i = 0; i < tinyosd_evdev_sequence_limit; i++){if (tinyosd_evdev_sequence[i] != 0 && in_array_int(evdev_detected_sequence, tinyosd_evdev_sequence[i], evdev_sequence_max * 2) != -1){tmp_detected_count++;}} //check tiny osd trigger
                    if (tmp_detected_count == tinyosd_evdev_sequence_limit){
                        print_stderr("tiny osd triggered\n");
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

static void tty_signal_handler(int sig){ //handle signal func
    if (debug){print_stderr("DEBUG: signal received: %d.\n", sig);}
    if (sig == SIGUSR1){
    } else if (sig == SIGUSR2){
    } else {kill_requested = true;}
}

static void program_close(void){ //regroup all close functs
    if (already_killed){return;}
    already_killed = true;
}

int main(int argc, char *argv[]){
    program_start_time = get_time_double(); //program start time, used for detailed outputs

    //tty signal handling
    signal(SIGINT, tty_signal_handler); //ctrl-c
    signal(SIGTERM, tty_signal_handler); //SIGTERM from htop or other, SIGKILL not work as program get killed before able to handle
    signal(SIGABRT, tty_signal_handler); //failure
    signal(SIGUSR1, tty_signal_handler); //use signal SIGUSR1 as trigger to display full OSD
    signal(SIGUSR2, tty_signal_handler); //use signal SIGUSR2 as trigger to display header OSD
    atexit(program_close); at_quick_exit(program_close); //run on program exit

    #ifndef NO_EVDEV
        evdev_thread_started = pthread_create(&evdev_thread, NULL, evdev_routine, NULL) == 0; //create evdev routine thread
    #endif

    while (!kill_requested){ //main loop
        //double loop_start_time = get_time_double(); //loop start time
        usleep(1000000);
    }
}
