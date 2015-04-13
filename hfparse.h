/* hfparse.h */

#ifndef _hfparse_h
#define _hfparse_h

#ifdef __cplusplus
extern "C" {
#endif


typedef struct hfparseS hfparseT;

typedef struct {
    union {
        uint8_t b[8];
        struct hf_header h;
        struct hf_pll_config opPllConfig;
    } h;
    union {
        uint8_t b[4 * 255 + 4];
        uint32_t g[255 + 1];
        struct hf_hash_serial opHashSerial;
        struct hf_hash_usb opHashUsb;
        struct hf_config_data opConfig;
        struct {
            struct hf_g1_monitor monitor;
            uint8_t core_status[24];
        } opStatus;
        struct hf_candidate_nonce opNonce;
        struct hf_group_data opGroup;
        struct hf_statistics opStatistics;
        struct {
            struct hf_usb_init_base base;
            struct hf_config_data config;
        } opUsbInit;
        struct hf_g1_die_data opDieStatus;
        struct hf_gwq_data opGwqStatus;
        struct hf_usb_stats1 opUsbStats1;
        struct hf_usb_notice_data opUsbNotice;
    } d;
} __attribute__ ((packed)) hfPacketT;

typedef struct {
    int includeDataCRC;
    void (*packet)(void *user, hfPacketT *packet);
    void *user;
} hfparseOptsT;

typedef struct {
    unsigned long syncLoss;
    unsigned long bytesDiscarded;
    unsigned long dataCRCErrors;
} hfparseStatsT;


hfparseT *hfparseCreate(hfparseOptsT *opts);

void hfparseRun(hfparseT *obj, unsigned char *d, unsigned int length);

void hfparseStats(hfparseT *obj, hfparseStatsT *stats);

void hfparseDestroy(hfparseT *obj);


#ifdef __cplusplus
}
#endif

#endif /* _hfparse_h */

