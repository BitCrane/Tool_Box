/* hfparse.c */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "swab.h"
#include "crc.h"
#include "hf_protocol.h"
#include "hfparse.h"


struct hfparseS {
    enum {lookingForHeaderPS, readingDataPS} state;
    unsigned int index;
    unsigned int count;
    hfparseOptsT opts;
    hfPacketT p;
    int lastHeaderGood;
    hfparseStatsT stats;
};


hfparseT *hfparseCreate(hfparseOptsT *opts) {
    hfparseT *obj;

    obj = malloc(sizeof(hfparseT));
    if (obj) {
        memset(obj, 0, sizeof(*obj));
        memcpy(&obj->opts, opts, sizeof(obj->opts));
    }

    return obj;
}

void hfparseRun(hfparseT *obj, unsigned char *d, unsigned int length) {
    unsigned int u;

    while (length) {
        switch (obj->state) {
        case lookingForHeaderPS:
            if (obj->index == 8) {
                if (obj->lastHeaderGood) {
                    obj->stats.syncLoss++;
                    obj->lastHeaderGood = 0;
                }
                obj->stats.bytesDiscarded++;
                memmove(&obj->p.h.b[0], &obj->p.h.b[1], 7);
                obj->index--;
            }
            obj->p.h.b[obj->index++] = *d++;
            if (obj->index == 8 &&
                obj->p.h.h.preamble == HF_PREAMBLE &&
                obj->p.h.h.crc8 ==
                crc8Accumulate(CRC8_INITIAL, &obj->p.h.b[1], 8 - 2)) {
                obj->lastHeaderGood = 1;
                obj->count = obj->p.h.h.data_length;
                obj->index = 0;
                if (obj->count) {
                    obj->state = readingDataPS;
                    obj->count *= 4;
                    if (obj->opts.includeDataCRC)
                        obj->count += 4;
                } else if (obj->opts.packet)
                    obj->opts.packet(obj->opts.user, &obj->p);
            }
            length--;
            break;
        case readingDataPS:
            u = length;
            if (u > obj->count)
                u = obj->count;
            memcpy(&obj->p.d.b[obj->index], d, u);
            obj->index += u;
            d += u;
            obj->count -= u;
            length -= u;
            if (obj->count == 0) {
                if (!obj->opts.includeDataCRC ||
                    LEToNativeUGawble(obj->p.d.g[obj->p.h.h.data_length]) ==
                    crc32Accumulate(CRC32_INITIAL, &obj->p.d.b[0],
                                    obj->index - 4)) {
                    if (obj->opts.packet)
                        obj->opts.packet(obj->opts.user, &obj->p);
                } else
                    obj->stats.dataCRCErrors++;
                obj->index = 0;
                obj->state = lookingForHeaderPS;
            }
            break;
        }
    }
}

void hfparseStats(hfparseT *obj, hfparseStatsT *stats) {

    memcpy(stats, &obj->stats, sizeof(*stats));
}

void hfparseDestroy(hfparseT *obj) {

    if (obj)
        free(obj);
}

