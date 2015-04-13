/* thermistor.c */

/*
    Copyright (c) 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>



static const char usage[] =
    "usage: %s [-bvalue] value\n"
    "    -bvalue        specify b (default 3590)\n";


int main(int argc, char *argv[]) {
    int result;
    int value;
    float b, f, r, t;
    int i;

    result = 0;
    b = 3590.0;
    value = -1;
    for (i = 1; i < argc && result == 0; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'b':
                b = strtof(&argv[i][2], NULL);
                break;
            default:
                result = 1;
                break;
            }
        } else {
            if (value == -1)
                value = strtol(argv[i], NULL, 0);
            else
                result = 1;
        }
    }

    if (value == -1)
        result = 1;

    if (result == 1)
        fprintf(stderr, usage, argv[0]);

    if (result == 0) {
        f = (float) value / 1023.0;
        r = 1.0 / (1.0 / f - 1.0);
        t = log(r) / b;
        t += 1.0 / (25.0 + 273.15);
        t = 1.0 / t - 273.15;
        printf("temperature %.1lfC (%.1lfF)\n", (double) t, (double) t * 9.0 / 5.0 + 32.0);
    }

    return result;
}

