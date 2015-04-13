/* hcm - HashFast Characterization Monitor */

/*
   Copyright (c) 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "libusb.h"
#include "hf_protocol.h"
#include "hf_factory.h"

#include "board_util.h"

typedef struct {
    uint32_t freq;
    uint8_t F, R, Q, range;
    } pll_entry_t;

#include "hcm_pll_table.h"

#define G1_CORES                384

#define DIMENSION(x) (sizeof(x)/sizeof(x[0]))

#define LO_CORE                 257
#define LO_SET_LIMIT            258
#define LO_GET_PLL              259
#define LO_NAME                 260
#define LO_NOPLLTWEAK           261
#define LO_HASH                 262
#define LO_PLL_R                263
#define LO_PLL_RANGE            264
#define LO_PLOT_ACTUAL          266
#define LO_LINESTYLE            267
#define LO_PRINT_ONLY_GOOD      268
#define LO_MAKE_PLL_TABLE       269
#define LO_SPEED_TEST           270
#define LO_PASS_FAIL            271
#define LO_READ_DIE_SETTINGS    272
#define LO_WRITE_DIE_SETTINGS   273
#define LO_FORCE                274
#define LO_REF_CLOCK            275
#define LO_BAD_INIT             276
#define LO_BAD_CORE             277
#define LO_BAD_READ             278
#define LO_GETNAME              279
#define LO_PUTNAME              280

static struct option longopts[] =
{
 /* { *name has_arg *flag val } */
    {"dac",                 required_argument, 0, 'D'},                 // Set DAC range
    {"die",                 required_argument, 0, 'd'},                 // ""
    {"freq",                required_argument, 0, 'f'},                 // Set frequency
    {"temp",                required_argument, 0, 't'},                 // Temperature setpoints
    {"voltage",             required_argument, 0, 'v'},                 // Set voltages
    {"pll-r",               required_argument, 0, LO_PLL_R},            // Force PLL R
    {"pll-range",           required_argument, 0, LO_PLL_RANGE},        // Force PLL Range

    {"test-args",           no_argument,       0, 'T'},                 // Test args processing and then exit
    {"help",                no_argument,       0, 'h'},                 //
    {"load",                no_argument,       0, 'l'},                 // Test while all die cores hashing
    {"chipload",            no_argument,       0, 'c'},                 // Test while all ASIC cores hashing
    {"loadall",             no_argument,       0, 'c'},                 // Ditto
    {"noisy",               no_argument,       0, 'n'},                 // Make debug noise
    {"output",              required_argument, 0, 'o'},                 // Send output to file as well as stdout
    {"quiet",               no_argument,       0, 'q'},                 // Suppress printing stuff
    {"address",             required_argument, 0, 'a'},                 // USB address for device select
    {"bus",                 required_argument, 0, 'b'},                 // USB Bus for device select
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    {"port",                required_argument, 0, 'p'},                 // USB Port for device select
#endif
    {"enter",               no_argument,       0, 'e'},                 // Tell a platform to enter loader state

    {"plcore",              no_argument,       0, LO_CORE},             // Plot core counts as X-Y plot
    {"tlimit",              required_argument, 0, LO_SET_LIMIT},        // Set thermal limit
    {"getpll",              no_argument,       0, LO_GET_PLL},          // Get PLL parameters for each setup
    {"name",                required_argument, 0, LO_NAME},             // The name to be used for files, plots etc.
    {"noplltweak",          no_argument,       0, LO_NOPLLTWEAK},       // Don't do any special PLL tweaking
    {"hash",                no_argument,       0, LO_HASH},             // Hash non stop across all die for "a long time"
    {"plactual",            no_argument,       0, LO_PLOT_ACTUAL},      // Plot actual X values
    {"linestyle",           required_argument, 0, LO_LINESTYLE},        // Linstyle to use for gnuplot
    {"print-only-good",     no_argument,       0, LO_PRINT_ONLY_GOOD},  // Print only good results (good = slow_good)
    {"make-pll-table",      required_argument, 0, LO_MAKE_PLL_TABLE},   // Print PLL data for sorting (only)
    {"speed-test",          no_argument,       0, LO_SPEED_TEST},       // Run the automated speed test
    {"pass-fail",           no_argument,       0, LO_PASS_FAIL},        // Some sort of manufacturing test
    {"read-die-settings",   no_argument,       0, LO_READ_DIE_SETTINGS},    // Read die settings, if they are stored in device
    {"write-die-settings",  required_argument, 0, LO_WRITE_DIE_SETTINGS},   // Read die settings, if they are stored in device
    {"force",               no_argument,       0, LO_FORCE},                // Force various operations
    {"ref-clock",           required_argument, 0, LO_REF_CLOCK},            // Specify reference clock frequency
    {"bad-init",            no_argument,       0, LO_BAD_INIT},             // Initialize bad core bitmap
    {"bad-core",            required_argument, 0, LO_BAD_CORE},         // Mark a core as bad
    {"bad-read",            no_argument,       0, LO_BAD_READ},         // Read bad core bitmap
    {"getname",             no_argument,       0, LO_GETNAME},          // Read system name
    {"putname",             required_argument, 0, LO_PUTNAME},          // Write system name

    { 0, 0, 0, 0 }
};

typedef struct {
    char *name;
    char *units;
    char *scaled_units;
    float scale;
    int limits[2];
    int low;
    int high;
    int increment;
    bool requested;
    } controlled_variable_t;

static controlled_variable_t vars[] = {
    // These have to be in the same order as the longopts[] above, starting at index 0
    {"dac setting",      "",        "",        1.0,  {0,   2047}, 0, 0, 0, true},   // Firmware can step these
    {"die index",        "",        "",        1.0,  {0,     19}, 0, 0, 0, true},
    {"Frequency",        "MHz",     "MHz",     1.0,  {125, 1200}, 0, 0, 0, true},
    {"Die Temperature",  "deg C",   "deg C",   1.0,  {25,   105}, 0, 0, 0, false},
    {"Voltage",          "mV x 10", "volts",   0.01, {60,   100}, 0, 0, 0, false},

    {"PLL R",            "units",   "units",   1.0,  {1,     32}, 0, 0, 0, false},  // Here and below stepped within hcm
    {"PLL range",        "units",   "units",   1.0,  {1,      7}, 0, 0, 0, false}
    };

#define DAC                 0
#define DIE                 1
#define FREQUENCY           2
#define DIE_TEMPERATURE     3
#define VOLTAGE             4
#define PLL_R               5
#define PLL_range           6

#define MAX_DIE             20

typedef struct {
    int frequency;
    int voltage;
    int dac;
    bool specified;
    } dieset_t;

static dieset_t die_settings[MAX_DIE];

static int noisy;                               // Make some debug noise
static int test_under_load;                     // Test cores while all others (on a die) are hashing
static int test_under_chip_load;                // Test cores while all others (on an ASIC) are hashing
static int use_core_maps;
static bool test_args_processing;
static bool plot_corecount;
static bool get_pll;
static bool no_pll_tweak;
static bool hash_standalone;                    // Really just for FCC test work
static bool do_speed_test;
static bool do_pass_fail;
static bool quiet;
static bool do_read_die_settings;
static bool do_write_die_settings;
static bool do_force;
static bool do_bad_init;
static bool do_bad_core;
static bool do_bad_read;
static bool do_op_name;
static bool do_enter_loader;
static uint8_t die_bad;
static uint8_t core_bad;
static uint8_t reference_clock[MAX_DIE/4];      // XXX Assumes 4 die per part which will be wrong one day
static int speed_test_count, speed_test_bad_count, speed_test_bad_cores, speed_test_total_cores;
static int bus = -1;
static int addr = -1;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
static int port = -1;
#endif

static char op_name[33];

typedef struct {
    libusb_device_handle *handle;
    const struct libusb_endpoint_descriptor *receive_endpoint;
    const struct libusb_endpoint_descriptor *send_endpoint;
    } hcm_usb_t;

static hcm_usb_t hcm_usb;

static void usage(void);
static int convert_arg(char *, controlled_variable_t *);
static void run_test(controlled_variable_t *);
static void run_results(void);
static void run_final_results(void);
static bool process_reply(uint8_t *, int);
static void parse_die_settings(char *);
static void parse_ref_settings(char *);
//static void dump_die_settings(void);
static void dump_bad_core_bitmap(int die, uint16_t *bitmap);
static void read_write_die_settings(void);
static void system_name(void);
static void enter_loader(void);
static hcm_usb_t *open_usb_device(void);
static void close_usb_device(hcm_usb_t *);

static char cmd_line[1024];
static uint32_t thermal_overload_limit;
static int8_t hcm_force_pll_r;
static int8_t hcm_force_pll_range = -1;
static bool plot_actual;
static char linestyle[32] = "linespoints";
static bool print_only_good;
static char pll_table_file[64] = {'\0'};
static char pll_temp[] = ".pll_temp";
static char name_modifier[32] = {'\0'};

static char test_name[64] = {'h','c','m','\0'};
static char test_name_sav[64] = {'h','c','m','\0'};
static char work_dir[] = "gpl";      // ".gplXXXXXX" and make a temporary name

#define MAXR 32
static uint32_t total_pll_combination[MAXR];
static uint32_t good_pll_combination[MAXR];

static FILE *fo = NULL;
static FILE *fp = NULL;             // For a PLL table file

#define RVALUE(r, x) \
    (((x) == DAC) ? (r)->reply.r.dac_setting : \
     ((x) == DIE) ? (r)->reply.r.die_index : \
     ((x) == FREQUENCY) ? (r)->reply.r.frequency : \
     ((x) == DIE_TEMPERATURE) ? (r)->reply.r.die_temperature : \
     ((x) == VOLTAGE) ? (r)->reply.r.core_voltage : 0)

//
// Things to hold replies in allocated linked lists
//
typedef struct {
    struct hf_header h;
    struct hf_characterize_result r;
    uint16_t core_map[96/16];
    } __attribute__((packed)) reply_t;

typedef struct reply_list {
    struct reply_list *next;
    reply_t reply;
    float vco;
    float ref;
    float freq;
    } reply_list_t;

static reply_list_t *reply_head;
static reply_list_t *reply_last;


static void do_plot_xy_corecount(reply_list_t *, int);
static void do_plot_xyz_corecount(reply_list_t *, int, int);

static void hexdump(uint8_t *, int);

static void hexdump(uint8_t *buf, int nb)
    {
    int i, j;

    fprintf(stderr, "hexdump: %d bytes\n", nb);
    for (i = 0; i < nb; i += 8)
        {
        fprintf(stderr, "%04d: ", i);
        for (j = 0; j < 8; j++)
            {
            if (i+j < nb)
                fprintf(stderr, "%02x ", buf[i+j]);
            }
        fprintf(stderr, "\n");
        }
    }


int main(int argc, char *argv[])
    {
    controlled_variable_t *r, *range;
    int opt_index;
    int opt;
    uint32_t val;
    char cmd[64];
    char *p, *e;
    int nb, left;

    for (opt = 0, p = cmd_line, left = sizeof(cmd_line); opt < argc; opt++)
        {
        nb = snprintf(p, left, "%s ", argv[opt]);
        p += nb;
        left -= nb;
        }

    while ((opt = getopt_long(argc, argv, "a:b:p:D:d:eF:f:t:v:o:chlnTq", longopts, &opt_index)) != -1)
        {
        switch (opt)
            {
            case 'a':
                addr = strtoul(optarg, NULL, 0);
                break;

            case 'b':
                bus = strtoul(optarg, NULL, 0);
                break;

#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
            case 'p':
                port = strtoul(optarg, NULL, 0);
                break;
#endif

            case 'D':
            case 'd':
            case 'F':
            case 'f':
            case 't':
            case 'v':
            case LO_PLL_R:
            case LO_PLL_RANGE:
                if (convert_arg(optarg, &vars[opt_index]) < 0)
                    {
                    fprintf(stderr, "Bad value (%s) for --%s!\n", optarg, longopts[opt_index].name);
                    exit(1);
                    }
                break;

            case 'e':
                do_enter_loader = 1;
                break;

            case 'h':
                usage();
                return(0);
                break;

            case 'c':
                test_under_chip_load = 1;
                break;

            case 'l':
                test_under_load = 1;
                break;

            case 'n':
                noisy = 1;
                break;

            case 'o':
                if (!(fo = fopen(optarg, "w+")))
                    {
                    fprintf(stderr, "Cant create output file %s\n", optarg);
                    exit(1);
                    }
                break;

            case 'q':
                quiet = true;
                break;

            case 'T':
                test_args_processing = 1;
                break;

            case LO_CORE:
                plot_corecount = true;
                break;

            case LO_GET_PLL:
                get_pll = true;
                break;

            case LO_SET_LIMIT:
                val = strtoul(optarg, 0, 0);
                if (val < 10 || val > 125)
                    {
                    fprintf(stderr, "That's a silly thermal limit, I won't do it (%d deg C)\n", val);
                    exit(1);
                    }
                thermal_overload_limit = val;
                break;

            case LO_NAME:
                strncpy(test_name, optarg, sizeof(test_name));      // XXX No protection
                break;

            case LO_NOPLLTWEAK:
                no_pll_tweak = true;
                break;

            case LO_HASH:
                hash_standalone = true;
                break;

            case LO_PLOT_ACTUAL:
                plot_actual = true;
                break;

            case LO_LINESTYLE:
                strncpy(linestyle, optarg, sizeof(linestyle));
                break;

            case LO_PRINT_ONLY_GOOD:
                print_only_good = true;
                break;

            case LO_MAKE_PLL_TABLE:
                strncpy(pll_table_file, optarg, sizeof(pll_table_file));
                break;

            case LO_SPEED_TEST:
                do_speed_test = true;
                get_pll = true;
                break;

            case LO_PASS_FAIL:
                do_pass_fail = true;
                break;

            case LO_READ_DIE_SETTINGS:
                do_read_die_settings = true;
                break;

            case LO_WRITE_DIE_SETTINGS:
                parse_die_settings(optarg);
                do_write_die_settings = true;
                break;

            case LO_FORCE:
                do_force = true;
                break;

            case LO_REF_CLOCK:
                parse_ref_settings(optarg);
                do_write_die_settings = true;
                break;

            case LO_BAD_INIT:
                do_bad_init = 1;
                break;

            case LO_BAD_CORE:
                die_bad = strtoul(optarg, &e, 0);
                if (*e++ == '/')
                    {
                    core_bad = strtoul(e, 0, 0);
                    if (core_bad > 95)
                        {
                        fprintf(stderr, "Bad core index, must be between 0 and 95 inclusive\n");
                        exit(1);
                        }
                    if (die_bad >= MAX_DIE)
                        {
                        fprintf(stderr, "Bad die index, must be between 0 and %d inclusive\n", MAX_DIE-1);
                        exit(1);
                        }
                    do_bad_core = 1;
                    }
                else
                    {
                    fprintf(stderr, "You must specify both a die and a core for --bad-core in the form \"die/core\"\n");
                    exit(1);
                    }
                break;

            case LO_BAD_READ:
                do_bad_read = 1;
                break;

            case LO_GETNAME:
                do_op_name = 1;
                op_name[0] = '\0';
                break;

            case LO_PUTNAME:
                do_op_name = 1;
                strncpy(op_name, optarg, 32);
                op_name[32] = '\0';
                break;

            default:
                fprintf(stderr, "ERROR: Unknown option\n");
                usage();
                return(-1);
                break;
            }
        }

    if (test_args_processing)
        exit(0);

    if (do_enter_loader)
        {
        enter_loader();
        exit(0);
        }

    if (do_bad_init && do_bad_core)
        {
        fprintf(stderr, "Can't have both --do-bad-init and --do-bad-core\n");
        exit(1);
        }

    if (do_read_die_settings || do_bad_read)
        read_write_die_settings();      // Never returns

    if (do_write_die_settings || do_bad_init || do_bad_core)
        {
        //dump_die_settings();
        read_write_die_settings();      // Never returns
        }

    if (do_op_name)
        {
        system_name();                  // Never returns
        }

    // Now we're ready to go!
    r = &vars[PLL_R];
    range = &vars[PLL_range];

    hcm_force_pll_r = r->low;
    if (range->low)
        hcm_force_pll_range = range->low;
    if (r->low && !range->low)
        snprintf(name_modifier, sizeof(name_modifier), "_R_%d", r->low);
    else if (!r->low && range->low)
        snprintf(name_modifier, sizeof(name_modifier), "_range_%d", range->low);
    else if (r->low && range->low)
        snprintf(name_modifier, sizeof(name_modifier), "_R_%d_range_%d", r->low, range->low);
    strncpy(test_name_sav, test_name, sizeof(test_name_sav));

    if (pll_table_file[0])
        {
        if (!(fp = fopen(pll_temp, "w+")))
            {
            fprintf(stderr, "Cant create temporary file %s\n", pll_temp);
            exit(1);
            }
        }

    for (;;)
        {
        snprintf(test_name, sizeof(test_name), "%s%s", test_name_sav, name_modifier);

        run_test(vars);
        run_results();

        if (!r->increment && !range->increment)
            break;

        if (r->increment)
            {
            hcm_force_pll_r += r->increment;
            if (hcm_force_pll_r > r->high)
                {
                hcm_force_pll_r = r->low;
                if (range->increment)
                    {
                    hcm_force_pll_range += range->increment;
                    if (hcm_force_pll_range > range->high)
                        break;
                    snprintf(name_modifier, sizeof(name_modifier), "_R_%d_range_%d", hcm_force_pll_r, hcm_force_pll_range);
                    }
                else
                    break;
                }
            if (range->increment)
                snprintf(name_modifier, sizeof(name_modifier), "_R_%d_range_%d", hcm_force_pll_r, hcm_force_pll_range);
            else
                snprintf(name_modifier, sizeof(name_modifier), "_R_%d", hcm_force_pll_r);
            }
        else if (range->increment)
            {
            hcm_force_pll_range += range->increment;
            if (hcm_force_pll_range > range->high)
                break;
            snprintf(name_modifier, sizeof(name_modifier), "_range_%d", hcm_force_pll_range);
            }
        else
            break;

        usleep(1500000);                                            // uC does self reset after each run
        }

    run_final_results();

    snprintf(cmd, sizeof(cmd), "rm -rf %s", work_dir);
    //system(cmd);                                                  // XXX Temp for development

    return(0);
    }

static char *extra[] = {
    "<DAC range>",
    "<Start die #>[<End die #>[<Increment>]]",
    "<Low frequency>[<High frequency>[<Increment>]]",
    "<Temp setpoints>",
    "Voltage",
    "<R divisor value>",
    "<Range value>",
    "",
    "",
    "",
    "",
    "",
    "",
    "<filename>",
    "",
    "<USB Device Address>",
    "<USB Bus Number",
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "<USB Port Number>",
#endif
    "",
    "",
    "<temperature>",
    "",
    "<string>",
    "",
    "",
    "",
    "<string>",
    "",
    "<filename>",
    "",
    "",
    "",
    "<Settings - see doco>",
    "",
    "<Value in MHz>",
    "",
    "<die>/<core>",
    "",
    "",
    "<string>",
};

static void usage()
  {
  struct option *l = longopts;
  int i;

  printf("Usage:\n");
  printf("    hcm [flags]\n");
  printf("Flags:\n");
  for (i = 0; l->name; l++, i++)
      printf("    --%-20s  %s\n", l->name, extra[i]);
  }

// Convert a min/max/incr argument according to a set of legal values
static int convert_arg(char *optarg, controlled_variable_t *v)
    {
    int i, val[3];
    char arg_copy[32];
    char *e, *f = NULL;

    // Find up to 3 numbers in the form a/b/c or a//c or //c or a//
    strncpy(arg_copy, optarg, sizeof(arg_copy));
    e = arg_copy;
    for (i = 0; i < 3; i++)
        {
        if (test_args_processing)
            fprintf(stderr, "convert_arg: i %d e |%s|\n", i, e);
        if (*e && isdigit(*e))
            {
            // There's a number to convert
            if ((f = strchr(e, '/')) || (f = strchr(e, ',')))
                *f++ = '\0';
            val[i] = strtoul(e, &e, 0);
            }
        else
            {
            val[i] = -1;
            }
        e++;
        }

    if (test_args_processing)
        fprintf(stderr, "convert_arg: val[0] %3d val[1] %3d val[2] %3d\n", val[0], val[1], val[2]);

    if (val[2] == 0 || val[2] < (-1))
        {
        fprintf(stderr, "ERROR: --%s increment setting (%d) is illegal\n", v->name, val[2]);
        exit(1);
        }

    v->low = (val[0] < 0) ? v->limits[0] : val[0];

    if (val[1] == -1 && val[2] == -1)
        v->high = v->low;
    else
        {
        v->high = (val[1] < 0) ? v->limits[1] : val[1];
        if (val[2] < 0)
            {
            // Increment wasn't specified.
            if ((v->high - v->low) > 1000)
                v->increment = 100;
            else if ((v->high - v->low) > 200)
                v->increment = 50;
            else if ((v->high - v->low) > 20)
                v->increment = 10;
            else
                v->increment = 1;
            }
        else
            v->increment = val[2];
        }
    if (v->increment < 0)
        v->increment = 1;
    if (v->low == v->high)
        v->increment = 0;

    if (v->low < v->limits[0])
        {
        fprintf(stderr, "ERROR: --%s low setting (%d) cannot be lower than %3d %s\n", v->name, v->low, v->limits[0], v->units);
        exit(1);
        }
    if (v->high > v->limits[1])
        {
        fprintf(stderr, "ERROR: --%s high setting (%d) cannot be higher than %3d %s\n", v->name, v->high, v->limits[1], v->units);
        exit(1);
        }
    if (v->high < v->limits[0])
        {
        fprintf(stderr, "ERROR: --%s high setting (%d) cannot be lower than %3d %s\n", v->name, v->high, v->limits[0], v->units);
        exit(1);
        }
    if (v->high < v->low)
        {
        fprintf(stderr, "ERROR: --%s low setting (%d) cannot be higher than high setting (%3d)\n", v->name, v->low, v->high);
        exit(1);
        }

    if (test_args_processing)
        fprintf(stderr, "convert_arg: %s: low %3d high %3d increment %3d\n", v->name, v->low, v->high, v->increment);

    return(0);
    }

//
// Parse a settings string for --write_die_settings
//
// <die>:<voltage>@<voltage>/<die>:<voltage>@<frequency> etc.
//
// where
//     <die> = Die #, or a range of die, or a wildcard
//     <voltage> = Target voltage in mV (e.g. 810), or "d" followed by digits for a direct DAC setting
//     <frequency> = Hash clock rate in Mhz
//
// A frequency or voltage of 0 disables that die
//
// If <die> is "*" then the associated settings are made to all remaining die.
// In which case settings after this point are ignored.
//
// <die> can be a range, e.g. "1-3" saying write the following data to these three die
//
// Example:
//      0:d1100@625/1:850@675/2-3@700/4:0/*:d1150
//
//      Sets die 0 to:
//          A DAC value of 1100, and hash clock rate of 625 Mhz
//      Sets die 1 to:
//          A voltage level of 0.85V, and a hash clock rate of 675 Mhz
//      Sets die 2 and 3 to:
//          A hash clock rate of 700 Mhz, and default voltage/dac levels
//      Sets die 4 to:
//          Disabled
//      Sets die 5 and above to:
//          A DAC value of 1150, and default hash clock rates (550 Mhz)
//
// We can only do single modules at present. If TWI support to read/write die settings was
// added, we could do characterization and settings in an assembled Sierra.
//
// Use --force ahead of the --write-die-settings flag to override revision level checks and
// always write the new die settings data
//

#if 0
static void dump_die_settings()
    {
    dieset_t *s;
    int i;

    for (i = 0, s = &die_settings[0]; i < MAX_DIE; i++, s++)
        {
        if (s->dac || s->voltage || s->frequency)
            {
            if (i == 0)
                printf("DIE %2d: DAC %4d Voltage %3d Frequency %8d\n", i, s->dac, s->voltage, s->frequency);
            else
                printf("DIE %2d:     %4d         %3d           %8d\n", i, s->dac, s->voltage, s->frequency);
            }
        else
            printf("DIE %2d: DISABLED\n", i);
        }
    }
#endif

static void parse_die_settings(char *optarg)
    {
    dieset_t *s = NULL;

    int die, die_low, die_high, dac, mV, freq;
    char *b, *next, *t, *e;
    int last_die = 0;
    static bool already_called = false;

    if (!optarg || !*optarg)
        {
        fprintf(stderr, "No die settings supplied\n");
        exit(1);
        }

    if (already_called)
        {
        fprintf(stderr, "You can only specify one die settings argument\n");
        exit(1);
        }
    already_called = true;

    b = optarg;
    while (b && *b)
        {
        freq = 550;                                 // Default
        dac = 1150;                                 // Default
        mV = 0;

        next = strchr(b, '/');
        if (next)
            *next++ = '\0';

        t = strchr(b, '@');
        if (t)
            {
            *t++ = '\0';
            freq = (int)strtoul(t, &e, 0);
            if (freq == 0)
                {
                dac = 0;
                mV = 0;
                }
            else if ((freq < 125 || freq > 1200) && (freq != 9999))
                {
                fprintf(stderr, "Bad frequency %d, must be between 125 and 1200 Mhz inclusive\n", freq);
                exit(1);
                }
            }

        t = strchr(b, ':');
        if (t)
            {
            *t++ = '\0';
            if (*t == 'd' || *t == 'D')
                {
                t++;
                dac = (int)strtoul(t, &e, 0);
                mV = 0;
                if (dac == 0)
                    freq = 0;
                else if (dac < 1 || dac > 4097)
                    {
                    fprintf(stderr, "Bad DAC setting %d, must be between 1 and 4097 inclusive\n", dac);
                    exit(1);
                    }
                }
            else
                {
                mV = (int)strtoul(t, &e, 0);
                dac = 0;
                if (mV == 0)
                    freq = 0;
                else if (mV < 600 || mV > 1100)
                    {
                    fprintf(stderr, "Bad voltage setting |%s| (%d), must be between 600 mV and 1100 mV inclusive\n", t, mV);
                    exit(1);
                    }
                }
            }

        if (*b == '*')
            {
            if (!s)
                s = &die_settings[last_die];
            // Fill in all remaining die
            for (die = last_die; die < MAX_DIE; die++)
                {
                s->frequency = freq;
                s->voltage = mV;
                s->dac = dac;
                s->specified = true;
                s++;
                }
            break;
            }
        else
            {
            die = die_low = die_high = (int)strtoul(b, &e, 0);
            if (e == b)
                {
                fprintf(stderr, "Bad die specification \"%s\"\n", b);
                exit(1);
                }
            if (die < last_die)
                {
                fprintf(stderr, "Sorry, this program(mer) is useless, die must be in ascending order\n");
                exit(1);
                }
            if (e && *e++ == '-')
                {
                // A die range?
                die_high = (int)strtoul(e, &e, 0);
                if (die_high < die || die_high >= MAX_DIE)
                    {
                    fprintf(stderr, "Bad die range, %d to %d\n", die, die_high);
                    exit(1);
                    }
                }

            if (!s)
                s = &die_settings[die_low];
            for (die = die_low; die <= die_high; die++)
                {
                s->frequency = freq;
                s->voltage = mV;
                s->dac = dac;
                s->specified = true;
                s++;
                }
            die--;                      // Revert to last die done (so last_die gets set correctly)
            }

        // Now we have all the settings

        last_die = die;
        b = next;
        }
    }


// Parse a settings string for --ref-clock
//
// <value>[/<value>]...
//
// where
//     <value> = Reference clock frequency, MHz
//
// Specified in module order. Last value is replicated for any remaining modules
//
// E.g.
//
//      25           Set all module reference clocks to 25 Mhz
//      25/125       Set first module to 25 Mhz, all remaining to 125 Mhz
// etc.

static void parse_ref_settings(char *optarg)
    {
    char *b, *next;
    int module = 0;
    uint32_t val = 0;
    static bool already_called = false;

    if (!optarg || !*optarg)
        {
        fprintf(stderr, "No reference clock settings supplied\n");
        exit(1);
        }

    if (already_called)
        {
        fprintf(stderr, "You can only specify one reference clock(s) argument\n");
        exit(1);
        }
    already_called = true;

    b = optarg;
    while (b && *b)
        {
        next = strchr(b, '/');
        if (next)
            *next++ = '\0';

        val = strtoul(b, 0, 0);
        if (val < 10 || val > 200)
            {
            fprintf(stderr, "Reference clock frequency (%d) is out of range for module %d\n", val, module);
            exit(1);
            }
        reference_clock[module++] = val;

        b = next;
        }

    // Fill remaining entries with last set value
    if (val)
        {
        while (module < MAX_DIE/4)
            reference_clock[module++] = val;
        }
    }

////////////////////////////////////////////////////////////////////////////////
// libusb interfaces
////////////////////////////////////////////////////////////////////////////////

static void close_usb_device(hcm_usb_t *u)
    {
    libusb_release_interface(u->handle, HF_BULK_INTERFACE);
    libusb_close(u->handle);
    libusb_exit(NULL);
    }

static hcm_usb_t *open_usb_device()
    {
    libusb_device **devices;
    libusb_device_handle *dev = NULL;
    struct libusb_device_descriptor desc;
    struct libusb_config_descriptor *cdesc;
    const struct libusb_interface *interface;
    const struct libusb_interface_descriptor *idesc;
    int i, j, count;

    if (libusb_init(NULL))
        {
        fprintf(stderr, "ERROR: Failed to initialize libusb\n");
        return(NULL);
        }
    libusb_set_debug(NULL, 0);
    count = libusb_get_device_list(NULL, &devices);
    if (count < 0)
        {
        fprintf(stderr, "Failed to get libusb device list\n");
        return(NULL);
        }

    for (i = 0, j = -1; i < count; i++)
        {
        if ((bus < 0 || bus == libusb_get_bus_number(devices[i])) &&
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
                (port < 0 || port == libusb_get_port_number(devices[i])) &&
#endif
                (addr < 0 || addr == libusb_get_device_address(devices[i])) &&
                libusb_get_device_descriptor(devices[i], &desc) == LIBUSB_SUCCESS)
            {
            if ((desc.idVendor == HF_USB_VENDOR_ID && desc.idProduct == HF_USB_PRODUCT_ID_G1))
                {
                if (j >= 0)
                    {
                    fprintf(stderr, "More than one device found, have to specify by address/bus/port\n");
                    libusb_free_device_list(devices, 1);
                    return(NULL);
                    }
                else
                    j = i;
                }
            }
        }

    if (j == -1)
        {
        fprintf(stderr, "No device found\n");
        libusb_free_device_list(devices, 1);
        return(NULL);
        }

    if (libusb_open(devices[j], &dev))
        {
        fprintf(stderr, "Failed to open device\n");
        dev = NULL;
        }

    if (dev)
        {
        if (libusb_kernel_driver_active(dev, HF_BULK_INTERFACE) == 1)
            if (libusb_detach_kernel_driver(dev, HF_BULK_INTERFACE))
                {
                fprintf(stderr, "Failed to detach kernel driver on interface\n");
                dev = NULL;
                }
        if (dev)
            {
            if (libusb_get_active_config_descriptor(devices[j], &cdesc))
                {
                fprintf(stderr, "Failed to get active config descriptor\n");
                dev = NULL;
                }
            else
                {
                interface = cdesc->interface + HF_BULK_INTERFACE;
                idesc = interface->altsetting;
                if (idesc->bInterfaceNumber != HF_BULK_INTERFACE || idesc->bNumEndpoints != 2)
                    {
                    fprintf(stderr, "I find the interface descriptor confusing\n");
                    dev = NULL;
                    }
                else
                    {
                    hcm_usb.handle = dev;
                    hcm_usb.receive_endpoint = idesc->endpoint;
                    hcm_usb.send_endpoint = idesc->endpoint + 1;
                    }
                }
            }
        }

    libusb_free_device_list(devices, 1);

    return((dev) ? &hcm_usb : NULL);
    }

static int send_frame(hcm_usb_t *s, uint8_t *frame, int nbytes)
    {
    uint8_t *p = frame;
    int nb = nbytes;
    int sts;

    do
        {
        int sent;

        sts = libusb_bulk_transfer(s->handle, s->send_endpoint->bEndpointAddress, p, nb, &sent, 1000);
        if (sts != 0)
            {
            fprintf(stderr, "libusb_bulk_transfer() failed while sending: %d %s\n", sts, libusb_strerror(sts));
            exit(1);
            }
        nb -= sent;
        p += sent;
        } while (nb);

    return(0);
    }

static int receive_frame(hcm_usb_t *s, uint8_t *buf, int bufsize, int expected)
    {
    int sts, rxbytes = 0;
    int got;
    int i;

    // Skip OP_USB_NOTICE and OP_STATUS that may come.
    unsigned char throwaway[1024];
    int discarding_frame = 0;
    struct hf_header *h;

    if (!expected)
        return(0);

    memset(buf, 0, bufsize);
    h = (struct hf_header *)&throwaway[0];

    for (i = 0, got = 0; i < 10000; i++)
        {
        sts = libusb_bulk_transfer(s->handle, s->receive_endpoint->bEndpointAddress, &throwaway[got], sizeof(throwaway)-got, &rxbytes, 0);
        if (sts != 0)
            {
            fprintf(stderr, "libusb_bulk_transfer() failed while receiving: rxbytes %d, sts %d [%s]\n", rxbytes, sts, libusb_strerror(sts));
            }
        got += rxbytes;

        if (got > 8)
            {
            // XXX Validate CRC sometime
            if (h->crc8 != hfa_crc8((unsigned char *)h))
                {
                fprintf(stderr, "Bad header CRC, hcm can't deal with this. Buffer:\n");
                hexdump(&throwaway[0], got);
                }
                
            if (h->operation_code == OP_USB_NOTICE || h->operation_code == OP_USB_STATS1)
                {
                int length = (int)throwaway[6] * 4;

                discarding_frame = 1;
                if (got > sizeof(struct hf_header) + length)
                    {
                    if (h->operation_code == OP_USB_NOTICE)
                        fprintf(stderr, "Discarding annoying OP_USB_NOTICE (got %d, message length %d): |%s|\n", got, length-4, &throwaway[8+4]);
                    //hexdump(&throwaway[0], got);
                    memmove(&throwaway[0], &throwaway[sizeof(struct hf_header)+length], got-(sizeof(struct hf_header)+length));
                    got -= sizeof(struct hf_header) + length;
                    discarding_frame = 0;
                    rxbytes = 0;
                    continue;
                    }
                }
            else
                {
                discarding_frame = 0;
                }
            }

        if (!discarding_frame && (got >= expected))
            break;
        usleep(20000);
        }
    if (got == expected)
        memcpy(buf, throwaway, got);
    else
        got = 0;                    // Toss it

    return(got);
    }

//
// Dump the bad core bitmap
//
static void dump_bad_core_bitmap(int die, uint16_t *bitmap)
    {
    uint16_t *p;
    int i, j;
    uint16_t mask;
    int core;
    int module = die >> 2;
    bool got_bad = false;
    int bad = 0;

    for (i = 0, p = bitmap, core = 0; i < G1_CORES/16; p++, i++)
        {
        if (*p != 0xffff)
            {
            for (j = 0, mask = 0x1; j < 16; j++, mask <<= 1)
                {
                if (!(*p & mask))
                    {
                    if (got_bad == false)
                        {
                        printf("Module %d: BAD CORES:\n", module);
                        got_bad = true;
                        }
                    printf("   die %2d core %2d is marked bad\n", die, core);
                    if (bad++ == 15)
                        {
                        printf("    (...more)\n");
                        return;
                        }
                    }
                if (++core == 96)
                    {
                    die++;
                    core = 0;
                    }
                }
            }
        else
            {
            core += 16;
            if (core == 96)
                {
                die++;
                core = 0;
                }
            }
        }

    if (got_bad == false)
        printf("Module %d: NO BAD CORES\n", module);

    }

//
// Read or Write die settings
//
// Only really works for a single module for now (die range 0-3)
//

#define MAX_SLAVES  4

static bool diagnostic_power(hcm_usb_t *u, bool on)
    {
    struct hf_header powerup, powerup_reply;
    int rx;

    powerup.preamble = HF_PREAMBLE;
    powerup.operation_code = OP_POWER;
    powerup.chip_address = 0;
    powerup.core_address = 0;
    powerup.hdata = (on) ? DIAGNOSTIC_POWER_ON : DIAGNOSTIC_POWER_OFF;
    powerup.data_length = 0;
    powerup.crc8 = hfa_crc8((unsigned char *) &powerup);
    send_frame(u, (uint8_t *)&powerup, sizeof(powerup));

    if (on)
        {
        rx = receive_frame(u, (uint8_t *)&powerup_reply, sizeof(powerup_reply), sizeof(powerup_reply));
        if (rx != sizeof(powerup_reply))
            {
            fprintf(stderr, "No reply to OP_POWER\n");
            exit(1);
            }
        }
    return(true);
    }

#define POWER_ON        diagnostic_power(u, true)
#define POWER_OFF       diagnostic_power(u, false)

// Tell a system to enter firmware load state
// The only reason this is here is because it was a lot easier to add this here than to change enterloader.c
// to support the address, bus and port flags.
static void enter_loader()
    {
    hcm_usb_t *u;
    struct hf_header h;

    hfa_init_crc8();
    if (!(u = open_usb_device()))
        exit(1);

    POWER_ON;
    usleep(1000000);

    memset(&h, 0, sizeof(h));
    h.preamble = HF_PREAMBLE;
    h.operation_code = OP_DFU;
    h.crc8 = hfa_crc8((unsigned char *) &h);
    send_frame(u, (uint8_t *)&h, sizeof(h));
    usleep(250000);
    close_usb_device(u);
    exit(0);
    }

// Set or get a system's name, using OP_NAME
static void system_name()
    {
    hcm_usb_t *u;
    int rx;
    bool name_not_set = false;
    int i;

    struct {
        struct hf_header h;
        char name[32];
        } __attribute__((packed)) nameframe, *n = &nameframe;

    hfa_init_crc8();
    if (!(u = open_usb_device()))
        exit(1);

    POWER_ON;
    usleep(1000000);

    memset(n, 0, sizeof(*n));
    n->h.preamble = HF_PREAMBLE;
    n->h.operation_code = OP_NAME;

    if (op_name[0])
        {
        strncpy(n->name, op_name, 32);
        n->h.core_address = 1;
        n->h.data_length = 32/4;
        n->h.crc8 = hfa_crc8((unsigned char *) &n->h);
        send_frame(u, (uint8_t *)n, sizeof(*n));
        }
    else
        {
        n->h.data_length = 0;
        n->h.crc8 = hfa_crc8((unsigned char *) &n->h);
        send_frame(u, (uint8_t *)n, sizeof(n->h));
        rx = receive_frame(u, (uint8_t *)n, sizeof(*n), sizeof(*n));

        if (rx != sizeof(*n))
            {
            fprintf(stderr, "Bad reply (%d bytes) to OP_NAME read request\n", rx);
            POWER_OFF;
            exit(1);
            }
        for (i = 0; i < 32; i++)
            {
            if (n->name[i] == '\0')         // NULL termination on a string
                break;
            if (!isprint(n->name[i]))
                {
                name_not_set = true;
                break;
                }
            }

        if (name_not_set)
            {
            printf("System name doesn't seem to be set, read:\n  0x");
            for (i = 0; i < 32; i++)
                printf("%02x", (unsigned char)n->name[i]);
            printf("\n");
            }
        else
            printf("System name is \"%.32s\"\n", n->name);
        }

    usleep(250000);                                                 // Leave USB link alive so last transaction can go through properly
    POWER_OFF;

    close_usb_device(u);
    exit(0);
    }



static void read_write_die_settings(void)
    {
    dieset_t *s;
    hcm_usb_t *u;
    int i;
    int rx;
    int module, max_module;
    bool inhibit_read_print = false;

    // What the USB frame needs to look like for die settings
    struct {
        struct hf_header h;
        op_settings_t new_settings;
        } __attribute__((packed)) new, *n = &new;

    struct {
        struct hf_header h;
        uint16_t bad_core_bitmap[G1_CORES/16];
        } __attribute__((packed)) bad, *b = &bad;

    hfa_init_crc8();
    if (!(u = open_usb_device()))
        exit(1);

    POWER_ON;
    usleep(1000000);

    memset(n, 0, sizeof(*n));
    n->h.preamble = HF_PREAMBLE;
    n->h.operation_code = OP_SETTINGS;
    n->h.chip_address = 0;
    n->h.hdata = U_MAGIC;

    max_module = 4;

    for (module = 0; module <= max_module; module++)
        {
        // Get the existing settings
        n->h.chip_address = module;
        n->h.core_address = 0;
        n->h.data_length = 0;
        n->h.crc8 = hfa_crc8((unsigned char *) &n->h);
        //fprintf(stderr, "Sending request, module %d\n", module);
        send_frame(u, (uint8_t *)n, sizeof(n->h));
        rx = receive_frame(u, (uint8_t *)n, sizeof(*n), sizeof(*n));

        if (rx != sizeof(*n))
            {
            fprintf(stderr, "Bad reply (%d bytes) to OP_SETTINGS request\n", rx);
            POWER_OFF;
            exit(1);
            }

        if (module == 0)
            {
            max_module = n->h.chip_address;         // Number of slaves
            //fprintf(stderr, "Setting max_module to %d\n", max_module);
            }

        if (!(VALID_DIE_SETTINGS(n->new_settings)))
            {
            printf("WARNING: Die settings have never been set for module %d\n", module);
            printf("READ:\n");
            for (i = 0; i < 4 /* MAX_DIE */; i++)
                printf("  DIE %2d: Voltage 0x%04x Frequency %d\n", i + 4*module, n->new_settings.die[i].voltage, n->new_settings.die[i].frequency);

            if (!do_read_die_settings)
                inhibit_read_print = true;
            // Set version etc. for a write
            n->new_settings.revision = DIE_SETTINGS_REVISION;
            n->new_settings.magic = U_MAGIC;
            if (do_write_die_settings)
                {
                for (i = 0, s = &die_settings[4*module]; i < 4 /* MAX_DIE */; i++, s++)
                    if (!s->specified)
                        {
                        fprintf(stderr, "ERROR: To write settings to a previously unwritten module, you must specify settings for all die\n");
                        POWER_OFF;
                        exit(1);
                        }
                if (!reference_clock[module])
                    {
                    fprintf(stderr, "ERROR: To write settings to a previously unwritten module, you must specify the reference clock\n");
                    POWER_OFF;
                    exit(1);
                    }
                if (!do_bad_init)
                    {
                    fprintf(stderr, "ERROR: To write settings to a previously unwritten module, you must also initialize the bad core bitmap with --bad-init\n");
                    POWER_OFF;
                    exit(1);
                    }
                }
            }
        else if (n->new_settings.revision != DIE_SETTINGS_REVISION)
            {
            fprintf(stderr, "Unknown revision number %d for die settings on module %d\n", n->new_settings.revision, module);
            if (!do_force)
                {
                POWER_OFF;
                exit(1);
                }
            }

        if (do_read_die_settings && !inhibit_read_print)
            {
            printf("Current die settings (module %d), rev = %d, reference = %d MHz\n", module, n->new_settings.revision, n->new_settings.ref_frequency);
            for (i = 0; i < 4 /* MAX_DIE */; i++)
                {
                if (n->new_settings.die[i].voltage & 0x8000)
                    printf("  DIE %2d: DAC     0x%04x    Frequency %6d\n", i+4*module, n->new_settings.die[i].voltage & 0x7fff, n->new_settings.die[i].frequency);
                else
                    printf("  DIE %2d: Voltage    %03x mV Frequency %6d\n", i+4*module, n->new_settings.die[i].voltage & 0xfff, n->new_settings.die[i].frequency);
                }
            }

        if (do_bad_read)
            {
            memset(b, 0, sizeof(bad));
            b->h.preamble = HF_PREAMBLE;
            b->h.operation_code = OP_BAD_CORE;
            b->h.chip_address = module<<2;
            b->h.crc8 = hfa_crc8((unsigned char *) &b->h);

            send_frame(u, (uint8_t *)b, sizeof(b->h));                   // Header only
            rx = receive_frame(u, (uint8_t *)b, sizeof(bad), sizeof(bad));

            if (rx != sizeof(bad))
                {
                fprintf(stderr, "Bad reply (%d bytes) to OP_BAD_CORE read request\n", rx);
                POWER_OFF;
                exit(1);
                }
            dump_bad_core_bitmap(module<<2, b->bad_core_bitmap);
            }

        if (!do_write_die_settings && !do_bad_core && !do_bad_init)
            {
            usleep(1000000);
            continue;
            }

        if (do_write_die_settings)
            {
            // Overwrite any fields that have changed
            n->new_settings.revision = DIE_SETTINGS_REVISION;           // May have been --force'd
            if (reference_clock[module])
                n->new_settings.ref_frequency = reference_clock[module];
            printf("WRITE:\n");
            for (i = 0, s = &die_settings[4*module]; i < 4 /* MAX_DIE */; i++, s++)
                {
                if (s->specified)
                    {
                    if (s->dac == 0 && s->voltage == 0 && s->frequency == 0)
                        {
                        n->new_settings.die[i].voltage = 0;
                        n->new_settings.die[i].frequency = 0;
                        }
                    else
                        {
                        n->new_settings.die[i].frequency = s->frequency;
                        if (s->dac)
                            n->new_settings.die[i].voltage = 0x8000 |  s->dac;
                        else
                            n->new_settings.die[i].voltage = s->voltage;
                        if (s->frequency == 9999)
                            {
                            n->new_settings.revision = 42;              // Invalidate settings
                            n->new_settings.die[i].frequency = 0xffff;
                            }
                        }
                    printf("  DIE %2d: Voltage 0x%04x Frequency %d Mhz\n", i+4*module, n->new_settings.die[i].voltage, n->new_settings.die[i].frequency);
                    }
                }

            // Fix up the header and write the new settings back
            n->h.data_length = U32SIZE(op_settings_t);                  // Fix up size
            n->h.core_address = 1;                                      // Write enable
            n->h.chip_address = module;                                 // Module # (0 == master, 1,2... etc = slaves)
            n->h.crc8 = hfa_crc8((unsigned char *) &n->h);

            send_frame(u, (uint8_t *)n, sizeof(*n));
            usleep(1000000);
            }

        if (do_bad_init)
            {
            memset(b, 0, sizeof(bad));
            b->h.preamble = HF_PREAMBLE;
            b->h.operation_code = OP_BAD_CORE;
            b->h.chip_address = module << 2;
            b->h.core_address = 1;
            b->h.data_length = 0;
            b->h.hdata = 0x8000;
            b->h.crc8 = hfa_crc8((unsigned char *) &b->h);

            send_frame(u, (uint8_t *)b, sizeof(b->h));       // Header only
            usleep(1000000);
            }

        if (do_bad_core)
            {
            if ((die_bad>>2) == module)
                {
                // Bad init, or bad core is on this module
                memset(b, 0, sizeof(bad));
                b->h.preamble = HF_PREAMBLE;
                b->h.operation_code = OP_BAD_CORE;
                b->h.chip_address = die_bad;
                b->h.core_address = 1;
                b->h.data_length = 0;
                b->h.hdata = 0x4000 | core_bad;
                b->h.crc8 = hfa_crc8((unsigned char *) &b->h);

                send_frame(u, (uint8_t *)b, sizeof(b->h));       // Header only
                usleep(1000000);
                }
            }
        }

    usleep(250000);                                                 // Leave USB link alive so last transaction can go through properly
    POWER_OFF;

    close_usb_device(u);
    exit(0);
    }

//
// Run the characterization
//
static void run_test(controlled_variable_t *cv)
    {
    hcm_usb_t *u;

    struct {
        struct hf_header h;
        struct hf_characterize c;
        } __attribute__((packed)) frame1, *f = &frame1;

    int sts;
    int rxbytes;
    int nb;
    int i;
    uint8_t buf[256];
    bool last = false;
    unsigned char *p;
    controlled_variable_t *v = cv;

    hfa_init_crc8();
    if (!(u = open_usb_device()))
        exit(1);

    memset(f, 0, sizeof(*f));

    f->h.preamble = HF_PREAMBLE;
    f->h.operation_code = OP_CHARACTERIZE;
    f->h.chip_address = 0;
    f->h.core_address = 0;
    f->h.hdata = U_MAGIC;
    f->h.data_length = sizeof(f->c)/4;
    f->h.crc8 = hfa_crc8((unsigned char *) &f->h);

    f->c.dac_low = (uint16_t)v->low;
    f->c.dac_high = (uint16_t)v->high;
    f->c.dac_incr = (uint8_t)v->increment;
    v++;
    f->c.die_low = (uint16_t)v->low;
    f->c.die_high = (uint16_t)v->high;
    f->c.die_incr = (uint8_t)v->increment;
    v++;
    f->c.f_low = (uint16_t)v->low;
    f->c.f_high = (uint16_t)v->high;
    f->c.f_incr = (uint8_t)v->increment;
    v++;
    f->c.temp_low = (uint8_t)v->low;
    f->c.temp_high = (uint8_t)v->high;
    f->c.temp_incr = (uint8_t)v->increment;
    v++;
    f->c.v_low = (uint16_t)v->low;
    f->c.v_high = (uint16_t)v->high;
    f->c.v_incr = (uint8_t)v->increment;

    if (test_under_load)
        f->c.flags |= F_TEST_WITH_ALL_DIE_CORES_HASHING;
    if (test_under_chip_load)
        f->c.flags |= F_TEST_WITH_ALL_ASIC_CORES_HASHING;
    if (use_core_maps)
        f->c.flags |= F_RETURN_CORE_MAPS;
    f->c.thermal_override = (uint8_t) thermal_overload_limit;
    if (get_pll)
        f->c.flags |= F_RETURN_PLL_PARAMETERS;
    if (no_pll_tweak)
        f->c.flags |= F_DONT_DO_PLL_TWEAKUP;
    if (hash_standalone)
        f->c.flags |= F_HASH_STANDALONE;

    if (hcm_force_pll_r > 0)
        {
        f->c.flags |= F_FORCE_PLL_R;
        f->c.other[0] = (uint8_t)hcm_force_pll_r;
        }
    if (hcm_force_pll_range >= 0)
        {
        f->c.flags |= F_FORCE_PLL_RANGE;
        f->c.other[1] = (uint8_t)hcm_force_pll_range;
        }
    if (do_speed_test)
        f->c.flags |= F_PLL_TABLE_SWEEP;

    p = (uint8_t *)f;
    nb = sizeof(*f);
    do
        {
        int sent;

        sts = libusb_bulk_transfer(u->handle, u->send_endpoint->bEndpointAddress, p, nb, &sent, 1000);
        if (sts != 0)
            {
            fprintf(stderr, "libusb_bulk_transfer() failed while sending: %s\n", libusb_strerror(sts));
            exit(1);
            }
        //fprintf(stderr, "hcm send, nb %d, sent %d\n", nb, sent);
        nb -= sent;
        p += sent;
        } while (nb);

    do
        {
        memset(buf, 0, sizeof(buf));
        for (i = 0; i < 10000; i++)
            {
            sts = libusb_bulk_transfer(u->handle, u->receive_endpoint->bEndpointAddress, buf, sizeof(buf), &rxbytes, 0);
            if (sts != 0)
                {
                fprintf(stderr, "libusb_bulk_transfer() failed while receiving: %s\n", libusb_strerror(sts));
                exit(1);
                }
            if (rxbytes)
                break;
            usleep(20000);
            }

        if (rxbytes)
            {
            last = process_reply(buf, rxbytes);
            }

        } while (rxbytes && !last);

    close_usb_device(u);

    if (fo)
        {
        fclose(fo);
        fo = NULL;
        }

    return;
    }


static bool process_reply(uint8_t *buf, int nbytes)
    {
    static const char *fmt1 = " die %2d: %3d Mhz @%.2fV %.1fC dac %4d:  %2d good (base %2d h %d) flags 0x%02x %s\n";
    reply_t *r;
    uint8_t crc8;
    char extra[64];
    float ref;
    float vco;
    float freq;
    uint32_t F, R, Q, range;
    bool pll_used = false;

    r = (reply_t *)buf;

    crc8 = hfa_crc8(buf);

    if (r->h.preamble != HF_PREAMBLE
     || r->h.crc8 != crc8
     || r->h.operation_code != OP_CHAR_RESULT)
        {
        fprintf(stderr, "process_reply: Bad frame\n");
        return(false);                              // Toss the frame
        }

    // Here for a valid header, which is an OP_CHAR_RESULT
    if (r->h.data_length < sizeof(r->r)/4)
        {
        fprintf(stderr, "process_reply: Valid frame but wrong length\n");
        return(false);
        }

    if (r->r.error_code)
        {
        fprintf(stderr, "process_reply: Error code %d\n", r->r.error_code);
        return(false);
        }

    // Here for a valid complete OP_CHAR_RESULT frame, which may have a core map

    if (GN_DIE_TEMPERATURE(r->r.die_temperature) > (float)100.0)
        {
        fprintf(stderr, "  Strange die temperature %.2f from value %d (0x%x)\n",
                GN_DIE_TEMPERATURE(r->r.die_temperature), r->r.die_temperature, r->r.die_temperature);
        r->r.die_temperature = GN_THERMAL_CUTOFF(99.9);
        }

    extra[0] = '\0';
    if (r->r.flags & F_PLL_DATA_RETURNED)
        {
        F = (r->r.other_data & 0xff) + 1;
        R = ((r->r.other_data >> 8) & 0x3f) + 1;
        Q = (r->r.other_data >> 16) & 0x7;
        range = (r->r.other_data >> 24) & 0x7;

        if ((r->r.other_data & 0x80000000) == 0)
            {
            pll_used = true;
            ref = 125.0 / (float)R;
            vco = ref * 2.0 * (float)F;
            freq = vco / (float)(1<<Q);
            }
        else
            {
            // PLL is disabled
            F = 0;
            R = 0;
            Q = 0;
            ref = 0.0;
            vco = 0.0;
            freq = 125.0;
            }

        snprintf(extra, sizeof(extra), "(PLL: F %2d R %2d Q %d rng %d (ref=%5.2f vco=%4d f=%5.2f MHz)",
            F, R, Q, range, ref, (uint32_t)vco, freq);
        }
    else
        {
        ref = 0.0;
        vco = 0.0;
        freq = 0.0;
        }

    ++total_pll_combination[R];
    if (pll_used && (r->r.good_core_count == r->r.good_cores_low_speed))
        ++good_pll_combination[R];

    if (do_pass_fail)
        {
        if (r->r.good_core_count < r->r.good_cores_low_speed)
            {
            printf("BOARD FAILED TEST:\n");
            printf(fmt1,  r->r.die_index,
                  r->r.frequency,
                  GN_CORE_VOLTAGE(r->r.measured_voltage),
                  GN_DIE_TEMPERATURE(r->r.die_temperature),
                  r->r.dac_setting,
                  r->r.good_core_count,
                  r->r.good_cores_low_speed,
                  r->r.good_half_cores,
                  r->r.flags,
                  extra);
            exit(1);
            }
        }

    if (!quiet && (print_only_good == false || (pll_used && (r->r.good_core_count == r->r.good_cores_low_speed))))
        {
        if (do_speed_test)
            {
            ++speed_test_count;
            speed_test_total_cores += r->r.good_cores_low_speed;
            if (r->r.good_core_count < r->r.good_cores_low_speed)
                {
                ++speed_test_bad_count;
                speed_test_bad_cores += r->r.good_cores_low_speed - r->r.good_core_count;
                printf(fmt1,  r->r.die_index,
                      r->r.frequency,
                      GN_CORE_VOLTAGE(r->r.measured_voltage),
                      GN_DIE_TEMPERATURE(r->r.die_temperature),
                      r->r.dac_setting,
                      r->r.good_core_count,
                      r->r.good_cores_low_speed,
                      r->r.good_half_cores,
                      r->r.flags,
                      extra);
                }
            }
        else
            printf(fmt1,  r->r.die_index,
                  r->r.frequency,
                  GN_CORE_VOLTAGE(r->r.measured_voltage),
                  GN_DIE_TEMPERATURE(r->r.die_temperature),
                  r->r.dac_setting,
                  r->r.good_core_count,
                  r->r.good_cores_low_speed,
                  r->r.good_half_cores,
                  r->r.flags,
                  extra);

        if (fo)
            {
              fprintf(fo, fmt1,
                  r->r.die_index,
                  r->r.frequency,
                  GN_CORE_VOLTAGE(r->r.measured_voltage),
                  GN_DIE_TEMPERATURE(r->r.die_temperature),
                  r->r.dac_setting,
                  r->r.good_core_count,
                  r->r.good_cores_low_speed,
                  r->r.good_half_cores,
                  r->r.flags,
                  extra);
            }

        if (fp)
            {
            fprintf(fp, "%d %d %.2f %d %d %d %d\n", (uint32_t)(freq * 1000000.0), (uint32_t)vco, ref, F, R, Q, range);
            fflush(fp);
            }
        }

    // Remember the reply for later
    if (!reply_last)
        {
        reply_head = (reply_list_t *) malloc(sizeof(reply_list_t));
        reply_last = reply_head;
        }
    else
        {
        reply_last->next = (reply_list_t *) malloc(sizeof(reply_list_t));
        reply_last = reply_last->next;
        }
    if (!reply_last)
        {
        fprintf(stderr, "Allocation error\n");
        exit(1);
        }
    reply_last->next = NULL;
    memcpy((char *)&reply_last->reply, (char *)r, sizeof(reply_t));      // XXX + Core map
    reply_last->vco = vco;
    reply_last->ref = ref;
    reply_last->freq = freq;

    if (r->r.flags & F_LAST)
        return(true);
    else
        return(false);
    }

//
// Analyze the results
//
static void run_results()
    {

    if (!reply_head)
        return;

    if (plot_corecount)
        {
        // Figure out what to plot
        if (vars[DAC].increment && vars[FREQUENCY].increment)
            {
            do_plot_xyz_corecount(reply_head, DAC, FREQUENCY);
            }
        else if (vars[FREQUENCY].increment)
            do_plot_xy_corecount(reply_head, FREQUENCY);
        }
    }

static void run_final_results()
    {
    char buf[256];
    bool did_last = false;
    FILE *fc;
    int i, n;
    int tot_good, tot_total;
    time_t ltime;


    if (do_speed_test)
        {
        printf("Speed test result: %d degraded PLL divisors out of %d combinations, %.3f%% core degredation\n",
            speed_test_bad_count, speed_test_count, (float)speed_test_bad_cores / (float)speed_test_total_cores * 100.0);
        }

    if (fp)
        {
        fclose(fp);
        fp = NULL;

        snprintf(buf, sizeof(buf), "cat %s | sort -n | uniq >& %s.1", pll_temp, pll_temp);
        system(buf);

        snprintf(buf, sizeof(buf), "%s.1", pll_temp);
        if ((fp = fopen(buf, "r")))
            {
            snprintf(buf, sizeof(buf), "%s_pll_table.h", test_name);
            if (!(fc = fopen("buf", "w+")))
                {
                fprintf(stderr, "Can't create pll table file");
                exit(1);
                }
            fprintf(fc, "//\n");
            time(&ltime);
            fprintf(fc, "// Generated by hcm on %s", asctime(localtime(&ltime)));
            fprintf(fc, "// Command line used:\n\n// %s\n", cmd_line);
            fprintf(fc, "//\n\n");
            fprintf(fc, "typedef struct {\n    uint32_t freq;\n    uint8_t F, R, Q, range;\n    } pll_entry_t;\n\n");

            fprintf(fc, "const pll_entry_t pll_table[] = {\n");
            fprintf(fc, "    //  freq     F  R  Q  range\n");
            while (fgets(buf, sizeof(buf), fp) == buf)
                {
                uint32_t freq, vco, F, Q, R, range;
                float ref;

                if ((n = sscanf(buf, "%u%u%f%d%d%d%d", &freq, &vco, &ref, &F, &Q, &R, &range)) > 0)
                    {
                    if (did_last)
                        {
                        did_last = false;
                        fprintf(fc, ",\n");
                        }
                    fprintf(fc, "    {%u, %2d, %d, %d, %d}", freq, F, Q, R, range);
                    did_last = true;
                    }
                }
            fprintf(fc, "\n    };\n");

            for (i = 0; i < 2; i++)
                {
                FILE *s = (i == 1) ? fc : stdout;
                char *comment = (i == 1) ? "// " : "";

                tot_good = 0;
                tot_total = 0;
                fprintf(s, "%sFinal PLL results:\n", comment);
                fprintf(s, "%s R   Good   from Total\n", comment);
                fprintf(s, "%s--   ----        -----\n", comment);
                for (n = vars[PLL_R].low; n <= vars[PLL_R].high; n++)
                    {
                    fprintf(s, "%s%2d    %4d     %5d\n", comment, n, good_pll_combination[n], total_pll_combination[n]);
                    tot_good += good_pll_combination[n];
                    tot_total += total_pll_combination[n];
                    }
                fprintf(s, "%s%d good results found out of a total of %d combinations\n", comment, tot_good, tot_total);
                fflush(s);
                }

            fclose(fc);

            }
        else
            {
            fprintf(stderr, "Failed to open sorted PLL tables file %s\n", buf);
            exit(1);
            }
        }

    if (do_pass_fail)
        printf("PASSED\n");
    }

//
// Plot "good" core count on Y axis vs. some variable on the X axis
//
static void do_plot_xy_corecount(reply_list_t *rhead, int xvar)
    {
    controlled_variable_t *v, *d;
    reply_list_t *r;
    char fdata[128];
    char fplot[128];
    char cmd[128];
    FILE *fs;
    int die, xvalue;
    static int pass;

    snprintf(cmd, sizeof(cmd), "mkdir -p %s", work_dir);        // XXX Temporary cheat for development
    system(cmd);

    // Create a data file and extract the required data
    snprintf(fdata, sizeof(fdata), "%s/%s.dat", work_dir, "xy_corecount");
    if (!(fs = fopen(fdata, "w+")))
        {
        fprintf(stderr, "Cant create temporary file %s\n", fdata);
        exit(1);
        }


    v = &vars[xvar];
    d = &vars[DIE];
    die = d->low;

    if (plot_actual == true)
        {
        for (r = rhead; r; r = r->next)
            {
            fprintf(fs, "%.2f ", r->freq);
            fprintf(fs, "%2d ", r->reply.r.good_cores_low_speed);       // Actually the same in all records for a given die
            fprintf(fs, "%2d\n", r->reply.r.good_core_count);
            }
        }
    else
        {
        xvalue = v->low;
        do
            {
            if (plot_actual == false)
                fprintf(fs, "%4d ", xvalue);
            d = &vars[DIE];
            die = d->low;
            do
                {
                for (r = rhead; r; r = r->next)
                    {
                    if (r->reply.r.die_index == die && RVALUE(r,xvar) == xvalue)    // XXX Abstract specific X variable
                        {
                        fprintf(fs, "%2d ", r->reply.r.good_cores_low_speed);       // Actually the same in all records for a given die
                        fprintf(fs, "%2d ", r->reply.r.good_core_count);
                        break;
                        }
                    }
                die += d->increment;
                } while (die <= d->high && d->increment);
            fprintf(fs, "\n");
            xvalue += v->increment;
            } while (xvalue <= v->high && v->increment);
        }
    fclose(fs);
    fs = NULL;

    // Create a file that contains the required plotting commands
    pass++;
    snprintf(fplot, sizeof(fplot), "%s/%s_%d.gpl", work_dir, "xy_corecount", pass);
    if (!(fs = fopen(fplot, "w+")))
        {
        fprintf(stderr, "Cant create temporary file %s\n", fplot);
        exit(1);
        }

    fprintf(fs, "set title \"Good cores vs. %s (%s)\"\n", v->name, test_name);
    fprintf(fs, "set ylabel \"Good core count\"\n");
    fprintf(fs, "set xlabel \"%s %s (%s)\"\n", (v->requested) ? ((plot_actual) ? "Actual" : "Requested") : "", v->name, v->units);
    fprintf(fs, "set yrange [0:120]\n");
    fprintf(fs, "set grid\n");
    fprintf(fs, "set key bottom left\n");

    fprintf(fs, "plot \\\n");
    fprintf(fs, "  '%s' using 1:2 with %s title \"Baseline\", \\\n", fdata, linestyle);
    for (d = &vars[DIE], die = d->low; die <= d->high; die += d->increment)
        {
        fprintf(fs, " '%s' using 1:%d with %s title \"die %d\"%s\n", fdata, (die-d->low)*2+3, linestyle, die, (die == d->high) ? "" : ",\\");
        if (!d->increment)
            break;
        }
#if 1
    //fprintf(fs, "set terminal png size 1800,800 enhanced font \"Helvetica,20\"\n");
    fprintf(fs, "set terminal png size 1200,800 enhanced\n");
    fprintf(fs, "set out \"%s.png\"\n", test_name);
    fprintf(fs, "replot\n");
#endif
#if 0
    fprintf(fs, "set terminal emf font \"Helvetica,20\" size 1800,800\n");
    fprintf(fs, "set out \"%s.emf\"\n", "cores");
    fprintf(fs, "set out \"%s.emf\"\n", test_name);
    fprintf(fs, "replot\n");
#endif

    fprintf(fs, "quit\n");
    fclose(fs);
    fs = NULL;

    // Plot it
    snprintf(cmd, sizeof(cmd), "gnuplot --persist < %s", fplot);
    system(cmd);

    }
//
// Surface plot of "good" cores in Z direction, vs X and Y variables
//
static void do_plot_xyz_corecount(reply_list_t *rhead, int xvar, int yvar)
    {

#if NOT_DONE_YET
    controlled_variable_t *vx, *d;
    reply_list_t *r;
    char fdata[128];
    char fplot[128];
    char cmd[128];
    FILE *fs;
    int die, xvalue, yvalue;

    snprintf(cmd, sizeof(cmd), "mkdir -p %s", work_dir);        // XXX Temporary cheat for development
    system(cmd);

    // Create a data file and extract the required data
    snprintf(fdata, sizeof(fdata), "%s/%s.dat", work_dir, "xyz_corecount");
    if (!(fs = fopen(fdata, "w+")))
        {
        fprintf(stderr, "Cant create temporary file %s\n", fdata);
        exit(1);
        }

    vx = &vars[xvar];
    vy = &vars[yvar];
    xvalue = vx->low;
    yvalue = vy->low;
    do
        {
        fprintf(fs, "%4d ", xvalue);
        d = &vars[DIE];
        die = d->low;
        do
            {
            for (r = rhead; r; r = r->next)
                {
                if (r->reply.r.die_index == die && RVALUE(r,xvar) == xvalue)    // XXX Abstract specific X variable
                    {
                    fprintf(fs, "%2d ", r->reply.r.good_cores_low_speed);       // Actually the same in all records for a given die
                    fprintf(fs, "%2d ", r->reply.r.good_core_count);
                    break;
                    }
                }
            die += d->increment;
            } while (die <= d->high && d->increment);
        fprintf(fs, "\n");
        xvalue += vx->increment;
        } while (xvalue <= vx->high && vx->increment);
    fclose(fs);
    fs = NULL;

    // Create a file that contains the required plotting commands
    snprintf(fplot, sizeof(fplot), "%s/%s.gpl", work_dir, "xy_corecount");
    if (!(fs = fopen(fplot, "w+")))
        {
        fprintf(stderr, "Cant create temporary file %s\n", fplot);
        exit(1);
        }

    fprintf(fs, "set title \"Good cores vs. %s (%s)\"\n", vx->name, test_name);
    fprintf(fs, "set ylabel \"Good core count\"\n");
    fprintf(fs, "set xlabel \"%s %s (%s)\"\n", (vx->requested) ? "Requested" : "", vx->name, vx->units);
    fprintf(fs, "set yrange [0:120]\n");
    fprintf(fs, "set grid\n");
    fprintf(fs, "set key bottom left\n");

    fprintf(fs, "plot \\\n");
    fprintf(fs, "  '%s' using 1:2 with linespoints title \"Baseline\", \\\n", fdata);
    for (d = &vars[DIE], die = d->low; die <= d->high; die += d->increment)
        {
        fprintf(fs, " '%s' using 1:%d with linespoints title \"die %d\"%s\n", fdata, (die-d->low)*2+3, die, (die == d->high) ? "" : ",\\");
        if (!d->increment)
            break;
        }
#if 1
    //fprintf(fs, "set terminal png size 1800,800 enhanced font \"Helvetica,20\"\n");
    fprintf(fs, "set terminal png size 1800,800 enhanced\n");
    fprintf(fs, "set out \"%s.png\"\n", test_name);
    fprintf(fs, "replot\n");
#endif
#if 0
    fprintf(fs, "set terminal emf font \"Helvetica,20\" size 1800,800\n");
    fprintf(fs, "set out \"%s.emf\"\n", "cores");
    fprintf(fs, "set out \"%s.emf\"\n", test_name);
    fprintf(fs, "replot\n");
#endif

    fprintf(fs, "quit\n");
    fclose(fs);
    fs = NULL;

    // Plot it
    snprintf(cmd, sizeof(cmd), "gnuplot --persist < %s", fplot);
    system(cmd);

#endif

    }
