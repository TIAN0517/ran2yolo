/* miniz.c - minimal PNG/zlib support */
#include "miniz.h"

#define ZLIB_VERSION_NUM 0x1240

typedef struct inflate_block {
    unsigned char data[16384];
    int size;
} inflate_block_t;

static int read_bits(const unsigned char* in, int* bitpos, int* bitsleft, int n, unsigned int* val) {
    *val = 0;
    while (n > 0) {
        if (*bitsleft == 0) {
            *bitpos++;
            *bitsleft = 8;
        }
        int take = (n < *bitsleft) ? n : *bitsleft;
        *val |= ((in[*bitpos] >> (8 - *bitsleft)) & ((1 << take) - 1)) << (n - take);
        *bitsleft -= take;
        n -= take;
    }
    return 0;
}

int inflate2(unsigned char* dest, unsigned long* destLen,
             const unsigned char* source, unsigned long sourceLen) {
    if (!dest || !destLen || !source) return -1;

    const unsigned char* in = source;
    unsigned long outPos = 0;
    unsigned long maxOut = *destLen;

    int bitPos = 0;
    int bitsLeft = 0;

    // zlib header
    int cmf = in[0];
    int flags = in[1];
    if ((cmf * 256 + flags) % 31 != 0) return -1;

    bitPos = 2;
    bitsLeft = 0;

    unsigned char window[32768];
    int writePos = 0;

    while (outPos < maxOut && bitPos < (int)sourceLen) {
        unsigned int bfinal = 0, btype = 0;
        read_bits(in, &bitPos, &bitsLeft, 1, &bfinal);
        read_bits(in, &bitPos, &bitsLeft, 2, &btype);

        if (btype == 0) {
            // no compression
            read_bits(in, &bitPos, &bitsLeft, (8 - bitsLeft % 8) % 8, NULL);
            unsigned int len = 0, nlen = 0;
            read_bits(in, &bitPos, &bitsLeft, 16, &len);
            read_bits(in, &bitPos, &bitsLeft, 16, &nlen);
            if (len != (~nlen & 0xFFFF)) return -1;

            for (unsigned i = 0; i < len && outPos < maxOut && (bitPos / 8) < (int)sourceLen; i++) {
                dest[outPos++] = in[bitPos / 8];
                bitPos += 8;
            }
        } else if (btype == 1) {
            // fixed huffman
            while (outPos < maxOut) {
                unsigned int code = 0;
                read_bits(in, &bitPos, &bitsLeft, 7, &code);

                if (code < 256) {
                    dest[outPos++] = (unsigned char)code;
                    window[writePos++ % 32768] = (unsigned char)code;
                } else {
                    int len = 0, dist = 0;
                    int sym = code - 257;
                    if (sym < 28) {
                        static const unsigned short lens[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
                        len = lens[sym];
                    }

                    unsigned int bits = 0;
                    if (len > 0) {
                        read_bits(in, &bitPos, &bitsLeft, (sym < 8) ? sym : 7, &bits);
                        if (bits > 0) len += bits - 1;
                    }

                    read_bits(in, &bitPos, &bitsLeft, 5, NULL);
                    dist = 1;
                    unsigned int dbits = 0;
                    read_bits(in, &bitPos, &bitsLeft, dbits, NULL);
                    dist += dbits;

                    for (int i = 0; i < len && outPos < maxOut; i++) {
                        unsigned char c = window[(writePos - dist) & 32767];
                        dest[outPos++] = c;
                        window[writePos++ % 32768] = c;
                    }
                }
            }
        } else {
            return -1;
        }

        if (bfinal) break;
    }

    *destLen = outPos;
    return 0;
}