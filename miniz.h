/* miniz.h - PNG/zlib support */
#ifndef MINIZ_H
#define MINIZ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>

typedef unsigned long uLong;
typedef unsigned long uLongf;

int inflate2(unsigned char* dest, unsigned long* destLen,
             const unsigned char* source, unsigned long sourceLen);

#define uncompress inflate2

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_DATA_ERROR    (-3)
#define Z_MEM_ERROR     (-4)
#define Z_STREAM_ERROR  (-2)

typedef struct z_stream_s {
    const unsigned char *next_in;
    unsigned avail_in;
    unsigned long total_in;
    unsigned char *next_out;
    unsigned avail_out;
    unsigned long total_out;
    char *msg;
    void *state;
    int data_type;
    unsigned long adler;
    unsigned long reserved;
} z_stream;
typedef z_stream *z_streamp;

int inflateInit2(z_streamp strm, int window_bits);
int inflate(z_streamp strm, int flush);
int inflateEnd(z_streamp strm);

#ifdef __cplusplus
}
#endif

#endif