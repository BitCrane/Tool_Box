/* crc.h */

/*
   Copyright (c) 2013, 2014 HashFast Technologies LLC
*/

#ifndef _crc_h
#define _crc_h

#ifdef __cplusplus
extern "C" {
#endif


#define CRC32_INITIAL      0xffffffff
#define CRC8_INITIAL             0xff


uint32_t crc32Accumulate(uint32_t crc, uint8_t *data, unsigned int len);
uint8_t crc8Accumulate(uint8_t crc, uint8_t *data, unsigned int len);


#ifdef __cplusplus
}
#endif

#endif /* _crc_h */

