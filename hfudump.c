/* hfudump.c */

/*
    Copyright (c) 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "hf_loader_p.h"



#define FLASH_START                   0x80000000


static const char usage[] =
    "usage: %s [-f<x>] [inputfile]\n"
    "    -f<x>              specify flash size in kBytes\n";


/*
static uint16_t getChawmpBE(FILE *s) {
    uint16_t v;

    v = (uint16_t) getc(s) << 8;
    v |= (uint16_t) getc(s);

    return v;
}
*/

static uint32_t getGawbleBE(FILE *s) {
    uint32_t v;

    v = (uint32_t) getc(s) << 24;
    v |= (uint32_t) getc(s) << 16;
    v |= (uint32_t) getc(s) << 8;
    v |= (uint32_t) getc(s);

    return v;
}

int main(int argc, char *argv[]) {
    int result;
    hfLoaderBlockHeaderT block;
    hfLoaderAppSuffixT suffix;
    uint32_t flashSize;
    uint32_t suffixAddr;
    FILE *s;
    int i;

    s = stdin;

    flashSize = 512 * 1024;

    result = 0;
    for (i = 1; i < argc && result == 0; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'f':
                flashSize = strtoul(&argv[i][2], NULL, 0) * 1024U;
                break;
            default:
                result = 1;
                break;
            }
        } else {
            if (s == stdin) {
                s = fopen(argv[1], "rb");
                if (s == NULL)
                    result = 1;
            } else
                result = 1;
        }
    }

    if (result == 1)
        fprintf(stderr, usage, argv[0]);

    suffixAddr = FLASH_START + flashSize - sizeof(hfLoaderAppSuffixT);

    while (result == 0) {
        block.magic = getGawbleBE(s);
        if (feof(s))
            break;
        else if (ferror(s)) {
            fprintf(stderr, "error reading input\n");
            result = 1;
        } else {
            block.addr = getGawbleBE(s);
            block.length = getGawbleBE(s);
            if (block.magic == HF_LOADER_BLOCK_MAGIC) {
                if (block.addr <= suffixAddr &&
                    block.addr + block.length >= suffixAddr +
                                                 sizeof(hfLoaderAppSuffixT)) {
                    while (block.addr++ < suffixAddr)
                        if (getc(s) == EOF) {
                            fprintf(stderr, "unexpected end of file\n");
                            result = 1;
                        }
                    suffix.magic = getGawbleBE(s);
                    suffix.length = getGawbleBE(s);
                    suffix.entry = getGawbleBE(s);
                    suffix.crc = getGawbleBE(s);
                    if (suffix.magic == HF_LOADER_SUFFIX_MAGIC) {
                        printf("entry:  0x%08x\n",
                               (unsigned int) suffix.entry);
                        printf("length: 0x%08x\n",
                               (unsigned int) suffix.length);
                        printf("crc:    0x%08x\n",
                               (unsigned int) suffix.crc);
                        break;
                    } else {
                        fprintf(stderr, "bad suffix\n");
                        result = 1;
                    }
                } else
                    while (block.length--)
                        if (getc(s) == EOF) {
                            fprintf(stderr, "unexpected end of file\n");
                            result = 1;
                        }
            } else {
                fprintf(stderr, "bad block header\n");
                result = 1;
            }
        }
    }

    if (s && s != stdin)
        fclose(s);

    return result;
}

