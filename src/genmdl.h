#ifndef GENMDL_H
#define GENMDL_H
#include <stdint.h>
#include <stddef.h>
#define G_MAXV 128
#define G_MAXH 128
#define G_MAXL 2
typedef struct
{
    int intVer, intV, intE, intH, intL, intK, intKout;
    const uint8_t* ptrCharset;
    const int8_t* ptrEmbed;
    const uint8_t* arrWih[G_MAXL];
    const uint8_t* arrWhh[G_MAXL];
    const uint8_t* ptrWout;
    int arrKih[G_MAXL];
    int arrKhh[G_MAXL];
    int16_t arrBih[G_MAXL][3 * G_MAXH];
    int16_t arrBhh[G_MAXL][3 * G_MAXH];
    int16_t arrBout[G_MAXV];
    int arrH[G_MAXL][G_MAXH];
    int arrLogits[G_MAXV];
} GenMdl;
int gb_genmdl_load(GenMdl* ptrM_a, const uint8_t* ptrData_a, size_t szLen_a);
void gb_genmdl_reset(GenMdl* ptrM_a);
int gb_genmdl_index(const GenMdl* ptrM_a, int intCh_a);
int gb_genmdl_feed(GenMdl* ptrM_a, int intC_a);
#endif
