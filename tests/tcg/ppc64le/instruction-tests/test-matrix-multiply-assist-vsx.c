#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <altivec.h>
#include <endian.h>
#include <string.h>

bool debug = false;

#define dprintf(...) \
    do { \
        if (debug == true) { \
            fprintf(stderr, "%s: ", __func__); \
            fprintf(stderr, __VA_ARGS__); \
        } \
    } while (0);

bool le;

#define XXMFACC(_AS) \
    ".long 31<<26 | (" #_AS ")<<23 | 177<<1\n"

#define XXMTACC(_AS) \
    ".long 31<<26 | (" #_AS ")<<23 | 1<<16 | 177<<1\n"

#define XXSETACC(_AS) \
    ".long 31<<26 | (" #_AS ")<<23 | 3<<16 | 177<<1\n"

void test_xxmtacc_xxmfacc(void) {
    register vector unsigned char v0 asm ("vs8") = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
     };
    register vector unsigned char v1 asm ("vs9") = {
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
     };
    register vector unsigned char v2 asm ("vs10") = {
        32, 33, 34, 35,
        36, 37, 38, 39,
        40, 41, 42, 43,
        44, 45, 46, 47
     };
    register vector unsigned char v3 asm ("vs11") = {
        48, 49, 50, 51,
        52, 53, 54, 55,
        56, 57, 58, 59,
        60, 61, 62, 63
    };
    uint8_t buf[16] __attribute__((aligned(16))) = { 0 };
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

#if 0
    for (i = 0; i < 128; i++) {
        buf[i] = 0;
    }
#endif

#if 0
    for (i = 0; i < 16; i++) {
        v0[i] = i;
        v1[i] = 16 + i;
        v2[i] = 32 + i;
        v3[i] = 48 + i;
    }
#endif

    /* TODO: lvx is treating vsr numbers as starting from vsr32 */
    /* TODO: that seems accurate, as it precedes VSX, so those
     * registers have been re-mapped to vsr32+
     */
    /* TODO: should this also be true for xxmtacc? doesn't
     * seem so, as it's already defined to use VSRs, and
     * only VSR 0-31 seem to be associated with accumulator
     * regs. so basically we can't use lvx in conjunction
     * with xxmtacc?
     *
     * lxsdx maybe?
     */
    printf("marker 0, buf_ptr: %p\n", buf_ptr);
    asm(XXMTACC(2)
        "lxvb16x %0, 0, %4\n"
        "lxvb16x %1, 0, %4\n"
        "lxvb16x %2, 0, %4\n"
        "lxvb16x %3, 0, %4\n"
        : "+wa" (v0), "+wa" (v1), "+wa" (v2), "+wa" (v3)
        : "r" (buf_ptr));
        //: "=wa" (v0), "=wa" (v1), "=wa" (v2), "=wa" (v3)

    printf("marker 1\n");
    for (i = 0; i < 16; i++) {
        printf("v0[%d]: %d\n", i, v0[i]);
        assert(v0[i] == 0);
        printf("v1[%d]: %d\n", i, v1[i]);
        assert(v1[i] == 0);
        printf("v2[%d]: %d\n", i, v2[i]);
        assert(v2[i] == 0);
        printf("v3[%d]: %d\n", i, v3[i]);
        assert(v3[i] == 0);
    }

    asm(XXMFACC(2)
        : "=wa" (v0), "=wa" (v1), "=wa" (v2), "=wa" (v3));

    printf("marker 2\n");
    for (i = 0; i < 16; i++) {
        printf("v0[%d]: %d\n", i, v0[i]);
        assert(v0[i] == i);
        printf("v1[%d]: %d\n", i, v1[i]);
        assert(v1[i] == 16 + i);
        printf("v2[%d]: %d\n", i, v2[i]);
        assert(v2[i] == 32 + i);
        printf("v3[%d]: %d\n", i, v3[i]);
        assert(v3[i] == 48 + i);
    }
}

void test_xxsetacc(void) {
    register vector unsigned char v0 asm ("vs8") = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
     };
    register vector unsigned char v1 asm ("vs9") = {
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
     };
    register vector unsigned char v2 asm ("vs10") = {
        32, 33, 34, 35,
        36, 37, 38, 39,
        40, 41, 42, 43,
        44, 45, 46, 47
     };
    register vector unsigned char v3 asm ("vs11") = {
        48, 49, 50, 51,
        52, 53, 54, 55,
        56, 57, 58, 59,
        60, 61, 62, 63
    };
    int i;

    printf("marker 0\n");

    asm(XXMTACC(2)
        XXSETACC(2)
        XXMFACC(2)
        : "+wa" (v0), "+wa" (v1), "+wa" (v2), "+wa" (v3));

    printf("marker 1\n");
    for (i = 0; i < 16; i++) {
        printf("v0[%d]: %d\n", i, v0[i]);
        assert(v0[i] == 0);
        printf("v1[%d]: %d\n", i, v1[i]);
        assert(v1[i] == 0);
        printf("v2[%d]: %d\n", i, v2[i]);
        assert(v2[i] == 0);
        printf("v3[%d]: %d\n", i, v3[i]);
        assert(v3[i] == 0);
    }
}

#if 0
#define PLXVP(_Tp, _RA, _d0, _d1, _R, _TX) \
    ".long 1<<26 | (" #_R ")<<20 | (" #_d0 ")\n" \
    ".long 58<<26 | (" #_Tp ")<<22 | (" #_TX ")<<21 | (" #_RA ")<<16 | (" #_d1 ")\n"

void test_plxvp_cia(void) {
    register vector unsigned char v0 asm("vs8") = { 0 };
    register vector unsigned char v1 asm("vs9") = { 0 };
    int i;

    /* load buf[0:31] into vs8,vs9 using CIA with relative offset */
    asm(
        PLXVP(4, 0, 0, 8, 1, 0)
        "b 1f\n"
        ".byte 0x1f\n"
        ".byte 0x1e\n"
        ".byte 0x1d\n"
        ".byte 0x1c\n"
        ".byte 0x1b\n"
        ".byte 0x1a\n"
        ".byte 0x19\n"
        ".byte 0x18\n"
        ".byte 0x17\n"
        ".byte 0x16\n"
        ".byte 0x15\n"
        ".byte 0x14\n"
        ".byte 0x13\n"
        ".byte 0x12\n"
        ".byte 0x11\n"
        ".byte 0x10\n"
        ".byte 0x0f\n"
        ".byte 0x0e\n"
        ".byte 0x0d\n"
        ".byte 0x0c\n"
        ".byte 0x0b\n"
        ".byte 0x0a\n"
        ".byte 0x09\n"
        ".byte 0x08\n"
        ".byte 0x07\n"
        ".byte 0x06\n"
        ".byte 0x05\n"
        ".byte 0x04\n"
        ".byte 0x03\n"
        ".byte 0x02\n"
        ".byte 0x01\n"
        ".byte 0x00\n"
        "1: nop\n"
        : "+wa" (v0), "+wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == i);
        else
            assert(v0[i] == (31 - i));
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == 16 + i);
        else
            assert(v1[i] == 15 - i);
    }
}

void test_plxvp(void) {
    register vector unsigned char v0 asm("vs6") = { 0 };
    register vector unsigned char v1 asm("vs7") = { 0 };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = i;
    }

    /* load buf[0:31] into vs6,vs7 using EA with no offset */
    asm(PLXVP(3, %2, 0, 0, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[31 - i]);
        else
            assert(v0[i] == buf[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[15 - i]);
        else
            assert(v1[i] == buf[16 + i]);
    }

    /* load buf[32:63] into vs6,vs7 using EA with d1 offset */
    buf_ptr = buf_ptr + 32 - 0x1000;
    asm(PLXVP(3, %2, 0, 0x1000, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[63 - i]);
        else
            assert(v0[i] == buf[32 + i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[47 - i]);
        else
            assert(v1[i] == buf[48 + i]);
    }

    /* load buf[0:31] into vs6,vs7 using EA with d0||d1 offset */
    buf_ptr = buf;
    buf_ptr = buf_ptr - ((0x1000 << 16) | 0x1000);
    asm(PLXVP(3, %2, 0x1000, 0x1000, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[31 - i]);
        else
            assert(v0[i] == buf[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[15 - i]);
        else
            assert(v1[i] == buf[16 + i]);
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
    /* load buf[32:63] into vs6,vs7 using EA with negative d0||d1 offset */
    buf_ptr = buf;
    buf_ptr = buf_ptr + 32 + 0x1000;
    asm(PLXVP(3, %2, 0x3ffff, 0xf000, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[63 - i]);
        else
            assert(v0[i] == buf[32 + i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[47 - i]);
        else
            assert(v1[i] == buf[48 + i]);
    }
}

#define PSTXVP(_Sp, _RA, _d0, _d1, _R, _SX) \
    ".long 1<<26 | (" #_R ")<<20 | (" #_d0 ")\n" \
    ".long 62<<26 | (" #_Sp ")<<22 | (" #_SX ")<<21 | (" #_RA ")<<16 | (" #_d1 ")\n"

void test_pstxvp(void) {
    register vector unsigned char v0 asm("vs6") = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
    };
    register vector unsigned char v1 asm("vs7") = {
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
    };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = 0;
    }

    /* store vs6,vs7 into buf[0:31] using EA with no offset */
    asm(PSTXVP(3, %0, 0, 0, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[31 - i]);
        else
            assert(v0[i] == buf[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[15 - i]);
        else
            assert(v1[i] == buf[16 + i]);
    }

    /* store vs6,vs7 into buf[32:63] using EA with d1 offset */
    buf_ptr = buf_ptr + 32 - 0x1000;
    asm(PSTXVP(3, %0, 0, 0x1000, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[63 - i]);
        else
            assert(v0[i] == buf[32 + i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[47 - i]);
        else
            assert(v1[i] == buf[48 + i]);
    }

    /* load buf[0:31] into vs6,vs7 using EA with d0||d1 offset */
    buf_ptr = buf;
    buf_ptr = buf_ptr - ((0x1000 << 16) | 0x1000);
    asm(PSTXVP(3, %0, 0x1000, 0x1000, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[31 - i]);
        else
            assert(v0[i] == buf[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[15 - i]);
        else
            assert(v1[i] == buf[16 + i]);
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
}

/* TODO: we force 2 instead of 1 in opc2 currently to hack around
 * QEMU impl, need a single handler to deal with the 1 in bit 31
 */
#define STXVP(_Sp, _RA, _DQ, _SX) \
    ".long 6<<26 | (" #_Sp ")<<22 | (" #_SX ")<<21 | (" #_RA ")<<16 | (" #_DQ ")<<4 | 1\n"

void test_stxvp(void) {
    register vector unsigned char v0 asm("vs4") = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
    };
    register vector unsigned char v1 asm("vs5") = {
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
    };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = 7;
    }

    /* load v0,v1 into buf[0:31] using EA with no offset */
    asm(STXVP(2, %0, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[i] == v1[15 - i]);
        else
            assert(buf[i] == v0[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[16 + i] == v0[15 - i]);
        else
            assert(buf[16 + i] == v1[i]);
    }

    /* load v0,v1 into buf[32:63] using EA with offset 0x40 */
    buf_ptr = buf_ptr + 32 - 0x40;
    asm(STXVP(2, %0, 4, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[32 + i] == v1[15 - i]);
        else
            assert(buf[32 + i] == v0[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[48 + i] == v0[15 - i]);
        else
            assert(buf[48 + i] == v1[i]);
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
}

#define LXVP(_Tp, _RA, _DQ, _TX) \
    ".long 6<<26 | (" #_Tp ")<<22 | (" #_TX ")<<21 | (" #_RA ")<<16 | (" #_DQ ")<<4\n"

void test_lxvp(void) {
    register vector unsigned char v0 asm("vs4") = { 0 };
    register vector unsigned char v1 asm("vs5") = { 0 };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = i;
    }

    /* load buf[0:31] into v0,v1 using EA with no offset */
    asm(LXVP(2, %2, 0, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[31-i]);
        else
            assert(v0[i] == buf[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[15-i]);
        else
            assert(v1[i] == buf[16+i]);
    }

    /* load buf[32:63] into v0,v1 using EA with 0x40 offset */
    buf_ptr = buf_ptr + 32 - 0x40;
    asm(LXVP(2, %2, 4, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[63-i]);
        else
            assert(v0[i] == buf[32+i]);

    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[47-i]);
        else
            assert(v1[i] == buf[48+i]);
    }

    /* TODO: signed offsets */
    /* TODO: PC-relative addresses */
}

#define LXVPX(_Tp, _RA, _RB, _TX) \
    ".long 31<<26 | (" #_Tp ")<<22 | (" #_TX ")<<21 | (" #_RA ")<<16 | (" #_RB ")<<11 | 333<<1\n"

void test_lxvpx(void) {
    register vector unsigned char v0 asm("vs8") = { 0 };
    register vector unsigned char v1 asm("vs9") = { 0 };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    uint32_t offset;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = i;
    }

    /* load buf[0:31] into v0,v1 using EA with no offset */
    offset = 0;
    asm(LXVPX(4, %2, %3, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr), "r" (offset)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[31-i]);
        else
            assert(v0[i] == buf[i]);

    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[15-i]);
        else
            assert(v1[i] == buf[16+i]);
    }

    /* load buf[32:63] into v0,v1 using EA with 0x40 offset */
    offset = 0x40;
    buf_ptr = buf_ptr + 32 - offset;
    asm(LXVPX(4, %2, %3, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr), "r" (offset)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[63-i]);
        else
            assert(v0[i] == buf[32+i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[47-i]);
        else
            assert(v1[i] == buf[48+i]);
    }

    /* TODO: signed offsets */
    /* TODO: PC-relative addresses */
}

#define STXVPX(_Sp, _RA, _RB, _SX) \
    ".long 31<<26 | (" #_Sp ")<<22 | (" #_SX ")<<21 | (" #_RA ")<<16 | (" #_RB ")<<11 | 461<<1\n"

void test_stxvpx(void) {
    register vector unsigned char v0 asm("vs10") = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
    };
    register vector unsigned char v1 asm("vs11") = {
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
    };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    uint32_t offset;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = 7;
    }

    /* load v0,v1 into buf[0:31] using EA with no offset */
    offset = 0;
    asm(STXVPX(5, %0, %1, 0)
        : "+r" (buf_ptr)
        : "r" (offset), "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[i] == v1[15 - i]);
        else
            assert(buf[i] == v0[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[16 + i] == v0[15 - i]);
        else
            assert(buf[16 + i] == v1[i]);
    }

    /* load v0,v1 into buf[32:63] using EA with offset 0x40 */
    offset = 0x40;
    buf_ptr = buf_ptr + 32 - offset;
    asm(STXVPX(5, %0, %1, 0)
        : "+r" (buf_ptr)
        : "r" (offset), "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[32 + i] == v1[15 - i]);
        else
            assert(buf[32 + i] == v0[i]);
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[48 + i] == v0[15 - i]);
        else
            assert(buf[48 + i] == v1[i]);
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
}
#endif

#define do_test(testname) \
    if (debug) \
        fprintf(stderr, "          -> running test: " #testname "\n"); \
    test_##testname(); \

int main(int argc, char **argv)
{
    le = (htole16(1) == 1);

    if (argc > 1 && !strcmp(argv[1], "-d")) {
        debug = true;
    }

#if 0
    do_test(lxvp);
    do_test(stxvp);
    do_test(plxvp);
    do_test(plxvp_cia);
    do_test(pstxvp);
    do_test(lxvpx);
    do_test(stxvpx);
#endif
    do_test(xxmtacc_xxmfacc);
    do_test(xxsetacc);
    return 0;
}
