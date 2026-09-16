/*
 * Userspace LED control for BY Tech / Sino Wealth HID keyboards
 * that enumerate as VID 0x258A PID 0x0049 ("Gaming Keyboard").
 *
 * Protocol is HID feature report 0x06, 1032 bytes. Packets match the
 * Redragon Anubis (K539-RGB) capture used by the official Windows
 * software: a static-color map followed by a mode/brightness packet.
 *
 * This is not a kernel driver. Do not send report 0x05 ISP commands.
 */

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <hidapi.h>

#define VID 0x258A
#define PID 0x0049
#define USAGE_PAGE_VENDOR 0xFF00
#define REPORT_LEN 1032
#define BRIGHTNESS_OFF 0x30
#define MODE_BYTE 0x15
#define STATIC_COLOR_MODE_BYTE 0x26
#define STATIC_BRIGHTNESS_BYTE 0x27

enum {
    MODE_OFF = 0x00,
    MODE_STATIC = 0x01,
    MODE_RESPIRE = 0x02,
    MODE_RAINBOW = 0x03,
    MODE_FLASH_AWAY = 0x04,
    MODE_RAINDROPS = 0x05,
    MODE_RAINBOW_WHEEL = 0x06,
    MODE_RIPPLES = 0x07,
    MODE_STARS = 0x08,
    MODE_SHADOW = 0x09,
    MODE_SNAKE = 0x0A,
    MODE_NEON = 0x0B,
    MODE_REACTION = 0x0C,
    MODE_SINE_WAVE = 0x0D,
    MODE_SCAN = 0x0E,
    MODE_WINDMILL = 0x0F,
    MODE_WATERFALL = 0x10,
    MODE_BLOSSOM = 0x11,
    MODE_STORM = 0x12,
    MODE_COLLISION = 0x13
};

static const char k_static_prefix[] =
    "06 08 b8 00 40 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00";

static const char k_static_mask[] =
    "00 00 ff 00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 "
    "ff 00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 "
    "ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff 00 "
    "ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff 00 ff ff "
    "00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff 00 ff ff 00 ff "
    "00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff 00 ff ff 00 ff 00 ff "
    "00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff 00 ff ff 00 ff 00 ff 00 ff "
    "ff ff ff ff 00 ff 00 00 00 ff 00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff "
    "ff ff ff 00 00 00 00 ff 00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff "
    "ff 00 00 00 00 ff 00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 "
    "00 00 00 ff 00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 "
    "00 ff 00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff "
    "00 ff 00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff "
    "00 ff ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff 00 ff "
    "ff 00 ff 00 ff 00 ff ff ff ff ff ff 00 00 00 00 ff 00 ff 00 ff ff 00 "
    "ff 00 ff 00 ff ff ff ff ff";

/*
 * OpenRGB mode/brightness template (report 0x06, command 0x03 0xB6).
 * Byte 0x15 selects the effect. Pairs from 0x26 are (color_mode, speed+bright)
 * for each effect; 0x07 in color_mode means random/rainbow colors.
 */
static const unsigned char k_mode_template[] = {
    0x06, 0x03, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x5A, 0xA5, 0x03, 0x03, 0x00, 0x00, 0x00, 0x02, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x55, 0x55, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x20, 0x00, 0x44, 0x07, 0x30,
    0x07, 0x23, 0x00, 0x23, 0x00, 0x23, 0x07, 0x33, 0x07, 0x23, 0x07, 0x23, 0x07, 0x23, 0x07, 0x23,
    0x07, 0x23, 0x07, 0x23, 0x07, 0x23, 0x07, 0x23, 0x07, 0x23, 0x07, 0x23, 0x07, 0x23, 0x07, 0x23,
    0x07, 0x23, 0x00, 0x10, 0x00, 0x10, 0x07, 0x44, 0x07, 0x44, 0x07, 0x44, 0x07, 0x44, 0x07, 0x44,
    0x07, 0x44, 0x07, 0x44, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5, 0x03, 0x03
};

struct effect_name {
    const char *name;
    unsigned char mode;
};

static const struct effect_name k_effects[] = {
    {"off", MODE_OFF},
    {"static", MODE_STATIC},
    {"breathe", MODE_RESPIRE},
    {"rainbow", MODE_RAINBOW},
    {"flash", MODE_FLASH_AWAY},
    {"raindrops", MODE_RAINDROPS},
    {"wheel", MODE_RAINBOW_WHEEL},
    {"ripples", MODE_RIPPLES},
    {"stars", MODE_STARS},
    {"shadow", MODE_SHADOW},
    {"snake", MODE_SNAKE},
    {"neon", MODE_NEON},
    {"reaction", MODE_REACTION},
    {"wave", MODE_SINE_WAVE},
    {"scan", MODE_SCAN},
    {"windmill", MODE_WINDMILL},
    {"waterfall", MODE_WATERFALL},
    {"blossom", MODE_BLOSSOM},
    {"storm", MODE_STORM},
    {"collision", MODE_COLLISION},
    {NULL, 0}
};

/* One resolved lighting request, so `idle` can replay the same CLI action. */
struct action {
    int want_effect;
    unsigned char mode;
    unsigned char rgb[3];
    unsigned char brightness;
    int random_colors;
};

/* Silences the per-send chatter while the idle loop is running. */
static int g_quiet;

static volatile sig_atomic_t g_stop;

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s list\n"
            "  %s dump [5|6]\n"
            "  %s static <rrggbb|#rrggbb|red|green|blue|white|off> [--brightness 0-4]\n"
            "  %s effect <name> [--brightness 0-4] [--random]\n"
            "  %s off\n"
            "  %s idle [--timeout SECS] [--poll SECS] <static ...|effect ...|off>\n"
            "\n"
            "Effects: off static breathe rainbow flash raindrops wheel ripples\n"
            "         stars shadow snake neon reaction wave scan windmill\n"
            "         waterfall blossom storm collision\n"
            "\n"
            "idle blanks the leds after SECS with no HID input (read from the\n"
            "macOS IOHIDSystem HIDIdleTime property) and restores the given\n"
            "lighting on the next input. Defaults: --timeout 300 --poll 1.\n"
            "Runs in the foreground; ctrl-c restores the lighting and exits.\n"
            "\n"
            "Controls BY Tech Gaming Keyboard (258A:0049) RGB via HID feature reports.\n",
            argv0, argv0, argv0, argv0, argv0, argv0);
}

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int parse_hex_bytes(const char *s, unsigned char *out, int cap)
{
    int n = 0;
    while (*s) {
        if (isspace((unsigned char)*s)) {
            s++;
            continue;
        }
        int hi = hex_nibble(*s++);
        if (hi < 0 || !*s) {
            return -1;
        }
        int lo = hex_nibble(*s++);
        if (lo < 0) {
            return -1;
        }
        if (n >= cap) {
            return -1;
        }
        out[n++] = (unsigned char)((hi << 4) | lo);
    }
    return n;
}

static void pad_report(unsigned char *buf, int used)
{
    if (used < 0) {
        return;
    }
    memset(buf + used, 0, (size_t)(REPORT_LEN - used));
}

static void print_hex(const unsigned char *buf, int n)
{
    for (int i = 0; i < n; i++) {
        printf("%02x%s", buf[i], ((i + 1) % 32) == 0 ? "\n" : " ");
    }
    if (n % 32) {
        printf("\n");
    }
}

static const char *narrow(const wchar_t *ws, char *tmp, size_t tmp_len)
{
    if (!ws) {
        return "";
    }
    wcstombs(tmp, ws, tmp_len - 1);
    tmp[tmp_len - 1] = '\0';
    return tmp;
}

static hid_device *open_led_interface(void)
{
    struct hid_device_info *list = hid_enumerate(VID, PID);
    struct hid_device_info *it;
    hid_device *dev = NULL;
    const char *path = NULL;

    if (!list) {
        fprintf(stderr, "no HID device 258A:0049 found\n");
        return NULL;
    }

    for (it = list; it; it = it->next) {
        if (!path && it->interface_number == 1 && it->usage_page == USAGE_PAGE_VENDOR) {
            path = it->path;
        }
    }

    if (!path) {
        for (it = list; it; it = it->next) {
            if (it->interface_number == 1) {
                path = it->path;
                break;
            }
        }
    }

    if (!path) {
        fprintf(stderr, "no vendor HID interface (iface 1 / usage page FF00)\n");
        hid_free_enumeration(list);
        return NULL;
    }

    dev = hid_open_path(path);
    if (!dev) {
        const wchar_t *err = hid_error(NULL);
        fprintf(stderr, "hid_open_path(%s) failed", path);
        if (err) {
            fprintf(stderr, ": %ls", err);
        }
        fprintf(stderr,
                "\nIf macOS blocked this, grant Input Monitoring / USB access to the terminal in System Settings > Privacy.\n");
    }
    hid_free_enumeration(list);
    return dev;
}

static int parse_color(const char *s, unsigned char rgb[3])
{
    struct {
        const char *name;
        unsigned char r, g, b;
    } named[] = {
        {"red", 255, 0, 0},
        {"green", 0, 255, 0},
        {"blue", 0, 0, 255},
        {"white", 255, 255, 255},
        {"cyan", 0, 255, 255},
        {"magenta", 255, 0, 255},
        {"yellow", 255, 255, 0},
        {"orange", 255, 128, 0},
        {"purple", 128, 0, 255},
        {"off", 0, 0, 0},
        {NULL, 0, 0, 0},
    };
    const char *hex = s;

    for (int i = 0; named[i].name; i++) {
        if (strcasecmp(s, named[i].name) == 0) {
            rgb[0] = named[i].r;
            rgb[1] = named[i].g;
            rgb[2] = named[i].b;
            return 0;
        }
    }

    if (hex[0] == '#') {
        hex++;
    }
    if (strlen(hex) != 6) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        rgb[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

static int build_static_packet(unsigned char *buf, unsigned char rgb[3])
{
    int n = parse_hex_bytes(k_static_prefix, buf, REPORT_LEN);
    if (n < 0) {
        return -1;
    }
    if (n + 3 > REPORT_LEN) {
        return -1;
    }
    buf[n++] = rgb[0];
    buf[n++] = rgb[1];
    buf[n++] = rgb[2];
    int m = parse_hex_bytes(k_static_mask, buf + n, REPORT_LEN - n);
    if (m < 0) {
        return -1;
    }
    n += m;
    pad_report(buf, n);
    return 0;
}

static int lookup_effect(const char *name, unsigned char *mode)
{
    for (int i = 0; k_effects[i].name; i++) {
        if (strcasecmp(name, k_effects[i].name) == 0) {
            *mode = k_effects[i].mode;
            return 0;
        }
    }
    return -1;
}

static int build_mode_packet(unsigned char *buf, unsigned char mode, unsigned char brightness, int random_colors)
{
    memset(buf, 0, REPORT_LEN);
    memcpy(buf, k_mode_template, sizeof k_mode_template);
    buf[MODE_BYTE] = mode;

    /* Each effect slot is (color_mode, speed+bright). 0x07 = rainbow/random. */
    for (int off = STATIC_COLOR_MODE_BYTE; off + 1 < (int)sizeof k_mode_template; off += 2) {
        if (buf[off] == 0x07 || buf[off] == 0x00) {
            buf[off] = random_colors ? 0x07 : 0x00;
        }
    }

    buf[STATIC_COLOR_MODE_BYTE] = random_colors ? 0x07 : 0x00;
    if (mode == MODE_STATIC || mode == MODE_OFF) {
        buf[STATIC_BRIGHTNESS_BYTE] = brightness;
    } else {
        int idx = 0x29 + ((int)mode - 2) * 2;
        if (idx >= 1 && idx < REPORT_LEN) {
            buf[idx - 1] = random_colors ? 0x07 : 0x00;
            buf[idx] = (unsigned char)(0x22 + (brightness - BRIGHTNESS_OFF));
        }
        buf[STATIC_BRIGHTNESS_BYTE] = brightness;
    }
    return 0;
}

static int send_report(hid_device *dev, const unsigned char *buf)
{
    if (hid_send_feature_report(dev, buf, REPORT_LEN) < 0) {
        fprintf(stderr, "hid_send_feature_report failed: %ls\n", hid_error(dev));
        return -1;
    }
    return 0;
}

static int send_static(hid_device *dev, unsigned char rgb[3], unsigned char brightness, int dry_run)
{
    unsigned char color[REPORT_LEN];
    unsigned char mode[REPORT_LEN];

    if (build_static_packet(color, rgb) < 0 ||
        build_mode_packet(mode, MODE_STATIC, brightness, 0) < 0) {
        fprintf(stderr, "failed to build packets\n");
        return 1;
    }

    if (!g_quiet) {
        printf("static rgb=%02x%02x%02x brightness=0x%02x (mode=0x01, no random)\n",
               rgb[0], rgb[1], rgb[2], brightness);
    }
    if (dry_run) {
        printf("mode packet:\n");
        print_hex(mode, 96);
        printf("color packet:\n");
        print_hex(color, 96);
        return 0;
    }

    /* Leave the wave/rainbow table first, then paint a solid color. */
    if (send_report(dev, mode) < 0) {
        return 1;
    }
    usleep(20000);
    if (send_report(dev, color) < 0) {
        return 1;
    }
    if (!g_quiet) {
        printf("sent static mode + color reports\n");
    }
    return 0;
}

static int send_effect(hid_device *dev, unsigned char mode, unsigned char brightness, int random_colors, int dry_run)
{
    unsigned char buf[REPORT_LEN];

    if (build_mode_packet(buf, mode, brightness, random_colors) < 0) {
        fprintf(stderr, "failed to build mode packet\n");
        return 1;
    }
    if (!g_quiet) {
        printf("effect mode=0x%02x brightness=0x%02x random=%s\n",
               mode, brightness, random_colors ? "yes" : "no");
    }
    if (dry_run) {
        print_hex(buf, 96);
        return 0;
    }
    if (send_report(dev, buf) < 0) {
        return 1;
    }
    if (!g_quiet) {
        printf("sent effect report\n");
    }
    return 0;
}

/*
 * argv[i] is the verb (off/static/effect); the rest are its options.
 * dry_run may be NULL when the caller does not accept --dry-run.
 */
static int parse_action(int argc, char **argv, int i, struct action *a, int *dry_run)
{
    a->want_effect = 0;
    a->mode = MODE_STATIC;
    a->rgb[0] = 255;
    a->rgb[1] = 0;
    a->rgb[2] = 0;
    a->brightness = 0x34;
    a->random_colors = 0;

    if (i >= argc) {
        return -1;
    }

    if (strcmp(argv[i], "off") == 0) {
        a->rgb[0] = a->rgb[1] = a->rgb[2] = 0;
        a->brightness = BRIGHTNESS_OFF;
        i++;
    } else if (strcmp(argv[i], "static") == 0) {
        if (i + 1 >= argc) {
            return -1;
        }
        if (parse_color(argv[i + 1], a->rgb) != 0) {
            fprintf(stderr, "invalid color '%s'\n", argv[i + 1]);
            return -1;
        }
        i += 2;
    } else if (strcmp(argv[i], "effect") == 0) {
        if (i + 1 >= argc) {
            return -1;
        }
        if (lookup_effect(argv[i + 1], &a->mode) != 0) {
            fprintf(stderr, "unknown effect '%s'\n", argv[i + 1]);
            return -1;
        }
        a->want_effect = 1;
        i += 2;
    } else {
        return -1;
    }

    for (; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0 && dry_run) {
            *dry_run = 1;
        } else if (strcmp(argv[i], "--random") == 0 && a->want_effect) {
            a->random_colors = 1;
        } else if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
            int b = atoi(argv[++i]);
            if (b < 0 || b > 4) {
                fprintf(stderr, "brightness must be 0-4\n");
                return -1;
            }
            a->brightness = (unsigned char)(0x30 + b);
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return -1;
        }
    }

    if (a->want_effect) {
        if (a->mode == MODE_RAINBOW || a->mode == MODE_RAINBOW_WHEEL ||
            a->mode == MODE_SINE_WAVE || a->mode == MODE_NEON ||
            a->mode == MODE_WATERFALL) {
            a->random_colors = 1;
        }
    } else if (a->rgb[0] == 0 && a->rgb[1] == 0 && a->rgb[2] == 0) {
        a->brightness = BRIGHTNESS_OFF;
    }
    return 0;
}

static int send_action(hid_device *dev, const struct action *a, int dry_run)
{
    unsigned char rgb[3];

    if (a->want_effect) {
        return send_effect(dev, a->mode, a->brightness, a->random_colors, dry_run);
    }
    memcpy(rgb, a->rgb, sizeof rgb);
    return send_static(dev, rgb, a->brightness, dry_run);
}

/*
 * Seconds since the last input on any HID device, from IOHIDSystem's
 * HIDIdleTime (nanoseconds). Negative on failure.
 */
static long hid_idle_seconds(void)
{
    io_iterator_t iter;
    io_registry_entry_t entry;
    CFTypeRef prop;
    int64_t ns = -1;

    if (IOServiceGetMatchingServices(kIOMainPortDefault,
                                     IOServiceMatching("IOHIDSystem"),
                                     &iter) != KERN_SUCCESS) {
        return -1;
    }
    entry = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (!entry) {
        return -1;
    }

    prop = IORegistryEntryCreateCFProperty(entry, CFSTR("HIDIdleTime"),
                                           kCFAllocatorDefault, 0);
    IOObjectRelease(entry);
    if (!prop) {
        return -1;
    }
    if (CFGetTypeID(prop) == CFNumberGetTypeID()) {
        CFNumberGetValue((CFNumberRef)prop, kCFNumberSInt64Type, &ns);
    }
    CFRelease(prop);
    return ns < 0 ? -1 : (long)(ns / 1000000000LL);
}

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/*
 * Blank the leds after `timeout` idle seconds and restore `on` at the next
 * input. While lit there is nothing to react to, so we sleep straight to the
 * deadline; while dark we poll so the restore feels immediate.
 */
static int cmd_idle(hid_device *dev, const struct action *on, long timeout, long poll)
{
    struct action dark;
    int lit = 1;
    int rc = 0;

    memset(&dark, 0, sizeof dark);
    dark.mode = MODE_STATIC;
    dark.brightness = BRIGHTNESS_OFF;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("idle: off after %lds, %lds poll while dark, ctrl-c restores\n", timeout, poll);
    fflush(stdout);

    if (send_action(dev, on, 0) != 0) {
        return 1;
    }
    g_quiet = 1;

    while (!g_stop) {
        long idle = hid_idle_seconds();

        if (idle < 0) {
            fprintf(stderr, "cannot read HIDIdleTime from IOHIDSystem\n");
            rc = 1;
            break;
        }

        if (lit && idle >= timeout) {
            if (send_action(dev, &dark, 0) != 0) {
                rc = 1;
                break;
            }
            lit = 0;
            printf("idle %lds: leds off\n", idle);
            fflush(stdout);
        } else if (!lit && idle < timeout) {
            if (send_action(dev, on, 0) != 0) {
                rc = 1;
                break;
            }
            lit = 1;
            printf("input after %lds: leds restored\n", idle);
            fflush(stdout);
        }

        if (g_stop) {
            break;
        }
        sleep((unsigned int)(lit ? (timeout - idle > 1 ? timeout - idle : 1) : poll));
    }

    if (!lit) {
        g_quiet = 0;
        printf("restoring leds\n");
        send_action(dev, on, 0);
    }
    return rc;
}

static int cmd_list(void)
{
    struct hid_device_info *list = hid_enumerate(VID, PID);
    struct hid_device_info *it;
    int n = 0;
    char mfg[128], prod[128], serial[128];

    if (!list) {
        printf("no 258A:0049 devices\n");
        return 1;
    }
    for (it = list; it; it = it->next) {
        n++;
        printf("258A:0049 iface=%d usage_page=0x%04x usage=0x%04x release=0x%04x\n",
               it->interface_number, it->usage_page, it->usage, it->release_number);
        printf("  manufacturer=%s product=%s serial=%s\n",
               narrow(it->manufacturer_string, mfg, sizeof mfg),
               narrow(it->product_string, prod, sizeof prod),
               narrow(it->serial_number, serial, sizeof serial));
        printf("  path=%s\n", it->path ? it->path : "(null)");
    }
    hid_free_enumeration(list);
    printf("%d HID collection(s)\n", n);
    return n ? 0 : 1;
}

static int cmd_dump(int report_id)
{
    unsigned char buf[REPORT_LEN];
    int n;
    hid_device *dev;

    printf("opening LED interface:\n");
    dev = open_led_interface();
    if (!dev) {
        return 1;
    }

    memset(buf, 0, sizeof buf);
    buf[0] = (unsigned char)report_id;
    n = hid_get_feature_report(dev, buf, report_id == 5 ? 6 : REPORT_LEN);
    if (n < 0) {
        fprintf(stderr, "hid_get_feature_report(0x%02x) failed: %ls\n", report_id, hid_error(dev));
        hid_close(dev);
        return 1;
    }
    printf("report 0x%02x (%d bytes):\n", report_id, n);
    print_hex(buf, n);
    hid_close(dev);
    return 0;
}

int main(int argc, char **argv)
{
    struct action act;
    int dry_run = 0;
    hid_device *dev;
    int rc;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (hid_init() != 0) {
        fprintf(stderr, "hid_init failed\n");
        return 1;
    }

    if (strcmp(argv[1], "list") == 0) {
        rc = cmd_list();
        hid_exit();
        return rc;
    }

    if (strcmp(argv[1], "dump") == 0) {
        int id = 6;
        if (argc >= 3) {
            id = atoi(argv[2]);
        }
        if (id != 5 && id != 6) {
            fprintf(stderr, "dump only supports report 5 (6 bytes) or 6 (1032 bytes)\n");
            hid_exit();
            return 2;
        }
        rc = cmd_dump(id);
        hid_exit();
        return rc;
    }

    if (strcmp(argv[1], "idle") == 0) {
        long timeout = 300;
        long poll = 1;
        int i = 2;

        for (; i < argc; i++) {
            if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
                timeout = atol(argv[++i]);
            } else if (strcmp(argv[i], "--poll") == 0 && i + 1 < argc) {
                poll = atol(argv[++i]);
            } else {
                break;
            }
        }
        if (timeout < 1 || poll < 1) {
            fprintf(stderr, "--timeout and --poll must be at least 1 second\n");
            hid_exit();
            return 2;
        }
        if (parse_action(argc, argv, i, &act, NULL) != 0) {
            usage(argv[0]);
            hid_exit();
            return 2;
        }
        dev = open_led_interface();
        if (!dev) {
            hid_exit();
            return 1;
        }
        rc = cmd_idle(dev, &act, timeout, poll);
        hid_close(dev);
        hid_exit();
        return rc;
    }

    if (parse_action(argc, argv, 1, &act, &dry_run) != 0) {
        usage(argv[0]);
        hid_exit();
        return 2;
    }

    dev = NULL;
    if (!dry_run) {
        dev = open_led_interface();
        if (!dev) {
            hid_exit();
            return 1;
        }
    }

    rc = send_action(dev, &act, dry_run);
    if (dev) {
        hid_close(dev);
    }
    hid_exit();
    return rc;
}
