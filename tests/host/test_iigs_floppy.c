/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_iigs_content.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t original[MT_IIGS_FLOPPY_RAW_TRACK_BYTES],decoded[MT_IIGS_FLOPPY_RAW_TRACK_BYTES];
static uint8_t bits[MT_IIGS_FLOPPY_TRACK_BYTES],rotated[MT_IIGS_FLOPPY_TRACK_BYTES];
static uint8_t header[MT_IIGS_FLOPPY_HEADER_BYTES];
static void rotate(unsigned count,unsigned distance)
{
    memset(rotated,0,sizeof(rotated));
    for(unsigned i=0U;i<count;++i) {
        const unsigned from=(i+distance)%count;
        if((bits[from/8U]&(0x80U>>(from%8U)))!=0U)rotated[i/8U]|=(uint8_t)(0x80U>>(i%8U));
    }
}
static unsigned read_byte(const uint8_t *p,unsigned start)
{
    unsigned result=0U;
    for(unsigned i=0U;i<8U;++i)result=(result<<1U)|((p[(start+i)/8U]>>(7U-((start+i)%8U)))&1U);
    return result;
}
static void check_track(uint8_t format,unsigned track)
{
    mt_iigs_floppy_track_t t;
    assert(mt_iigs_floppy_track(format,track,&t));
    assert(t.block_count*512U<=sizeof(bits) && t.raw_bytes<=sizeof(original));
    assert(mt_iigs_floppy_encode(format,track,original,bits));
    memset(decoded,0xcc,sizeof(decoded));
    assert(mt_iigs_floppy_decode(format,track,bits,decoded));
    assert(memcmp(original,decoded,t.raw_bytes)==0);
    /* Arbitrary bit alignment and a sector crossing the circular seam. */
    const unsigned shifts[]={1U,7U,487U,t.bit_count-137U};
    for(unsigned i=0U;i<sizeof(shifts)/sizeof(shifts[0]);++i) {
        rotate(t.bit_count,shifts[i]);
        assert(mt_iigs_floppy_decode(format,track,rotated,decoded));
        assert(memcmp(original,decoded,t.raw_bytes)==0);
    }
    assert(!mt_iigs_floppy_decode(format,track==0U ? 1U:0U,bits,decoded));
    /* Destroy a data checksum, leaving all other sectors untouched. */
    const unsigned address_end=format==MT_IIGS_RAW_DOS525 ? 480U+14U*8U+60U:1480U+11U*8U+50U;
    const unsigned checksum=address_end+(format==MT_IIGS_RAW_DOS525 ? (3U+342U)*8U:(4U+699U)*8U);
    for(unsigned i=0U;i<8U;++i)bits[(checksum+i)/8U]&=(uint8_t)~(0x80U>>((checksum+i)%8U));
    assert(!mt_iigs_floppy_decode(format,track,bits,decoded));
}
static void unit_tests(void)
{
    mt_iigs_floppy_track_t t;
    assert(!mt_iigs_floppy_track(MT_IIGS_RAW_NONE,0U,&t));
    assert(!mt_iigs_floppy_track(MT_IIGS_RAW_DOS525,35U,&t));
    assert(!mt_iigs_floppy_track(MT_IIGS_RAW_PRODOS35,160U,&t));
    assert(mt_iigs_floppy_woz_size(MT_IIGS_RAW_NONE)==0U);
    for(uint8_t format=MT_IIGS_RAW_DOS525;format<=MT_IIGS_RAW_PRODOS35;++format) {
        assert(mt_iigs_floppy_header(format,false,header));
        assert(memcmp(header,"WOZ2\xff\x0a\x0d\x0a",8U)==0 && header[22]==1U);
        assert(header[59]==(format==MT_IIGS_RAW_DOS525 ? 32U:16U));
        const unsigned tracks=format==MT_IIGS_RAW_DOS525 ? 35U:160U;
        unsigned end=0U;
        for(unsigned track=0U;track<tracks;++track) {
            assert(mt_iigs_floppy_track(format,track,&t));
            assert(t.raw_offset==end); end+=t.raw_bytes;
            for(unsigned i=0U;i<t.raw_bytes;++i)original[i]=(uint8_t)(i*19U+(i>>7U)*track+track*37U);
            check_track(format,track);
        }
        assert(end==(format==MT_IIGS_RAW_DOS525 ? 143360U:819200U));
    }
    /* Independent known framing and DOS sector permutation. GCR address
     * sector1 must encode file sector7, with a genuine 10-bit sync gap. */
    for(unsigned i=0U;i<4096U;++i)original[i]=(uint8_t)(i/256U);
    assert(mt_iigs_floppy_encode(MT_IIGS_RAW_DOS525,0U,original,bits));
    assert(read_byte(bits,480U)==0xd5U && read_byte(bits,480U+8U)==0xaaU);
    assert(read_byte(bits,480U+14U*8U)==0xffU);
    puts("195 raw floppy tracks, five zones, circular bit alignment and corrupt fields passed");
}
static void image_test(unsigned drive,const char *filename)
{
    FILE *f=fopen(filename,"rb"); assert(f!=NULL);
    assert(fseek(f,0L,SEEK_END)==0); const long size=ftell(f); assert(size>0L);
    assert(fseek(f,0L,SEEK_SET)==0); assert(fread(header,1U,512U,f)==512U);
    mt_iigs_media_info_t info;
    assert(mt_iigs_media_inspect(drive,filename,header,512U,(uint64_t)size,&info)==MT_CONTENT_VALID);
    assert(info.raw_format!=MT_IIGS_RAW_NONE);
    const unsigned tracks=info.raw_format==MT_IIGS_RAW_DOS525 ? 35U:160U;
    for(unsigned track=0U;track<tracks;++track) {
        mt_iigs_floppy_track_t t; assert(mt_iigs_floppy_track(info.raw_format,track,&t));
        assert(fseek(f,(long)(info.offset+t.raw_offset),SEEK_SET)==0);
        assert(fread(original,1U,t.raw_bytes,f)==t.raw_bytes);
        assert(mt_iigs_floppy_encode(info.raw_format,track,original,bits));
        assert(mt_iigs_floppy_decode(info.raw_format,track,bits,decoded));
        assert(memcmp(original,decoded,t.raw_bytes)==0);
    }
    assert(fclose(f)==0); printf("Read-only whole-image %u-track roundtrip passed\n",tracks);
}
int main(int argc,char **argv)
{
    if(argc==3)image_test((unsigned)strtoul(argv[1],NULL,10),argv[2]);
    else { assert(argc==1); unit_tests(); }
    return 0;
}
