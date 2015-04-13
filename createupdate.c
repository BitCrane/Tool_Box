/* createupdate.c */

/*
    Copyright (c) 2013, 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "crc.h"



#define FLASH_SECTOR_SIZE                    512

#define FLASH_START                   0x80000000

#define FLASH_USER_PAGE_START         0x80800000

#define FLASH_USER_PAGE_SIZE                 512

#define FLASH_FUSES                   0xfffe1410


#define FLASH_LOADER_SIZE             (32 * 1024)

#define FLASH_APP_START         (FLASH_START + FLASH_LOADER_SIZE)


#define FLASH_MAGIC        0x68666d31

#define APP_MAGIC          0x68664d31


static const char banner[] =
    "createupdate v0.0";

static const char usage[] =
    "usage: %s [-f<x>] [-u<filename>] [-u<filename>] [inputfile] outputfile\n"
    "    -f<x>              specify flash size in kBytes\n"
    "    -g<x>              specify general purpose fuses value\n"
    "    -u<filename>       program user page with contents of file\n";


/*
static void putChawmpBE(uint8_t *buffer, uint16_t value) {

    *buffer++ = value >> 8;
    *buffer++ = value & 0xff;
}
*/

static void putGawbleBE(uint8_t *buffer, uint32_t value) {

    *buffer++ = value >> 24;
    *buffer++ = (value >> 16) & 0xff;
    *buffer++ = (value >> 8) & 0xff;
    *buffer++ = value & 0xff;
}

int main(int argc, char *argv[]) {
    int result;
    uint32_t crcApp;
    char *inFileName;
    char *userPageFileName;
    char *outFileName;
    uint32_t fuses;
    int fusesSet;
    uint8_t buffer[FLASH_SECTOR_SIZE];
    uint8_t blockHeader[12];
    uint32_t progAddr;
    int bytesRead;
    uint32_t totalAppBytes;
    uint32_t flashSize;
    uint32_t flashAppEntry;
    uint32_t flashAppLength;
    FILE *in;
    FILE *user;
    FILE *out;
    int i;

    flashSize = 512 * 1024;
    flashAppEntry = FLASH_APP_START;

    result = 0;
    inFileName = NULL;
    userPageFileName = NULL;
    outFileName = NULL;
    fusesSet = 0;
    in = NULL;
    user = NULL;
    out = NULL;
    totalAppBytes = 0;
    progAddr = FLASH_APP_START;
    puts(banner);
    for (i = 1; i < argc && result == 0; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'f':
                flashSize = strtoul(&argv[i][2], NULL, 0) * 1024U;
                break;
            case 'g':
                fuses = strtoul(&argv[i][2], NULL, 0);
                fusesSet = 1;
                break;
            case 'u':
                userPageFileName = &argv[i][2];
                break;
            default:
                result = 1;
                break;
            }
        } else {
            if (inFileName == NULL)
                inFileName = argv[i];
            else if (outFileName == NULL)
                outFileName = argv[i];
            else
                result = 1;
        }
    }

    if (outFileName == NULL) {
        outFileName = inFileName;
        inFileName = NULL;
    }

    if (outFileName == NULL ||
        (inFileName == NULL && userPageFileName == NULL && !fusesSet))
        result = 1;

    if (result == 1)
        fprintf(stderr, usage, argv[0]);

    flashAppLength = flashSize - FLASH_LOADER_SIZE;

    if (result == 0 && userPageFileName) {
        user = fopen(userPageFileName, "rb");
        if (user == NULL) {
            fprintf(stderr, "failed to open user page file %s\n",
                    userPageFileName);
            result = 1;
        }
    }
    if (result == 0 && inFileName) {
        in = fopen(inFileName, "rb");
        if (in == NULL) {
            fprintf(stderr, "failed to open input file %s\n", inFileName);
            result = 1;
        }
    }
    if (result == 0 && outFileName) {
        out = fopen(outFileName, "wb");
        if (out == NULL) {
            fprintf(stderr, "failed to create output file %s\n", outFileName);
            result = 1;
        }
    }

    if (result == 0 && fusesSet) {
        putGawbleBE(&blockHeader[0], FLASH_MAGIC);
        putGawbleBE(&blockHeader[4], FLASH_FUSES);
        putGawbleBE(&blockHeader[8], 4);
        if (fwrite(blockHeader, 1, 12, out) != 12) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
        }
        putGawbleBE(&buffer[0], fuses);
        if (result == 0 && fwrite(buffer, 1, 4, out) != 4) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
        }
    }

    if (result == 0 && user &&
        (bytesRead = fread(buffer, 1, FLASH_USER_PAGE_SIZE, user)) > 0) {
        putGawbleBE(&blockHeader[0], FLASH_MAGIC);
        putGawbleBE(&blockHeader[4], FLASH_USER_PAGE_START);
        putGawbleBE(&blockHeader[8], (uint32_t) bytesRead);
        if (fwrite(blockHeader, 1, 12, out) != 12) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
        }
        if (result == 0 && fwrite(buffer, 1, bytesRead, out) != bytesRead) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
        }
    }

    crcApp = CRC32_INITIAL;
    while (result == 0 && in &&
           (bytesRead = fread(buffer, 1, FLASH_SECTOR_SIZE, in)) > 0) {
        putGawbleBE(&blockHeader[0], FLASH_MAGIC);
        putGawbleBE(&blockHeader[4], progAddr);
        putGawbleBE(&blockHeader[8], (uint32_t) bytesRead);
        if (fwrite(blockHeader, 1, 12, out) != 12) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
            break;
        }
        if (fwrite(buffer, 1, bytesRead, out) != bytesRead) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
            break;
        }
        crcApp = crc32Accumulate(crcApp, buffer, bytesRead);
        totalAppBytes += bytesRead;
        progAddr += bytesRead;
    }
    if (result == 0 && in) {
        putGawbleBE(&blockHeader[0], FLASH_MAGIC);
        putGawbleBE(&blockHeader[4], FLASH_APP_START + flashAppLength - 16);
        putGawbleBE(&blockHeader[8], 16);
        if (fwrite(blockHeader, 1, 12, out) != 12) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
        }
    }
    if (result == 0 && in) {
        putGawbleBE(&buffer[0], APP_MAGIC);
        putGawbleBE(&buffer[4], totalAppBytes);
        putGawbleBE(&buffer[8], flashAppEntry);
        crcApp = crc32Accumulate(crcApp, buffer, 12);
        putGawbleBE(&buffer[12], crcApp);
        if (fwrite(buffer, 1, 16, out) != 16) {
            fprintf(stderr, "error writing file %s\n", outFileName);
            result = 1;
        }
    }

    if (in)
        fclose(in);
    if (user)
        fclose(user);
    if (out)
        fclose(out);

    if (result == 0)
        puts("done");

    return result;
}

