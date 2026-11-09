/* genmdl.c - the CyborgOS GEN.MDL ternary GRU, natively in C. Same integer arithmetic as
 * ntc/src/lib/z80/Net.cls and tools/gen/qgru.py IntModel: k balanced-trit planes, Horner-combined
 * (high plane first), arithmetic shift, int16 bias, Tiny activations (1.0 = 64), 256-entry tanh/sigm
 * tables, mul = sat((a*b + 32) >> 6). Output is byte-identical to the Z80 and the PC reference.
 *
 * MDL layout ('GENM' ver V E H L [K] | charset | embed V*E int8 | per layer: k_ih k_hh,
 * W_ih K planes row-interleaved (3H rows x in/4 bytes a plane), b_ih int16 x 3H, W_hh, b_hh |
 * k_out, W_out, b_out int16 x V). Trit packing: 4 a byte, trit j in bits 2j, code 1 = +1, 3 = -1.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "genmdl.h"
#include "tables.h"

#define G_S 64

static int gb_sat(int intV_a)
{
    return (intV_a > 127) ? 127 : ((intV_a < -128) ? -128 : intV_a);
}

static int gb_mul(int intA_a, int intB_a)
{
    return gb_sat((intA_a * intB_a + 32) >> 6);
}

/* trit j of packed byte: code 1 -> +1, 3 -> -1, else 0 */
static int gb_trit(uint8_t byPacked_a, int intJ_a)
{
    int intCode = (byPacked_a >> (2 * intJ_a)) & 3;
    return (intCode == 1) ? 1 : ((intCode == 3) ? -1 : 0);
}

/* one plane's dot: n packed trits (n/4 bytes) with n int8/int values */
static int32_t gb_plane_dot(const uint8_t* ptrPlane_a, const int* ptrX_a, int intN_a)
{
    int32_t i32Acc = 0;
    int intI;
    for (intI = 0; intI < intN_a; intI++)
    {
        i32Acc += gb_trit(ptrPlane_a[intI >> 2], intI & 3) * ptrX_a[intI];
    }
    return i32Acc;
}

/* linear layer: rows outputs, in inputs. ptrW_a row r at r*K*(in/4), plane j at +j*(in/4).
 * out[r] = (Horner_high_first(planes . x) >> shift) + bias[r]  (arithmetic shift = floor) */
static void gb_lin(const uint8_t* ptrW_a, const int16_t* ptrB_a, int intK_a, int intShift_a,
                   const int* ptrX_a, int intIn_a, int intRows_a, int* ptrOut_a)
{
    int intRowBytes = intIn_a >> 2;
    int intR;
    int intJ;
    int64_t i64Acc;
    for (intR = 0; intR < intRows_a; intR++)
    {
        i64Acc = 0;
        for (intJ = intK_a - 1; intJ >= 0; intJ--)
        {
            i64Acc = i64Acc * 3 + gb_plane_dot(ptrW_a + ((size_t)intR * intK_a + intJ) * intRowBytes, ptrX_a, intIn_a);
        }
        ptrOut_a[intR] = (int)(i64Acc >> intShift_a) + ptrB_a[intR];
    }
}

static int16_t gb_le16(const uint8_t* ptr_a)
{
    return (int16_t)(ptr_a[0] | (ptr_a[1] << 8));
}

int gb_genmdl_load(GenMdl* ptrM_a, const uint8_t* ptrData_a, size_t szLen_a)
{
    int blnOk = 1;
    size_t szP = 0;
    int intL;
    int intIn;
    int intR;
    memset(ptrM_a, 0, sizeof(GenMdl));
    if (szLen_a < 12 || memcmp(ptrData_a, "GENM", 4) != 0 || (ptrData_a[4] != 1 && ptrData_a[4] != 2))
    {
        blnOk = 0;
    }
    if (blnOk)
    {
        ptrM_a->intVer = ptrData_a[4];
        ptrM_a->intV = ptrData_a[5];
        ptrM_a->intE = ptrData_a[6];
        ptrM_a->intH = ptrData_a[7];
        ptrM_a->intL = ptrData_a[8];
        szP = 9;
        ptrM_a->intK = 1;
        if (ptrM_a->intVer == 2)
        {
            ptrM_a->intK = ptrData_a[szP];
            szP++;
        }
        if (ptrM_a->intV > G_MAXV || ptrM_a->intH > G_MAXH || ptrM_a->intL > G_MAXL || ptrM_a->intE > G_MAXH
            || ptrM_a->intK < 1 || ptrM_a->intK > 8 || (ptrM_a->intE & 3) || (ptrM_a->intH & 3))
        {
            blnOk = 0;
        }
    }
    if (blnOk)
    {
        ptrM_a->ptrCharset = ptrData_a + szP;
        szP += ptrM_a->intV;
        ptrM_a->ptrEmbed = (const int8_t*)(ptrData_a + szP);
        szP += (size_t)ptrM_a->intV * ptrM_a->intE;
        for (intL = 0; intL < ptrM_a->intL; intL++)
        {
            intIn = (intL == 0) ? ptrM_a->intE : ptrM_a->intH;
            ptrM_a->arrKih[intL] = ptrData_a[szP];
            ptrM_a->arrKhh[intL] = ptrData_a[szP + 1];
            szP += 2;
            ptrM_a->arrWih[intL] = ptrData_a + szP;
            szP += (size_t)ptrM_a->intK * 3 * ptrM_a->intH * (intIn >> 2);
            for (intR = 0; intR < 3 * ptrM_a->intH; intR++)
            {
                ptrM_a->arrBih[intL][intR] = gb_le16(ptrData_a + szP + 2 * intR);
            }
            szP += 6 * ptrM_a->intH;
            ptrM_a->arrWhh[intL] = ptrData_a + szP;
            szP += (size_t)ptrM_a->intK * 3 * ptrM_a->intH * (ptrM_a->intH >> 2);
            for (intR = 0; intR < 3 * ptrM_a->intH; intR++)
            {
                ptrM_a->arrBhh[intL][intR] = gb_le16(ptrData_a + szP + 2 * intR);
            }
            szP += 6 * ptrM_a->intH;
        }
        ptrM_a->intKout = ptrData_a[szP];
        szP++;
        ptrM_a->ptrWout = ptrData_a + szP;
        szP += (size_t)ptrM_a->intK * ptrM_a->intV * (ptrM_a->intH >> 2);
        for (intR = 0; intR < ptrM_a->intV; intR++)
        {
            ptrM_a->arrBout[intR] = gb_le16(ptrData_a + szP + 2 * intR);
        }
        szP += 2 * (size_t)ptrM_a->intV;
        if (szP > szLen_a)
        {
            blnOk = 0;
        }
    }
    if (blnOk)
    {
        gb_genmdl_reset(ptrM_a);
    }
    return blnOk;
}

void gb_genmdl_reset(GenMdl* ptrM_a)
{
    memset(ptrM_a->arrH, 0, sizeof(ptrM_a->arrH));
}

int gb_genmdl_index(const GenMdl* ptrM_a, int intCh_a)
{
    int intI;
    int intFound = 0;
    for (intI = 0; intI < ptrM_a->intV; intI++)
    {
        if (ptrM_a->ptrCharset[intI] == (uint8_t)intCh_a && intFound == 0)
        {
            intFound = intI + 1;
        }
    }
    return (intFound == 0) ? 0 : intFound - 1;
}

/* feed class c; afterwards ptrM_a->arrLogits holds V logits. Returns argmax. */
int gb_genmdl_feed(GenMdl* ptrM_a, int intC_a)
{
    int arrX[G_MAXH];
    int arrGi[3 * G_MAXH];
    int arrGh[3 * G_MAXH];
    int arrHn[G_MAXH];
    int intIn = ptrM_a->intE;
    int intH = ptrM_a->intH;
    int intL;
    int intG;
    int intR;
    int intZ;
    int intN;
    int intBest = 0;
    for (intG = 0; intG < intIn; intG++)
    {
        arrX[intG] = ptrM_a->ptrEmbed[(size_t)intC_a * intIn + intG];
    }
    for (intL = 0; intL < ptrM_a->intL; intL++)
    {
        gb_lin(ptrM_a->arrWih[intL], ptrM_a->arrBih[intL], ptrM_a->intK, ptrM_a->arrKih[intL], arrX, intIn, 3 * intH, arrGi);
        gb_lin(ptrM_a->arrWhh[intL], ptrM_a->arrBhh[intL], ptrM_a->intK, ptrM_a->arrKhh[intL], ptrM_a->arrH[intL], intH, 3 * intH, arrGh);
        for (intG = 0; intG < intH; intG++)
        {
            intR = g_arrSigm[gb_sat(arrGi[intG] + arrGh[intG]) & 255];
            intZ = g_arrSigm[gb_sat(arrGi[intH + intG] + arrGh[intH + intG]) & 255];
            intN = g_arrTanh[gb_sat(arrGi[2 * intH + intG] + gb_mul(intR, gb_sat(arrGh[2 * intH + intG]))) & 255];
            arrHn[intG] = gb_sat(gb_mul(G_S - intZ, intN) + gb_mul(intZ, ptrM_a->arrH[intL][intG]));
        }
        for (intG = 0; intG < intH; intG++)
        {
            ptrM_a->arrH[intL][intG] = arrHn[intG];
            arrX[intG] = arrHn[intG];
        }
        intIn = intH;
    }
    gb_lin(ptrM_a->ptrWout, ptrM_a->arrBout, ptrM_a->intK, ptrM_a->intKout, arrX, intH, ptrM_a->intV, ptrM_a->arrLogits);
    for (intG = 1; intG < ptrM_a->intV; intG++)
    {
        if (ptrM_a->arrLogits[intG] > ptrM_a->arrLogits[intBest])
        {
            intBest = intG;
        }
    }
    return intBest;
}

#ifdef GENMDL_MAIN
/* genmdl MODEL.MDL "prompt" n  -> prints n greedy characters (newline shown as |) */
int main(int intArgc_a, char** arrArgv_a)
{
    int intRet = 1;
    FILE* objF = NULL;
    uint8_t* ptrData = NULL;
    long lngLen = 0;
    GenMdl objM;
    const char* strPrompt;
    int intN;
    int intI;
    int intC = 0;
    int intCh;
    if (intArgc_a >= 4)
    {
        objF = fopen(arrArgv_a[1], "rb");
    }
    if (objF != NULL)
    {
        fseek(objF, 0, SEEK_END);
        lngLen = ftell(objF);
        fseek(objF, 0, SEEK_SET);
        ptrData = (uint8_t*)malloc((size_t)lngLen);
        if (fread(ptrData, 1, (size_t)lngLen, objF) == (size_t)lngLen && gb_genmdl_load(&objM, ptrData, (size_t)lngLen))
        {
            strPrompt = arrArgv_a[2];
            intN = atoi(arrArgv_a[3]);
            fprintf(stderr, "GENM v%d V=%d E=%d H=%d L=%d K=%d\n", objM.intVer, objM.intV, objM.intE, objM.intH, objM.intL, objM.intK);
            for (intI = 0; strPrompt[intI] != 0; intI++)
            {
                intCh = strPrompt[intI];
                if (intCh == '\\' && strPrompt[intI + 1] == 'n')
                {
                    intCh = 10;
                    intI++;
                }
                intC = gb_genmdl_feed(&objM, gb_genmdl_index(&objM, intCh));
            }
            for (intI = 0; intI < intN; intI++)
            {
                intCh = objM.ptrCharset[intC];
                putchar((intCh == 10) ? '|' : intCh);
                intC = gb_genmdl_feed(&objM, intC);
            }
            putchar('\n');
            intRet = 0;
        }
        fclose(objF);
        free(ptrData);
    }
    return intRet;
}
#endif
