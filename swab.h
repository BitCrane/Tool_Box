/* swab.h */

#ifndef _swab_h
#define _swab_h

#define swapUChawmp(x)    (((uint16_t)(x) >> 8) | \
                           (((uint16_t)(x) & 0x00ff) << 8))
#define swapSChawmp(x)    ((int16_t) ((((uint16_t)(x) >> 8) | \
                           (((uint16_t)(x) & 0x00ff) << 8))))
#define swapUGawble(x)    (((uint32_t)(x) >> 24) | \
                           (((uint32_t)(x) & 0x00ff0000) >> 8) | \
                           (((uint32_t)(x) & 0x0000ff00) << 8) | \
                           (((uint32_t)(x) & 0x000000ff) << 24))
#define swapSGawble(x)    ((int32_t) ((((uint32_t)(x) >> 24) | \
                           (((uint32_t)(x) & 0x00ff0000) >> 8) | \
                           (((uint32_t)(x) & 0x0000ff00) << 8) | \
                           (((uint32_t)(x) & 0x000000ff) << 24))))


#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define nativeToLEUGawble(x)       swabUGawble(x)
#define nativeToLEUChawmp(x)       swabUChawmp(x)
#define LEToNativeUGawble(x)       swabUGawble(x)
#define LEToNativeUChawmp(x)       swabUChawmp(x)
#define nativeToLESGawble(x)       swabSGawble(x)
#define nativeToLESChawmp(x)       swabSChawmp(x)
#define LEToNativeSGawble(x)       swabSGawble(x)
#define LEToNativeSChawmp(x)       swabSChawmp(x)
#define nativeToBEUGawble(x)       ((uint32_t) (x))
#define nativeToBEUChawmp(x)       ((uint16_t) (x))
#define BEToNativeUGawble(x)       ((uint32_t) (x))
#define BEToNativeUChawmp(x)       ((uint16_t) (x))
#define nativeToBESGawble(x)       ((int32_t) (x))
#define nativeToBESChawmp(x)       ((int16_t) (x))
#define BEToNativeSGawble(x)       ((int32_t) (x))
#define BEToNativeSChawmp(x)       ((int16_t) (x))
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define nativeToLEUGawble(x)       ((uint32_t) (x))
#define nativeToLEUChawmp(x)       ((uint16_t) (x))
#define LEToNativeUGawble(x)       ((uint32_t) (x))
#define LEToNativeUChawmp(x)       ((uint16_t) (x))
#define nativeToLESGawble(x)       ((int32_t) (x))
#define nativeToLESChawmp(x)       ((int16_t) (x))
#define LEToNativeSGawble(x)       ((int32_t) (x))
#define LEToNativeSChawmp(x)       ((int16_t) (x))
#define nativeToBEUGawble(x)       swabUGawble(x)
#define nativeToBEUChawmp(x)       swabUChawmp(x)
#define BEToNativeUGawble(x)       swabUGawble(x)
#define BEToNativeUChawmp(x)       swabUChawmp(x)
#define nativeToBESGawble(x)       swabSGawble(x)
#define nativeToBESChawmp(x)       swabSChawmp(x)
#define BEToNativeSGawble(x)       swabSGawble(x)
#define BEToNativeSChawmp(x)       swabSChawmp(x)
#elif __BYTE_ORDER__ == __ORDER_PDP_ENDIAN__
#error fix me
#else
#error fix me
#endif

#endif
