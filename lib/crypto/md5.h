/*
 * md5.h -- RFC 1321 MD5 implementation (public domain)
 * Single-header, no dependencies.
 */
#ifndef MD5_H
#define MD5_H
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buf[64];
} MD5_CTX;

static inline uint32_t md5_F(uint32_t x,uint32_t y,uint32_t z){return (x&y)|(~x&z);}
static inline uint32_t md5_G(uint32_t x,uint32_t y,uint32_t z){return (x&z)|(y&~z);}
static inline uint32_t md5_H(uint32_t x,uint32_t y,uint32_t z){return x^y^z;}
static inline uint32_t md5_I(uint32_t x,uint32_t y,uint32_t z){return y^(x|~z);}
static inline uint32_t md5_rot(uint32_t x,int n){return (x<<n)|(x>>(32-n));}

static const uint32_t md5_T[64]={
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static void md5_transform(uint32_t s[4], const uint8_t blk[64]) {
    uint32_t a=s[0],b=s[1],c=s[2],d=s[3],x[16];
    for(int i=0;i<16;i++) x[i]=(uint32_t)blk[i*4]|((uint32_t)blk[i*4+1]<<8)|((uint32_t)blk[i*4+2]<<16)|((uint32_t)blk[i*4+3]<<24);
    #define MD5_STEP(f,a,b,c,d,k,s,i) a=b+md5_rot(a+f(b,c,d)+x[k]+md5_T[i],s)
    MD5_STEP(md5_F,a,b,c,d, 0, 7, 0); MD5_STEP(md5_F,d,a,b,c, 1,12, 1);
    MD5_STEP(md5_F,c,d,a,b, 2,17, 2); MD5_STEP(md5_F,b,c,d,a, 3,22, 3);
    MD5_STEP(md5_F,a,b,c,d, 4, 7, 4); MD5_STEP(md5_F,d,a,b,c, 5,12, 5);
    MD5_STEP(md5_F,c,d,a,b, 6,17, 6); MD5_STEP(md5_F,b,c,d,a, 7,22, 7);
    MD5_STEP(md5_F,a,b,c,d, 8, 7, 8); MD5_STEP(md5_F,d,a,b,c, 9,12, 9);
    MD5_STEP(md5_F,c,d,a,b,10,17,10); MD5_STEP(md5_F,b,c,d,a,11,22,11);
    MD5_STEP(md5_F,a,b,c,d,12, 7,12); MD5_STEP(md5_F,d,a,b,c,13,12,13);
    MD5_STEP(md5_F,c,d,a,b,14,17,14); MD5_STEP(md5_F,b,c,d,a,15,22,15);
    MD5_STEP(md5_G,a,b,c,d, 1, 5,16); MD5_STEP(md5_G,d,a,b,c, 6, 9,17);
    MD5_STEP(md5_G,c,d,a,b,11,14,18); MD5_STEP(md5_G,b,c,d,a, 0,20,19);
    MD5_STEP(md5_G,a,b,c,d, 5, 5,20); MD5_STEP(md5_G,d,a,b,c,10, 9,21);
    MD5_STEP(md5_G,c,d,a,b,15,14,22); MD5_STEP(md5_G,b,c,d,a, 4,20,23);
    MD5_STEP(md5_G,a,b,c,d, 9, 5,24); MD5_STEP(md5_G,d,a,b,c,14, 9,25);
    MD5_STEP(md5_G,c,d,a,b, 3,14,26); MD5_STEP(md5_G,b,c,d,a, 8,20,27);
    MD5_STEP(md5_G,a,b,c,d,13, 5,28); MD5_STEP(md5_G,d,a,b,c, 2, 9,29);
    MD5_STEP(md5_G,c,d,a,b, 7,14,30); MD5_STEP(md5_G,b,c,d,a,12,20,31);
    MD5_STEP(md5_H,a,b,c,d, 5, 4,32); MD5_STEP(md5_H,d,a,b,c, 8,11,33);
    MD5_STEP(md5_H,c,d,a,b,11,16,34); MD5_STEP(md5_H,b,c,d,a,14,23,35);
    MD5_STEP(md5_H,a,b,c,d, 1, 4,36); MD5_STEP(md5_H,d,a,b,c, 4,11,37);
    MD5_STEP(md5_H,c,d,a,b, 7,16,38); MD5_STEP(md5_H,b,c,d,a,10,23,39);
    MD5_STEP(md5_H,a,b,c,d,13, 4,40); MD5_STEP(md5_H,d,a,b,c, 0,11,41);
    MD5_STEP(md5_H,c,d,a,b, 3,16,42); MD5_STEP(md5_H,b,c,d,a, 6,23,43);
    MD5_STEP(md5_H,a,b,c,d, 9, 4,44); MD5_STEP(md5_H,d,a,b,c,12,11,45);
    MD5_STEP(md5_H,c,d,a,b,15,16,46); MD5_STEP(md5_H,b,c,d,a, 2,23,47);
    MD5_STEP(md5_I,a,b,c,d, 0, 6,48); MD5_STEP(md5_I,d,a,b,c, 7,10,49);
    MD5_STEP(md5_I,c,d,a,b,14,15,50); MD5_STEP(md5_I,b,c,d,a, 5,21,51);
    MD5_STEP(md5_I,a,b,c,d,12, 6,52); MD5_STEP(md5_I,d,a,b,c, 3,10,53);
    MD5_STEP(md5_I,c,d,a,b,10,15,54); MD5_STEP(md5_I,b,c,d,a, 1,21,55);
    MD5_STEP(md5_I,a,b,c,d, 8, 6,56); MD5_STEP(md5_I,d,a,b,c,15,10,57);
    MD5_STEP(md5_I,c,d,a,b, 6,15,58); MD5_STEP(md5_I,b,c,d,a,13,21,59);
    MD5_STEP(md5_I,a,b,c,d, 4, 6,60); MD5_STEP(md5_I,d,a,b,c,11,10,61);
    MD5_STEP(md5_I,c,d,a,b, 2,15,62); MD5_STEP(md5_I,b,c,d,a, 9,21,63);
    #undef MD5_STEP
    s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d;
}

static void md5_init(MD5_CTX *c){
    c->state[0]=0x67452301; c->state[1]=0xefcdab89;
    c->state[2]=0x98badcfe; c->state[3]=0x10325476;
    c->count[0]=c->count[1]=0;
}
static void md5_update(MD5_CTX *c, const uint8_t *in, size_t len){
    uint32_t idx=(c->count[0]>>3)&0x3f;
    c->count[0]+=(uint32_t)(len<<3); if(c->count[0]<(uint32_t)(len<<3)) c->count[1]++;
    c->count[1]+=(uint32_t)(len>>29);
    uint32_t part=64-idx;
    size_t i=0;
    if(len>=part){ memcpy(c->buf+idx,in,part); md5_transform(c->state,c->buf); for(i=part;i+63<len;i+=64) md5_transform(c->state,in+i); idx=0; }
    memcpy(c->buf+idx,in+i,len-i);
}
static void md5_final(uint8_t digest[16], MD5_CTX *c){
    static const uint8_t pad[64]={0x80};
    uint8_t bits[8];
    for(int i=0;i<8;i++) bits[i]=(i<4)?(c->count[0]>>(i*8)):(c->count[1]>>((i-4)*8));
    uint32_t idx=(c->count[0]>>3)&0x3f;
    uint32_t padlen=(idx<56)?56-idx:120-idx;
    md5_update(c,pad,padlen); md5_update(c,bits,8);
    for(int i=0;i<4;i++){ digest[i*4]=(uint8_t)c->state[i]; digest[i*4+1]=(uint8_t)(c->state[i]>>8); digest[i*4+2]=(uint8_t)(c->state[i]>>16); digest[i*4+3]=(uint8_t)(c->state[i]>>24); }
}

/* Convenience: hash one buffer */
static inline void md5(const uint8_t *in, size_t len, uint8_t out[16]){
    MD5_CTX c; md5_init(&c); md5_update(&c,in,len); md5_final(out,&c);
}

#endif /* MD5_H */
