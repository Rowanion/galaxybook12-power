/*
 * Galaxy Book 12 AMOLED brightness service.
 *
 * Panel programming is derived from Aurélien Croc's SamsungGalaxyBook12
 * project, Copyright (C) 2022-2023 Aurélien Croc (AP2C).
 * Safety checks, device discovery and the service loop are Copyright (C) 2026
 * Samsung-Galaxy-Book-12-Linux contributors.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "brightness.h"
#include "dpAux.h"

#define DMI_VENDOR_PATH "/sys/class/dmi/id/sys_vendor"
#define DMI_PRODUCT_PATH "/sys/class/dmi/id/product_name"
#define BACKLIGHT_PATH "/sys/class/backlight/intel_backlight"
#define AUX_CLASS_GLOB "/sys/class/drm_dp_aux_dev/drm_dp_aux*"
#define PANEL_ABSOLUTE_MIN 1
#define PANEL_DEFAULT_MIN 10
#define PANEL_REFERENCE_SAFE_MIN 40
#define PANEL_MAX 101
#define FADE_STEP 1

static volatile sig_atomic_t running = 1;

static void stop_handler(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static int read_text(const char *path, char *buffer, size_t size)
{
    FILE *file = fopen(path, "r");
    size_t length;

    if (!file)
        return -1;
    if (!fgets(buffer, size, file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    length = strlen(buffer);
    while (length && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
        buffer[--length] = '\0';
    return 0;
}

static int read_number(const char *path, long *value)
{
    char buffer[64];
    char *end;

    if (read_text(path, buffer, sizeof(buffer)) < 0)
        return -1;
    errno = 0;
    *value = strtol(buffer, &end, 10);
    return errno || *end ? -1 : 0;
}

static bool supported_dmi(void)
{
    char vendor[128];
    char product[128];

    return read_text(DMI_VENDOR_PATH, vendor, sizeof(vendor)) == 0 &&
           read_text(DMI_PRODUCT_PATH, product, sizeof(product)) == 0 &&
           strstr(vendor, "SAMSUNG ELECTRONICS") != NULL &&
           (strcmp(product, "Galaxy Book 12") == 0 || strcmp(product, "Galaxy Book 12 LTE") == 0);
}

static bool supported_edid(const char *connector)
{
    char path[PATH_MAX];
    unsigned char edid[12];
    FILE *file;

    if (snprintf(path, sizeof(path), "%s/edid", connector) >= (int)sizeof(path))
        return false;
    file = fopen(path, "rb");
    if (!file)
        return false;
    bool valid = fread(edid, 1, sizeof(edid), file) == sizeof(edid);
    fclose(file);

    /* EDID manufacturer SDC, product 0xa029 (little endian in EDID). */
    return valid && edid[8] == 0x4c && edid[9] == 0x83 &&
           edid[10] == 0x29 && edid[11] == 0xa0;
}

static int find_panel(char *aux_device, size_t aux_size,
                      char *connector, size_t connector_size)
{
    glob_t matches = {0};
    int found = -1;

    if (glob(AUX_CLASS_GLOB, 0, NULL, &matches) != 0)
        return -1;

    for (size_t i = 0; i < matches.gl_pathc; i++) {
        char device_link[PATH_MAX];
        char resolved[PATH_MAX];
        char status_path[PATH_MAX];
        char status[32];
        const char *aux_name = strrchr(matches.gl_pathv[i], '/');
        const char *connector_name;

        if (!aux_name)
            continue;
        aux_name++;
        if (snprintf(device_link, sizeof(device_link), "%s/device",
                     matches.gl_pathv[i]) >= (int)sizeof(device_link) ||
            !realpath(device_link, resolved))
            continue;
        connector_name = strrchr(resolved, '/');
        if (!connector_name || !strstr(connector_name + 1, "-eDP-"))
            continue;
        if (snprintf(status_path, sizeof(status_path), "%s/status", resolved) >=
            (int)sizeof(status_path) ||
            read_text(status_path, status, sizeof(status)) < 0 ||
            strcmp(status, "connected") != 0 || !supported_edid(resolved))
            continue;
        if (snprintf(aux_device, aux_size, "/dev/%s", aux_name) >=
                (int)aux_size ||
            snprintf(connector, connector_size, "%s", resolved) >=
                (int)connector_size)
            continue;
        found = 0;
        break;
    }
    globfree(&matches);
    return found;
}

static int inspect_hardware(char *aux, size_t aux_size,
                            char *connector, size_t connector_size)
{
    char brightness_path[PATH_MAX];
    long unused;

    if (!supported_dmi()) {
        fprintf(stderr, "Unsupported system: expected Samsung Galaxy Book 12.\n");
        return -1;
    }
    if (find_panel(aux, aux_size, connector, connector_size) < 0) {
        fprintf(stderr, "Supported SDC a029 AMOLED panel not found on eDP.\n");
        return -1;
    }
    snprintf(brightness_path, sizeof(brightness_path), "%s/max_brightness",
             BACKLIGHT_PATH);
    if (read_number(brightness_path, &unused) < 0 || unused <= 0) {
        fprintf(stderr, "Intel backlight interface not found.\n");
        return -1;
    }
    return 0;
}

static int open_panel(const char *aux_path, aux_t *aux,
                      displayBrightness_t **display)
{
    *aux = dpAuxOpen(aux_path);
    if (*aux < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", aux_path,
                strerror(dpAuxLastError()));
        return -1;
    }
    *display = initBrightness(*aux, 1);
    if (!*display) {
        fprintf(stderr, "Panel initialization failed: %s\n",
                dpAuxLastError() ? strerror(dpAuxLastError()) :
                "private AUX registers unavailable");
        dpAuxClose(*aux);
        *aux = -1;
        return -1;
    }
    return 0;
}

static int apply_level(aux_t aux, displayBrightness_t *display,
                       int level, int profile, bool set_profile)
{
    dpAuxClearError();
    if (set_profile)
        setColorProfile(aux, display, (char)profile);
    setBrightness(aux, display, (char)level);
    if (dpAuxLastError()) {
        fprintf(stderr, "AUX transfer failed: %s\n", strerror(dpAuxLastError()));
        return -1;
    }
    return 0;
}

static int mapped_level(long brightness, long maximum, int minimum)
{
    if (brightness < 0)
        brightness = 0;
    if (brightness > maximum)
        brightness = maximum;
    return minimum + (int)((brightness * (PANEL_MAX - minimum) +
                            maximum / 2) / maximum);
}

static int watch_brightness(const char *aux_path, int profile, int minimum)
{
    char brightness_path[PATH_MAX];
    char maximum_path[PATH_MAX];
    aux_t aux = -1;
    displayBrightness_t *display = NULL;
    long maximum;
    int previous = -1;
    bool set_profile = true;
    struct timespec idle_pause = {.tv_sec = 0, .tv_nsec = 100000000};
    struct timespec fade_pause = {.tv_sec = 0, .tv_nsec = 5000000};
    struct timespec previous_time;

    snprintf(brightness_path, sizeof(brightness_path), "%s/brightness",
             BACKLIGHT_PATH);
    snprintf(maximum_path, sizeof(maximum_path), "%s/max_brightness",
             BACKLIGHT_PATH);
    if (read_number(maximum_path, &maximum) < 0 || maximum <= 0)
        return 1;

    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    clock_gettime(CLOCK_BOOTTIME, &previous_time);
    while (running) {
        long brightness;
        int level;
        int target;
        struct timespec current_time;

        clock_gettime(CLOCK_BOOTTIME, &current_time);
        if (current_time.tv_sec - previous_time.tv_sec > 2 && display) {
            /* CLOCK_BOOTTIME includes suspend: reopen AUX and recalibrate. */
            free(display);
            display = NULL;
            dpAuxClose(aux);
            aux = -1;
            previous = -1;
            set_profile = true;
        }
        previous_time = current_time;

        if (!display && open_panel(aux_path, &aux, &display) < 0) {
            sleep(1);
            continue;
        }
        if (read_number(brightness_path, &brightness) < 0) {
            fprintf(stderr, "Cannot read %s.\n", brightness_path);
            break;
        }
        target = mapped_level(brightness, maximum, minimum);
        level = target;
        if (previous >= 0 && target > previous + FADE_STEP)
            level = previous + FADE_STEP;
        else if (previous >= 0 && target < previous - FADE_STEP)
            level = previous - FADE_STEP;
        if (level != previous || set_profile) {
            if (apply_level(aux, display, level, profile, set_profile) < 0) {
                free(display);
                display = NULL;
                dpAuxClose(aux);
                aux = -1;
                previous = -1;
                set_profile = true;
                sleep(1);
                continue;
            }
            previous = level;
            set_profile = false;
        }
        nanosleep(level == target ? &idle_pause : &fade_pause, NULL);
    }
    free(display);
    dpAuxClose(aux);
    return running ? 1 : 0;
}

static int parse_range(const char *text, int minimum, int maximum, int *value)
{
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || *text == '\0' || *end || parsed < minimum || parsed > maximum)
        return -1;
    *value = (int)parsed;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s check\n"
        "  %s set <1..101> [profile 0..6]\n"
        "  %s watch [profile 0..6] [minimum 1..40]\n",
        program, program, program);
}

int main(int argc, char **argv)
{
    char aux_path[PATH_MAX];
    char connector[PATH_MAX];
    int profile = 3;
    int minimum = PANEL_DEFAULT_MIN;

    if (argc < 2 || argc > 4) {
        usage(argv[0]);
        return 2;
    }
    if (inspect_hardware(aux_path, sizeof(aux_path), connector,
                         sizeof(connector)) < 0)
        return 1;

    if (!strcmp(argv[1], "check")) {
        printf("System: Samsung Galaxy Book 12\n");
        printf("Panel: SDC a029 AMOLED\n");
        printf("Connector: %s\n", connector);
        printf("AUX device: %s\n", aux_path);
        printf("Default panel range: %d..%d\n", PANEL_DEFAULT_MIN, PANEL_MAX);
        printf("Reference flicker warning: below %d\n",
               PANEL_REFERENCE_SAFE_MIN);
        return 0;
    }
    if (!strcmp(argv[1], "watch")) {
        if (argc == 3 && parse_range(argv[2], 0, 6, &profile) < 0) {
            usage(argv[0]);
            return 2;
        }
        if (argc == 4 &&
            (parse_range(argv[2], 0, 6, &profile) < 0 ||
             parse_range(argv[3], PANEL_ABSOLUTE_MIN,
                         PANEL_REFERENCE_SAFE_MIN,
                         &minimum) < 0)) {
            usage(argv[0]);
            return 2;
        }
        return watch_brightness(aux_path, profile, minimum);
    }
    if (!strcmp(argv[1], "set")) {
        int level;
        aux_t aux;
        displayBrightness_t *display;
        int result;

        if ((argc != 3 && argc != 4) ||
            parse_range(argv[2], PANEL_ABSOLUTE_MIN, PANEL_MAX, &level) < 0 ||
            (argc == 4 && parse_range(argv[3], 0, 6, &profile) < 0)) {
            usage(argv[0]);
            return 2;
        }
        if (geteuid() != 0) {
            fprintf(stderr, "The set command must run as root.\n");
            return 1;
        }
        if (level < PANEL_REFERENCE_SAFE_MIN)
            fprintf(stderr,
                    "Warning: levels below %d may flicker on this panel.\n",
                    PANEL_REFERENCE_SAFE_MIN);
        if (open_panel(aux_path, &aux, &display) < 0)
            return 1;
        result = apply_level(aux, display, level, profile, true);
        free(display);
        dpAuxClose(aux);
        return result < 0;
    }

    usage(argv[0]);
    return 2;
}
