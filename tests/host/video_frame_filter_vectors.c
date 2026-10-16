/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent HDL oracle: execute the production portable CPU frame kernel. */
#include "multitini_video_frame.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t random_state = UINT32_C(0x182749ab);
static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static uint32_t rgb888(uint16_t pixel)
{
    uint32_t r=pixel>>11,g=(pixel>>5)&63U,b=pixel&31U;
    return ((r<<3)|(r>>2)) | (((g<<2)|(g>>4))<<8) | (((b<<3)|(b>>2))<<16);
}
static FILE *open_output(const char *directory,const char *name)
{
    char path[1024];
    int length=snprintf(path,sizeof(path),"%s/%s",directory,name);
    if(length<0 || (size_t)length>=sizeof(path))return NULL;
    return fopen(path,"w");
}
int main(int argc,char **argv)
{
    if(argc!=5 && argc!=7)return 2;
    uint32_t width=(uint32_t)strtoul(argv[1],NULL,10);
    uint32_t height=(uint32_t)strtoul(argv[2],NULL,10);
    unsigned exhaustive=(unsigned)strtoul(argv[4],NULL,10);
    uint32_t scale_num=argc==7 ? (uint32_t)strtoul(argv[5],NULL,10) : 3U;
    uint32_t scale_den=argc==7 ? (uint32_t)strtoul(argv[6],NULL,10) : 2U;
    if(!mt_video_frame_geometry_valid(width,height) || exhaustive>1)return 2;
    if(scale_num==0 || scale_num>64 || scale_den==0 || scale_den>64)return 2;
    uint32_t count=width*height;
    uint16_t *input=calloc(count,sizeof(*input)),*output=calloc(count,sizeof(*output));
    uint32_t *history=calloc(count,sizeof(*history)),*base=calloc(count,sizeof(*base));
    uint32_t *scratch=calloc(count,sizeof(*scratch)),*persisted=calloc(count,sizeof(*persisted));
    FILE *inputs=open_output(argv[3],"input.hex"),*expected=open_output(argv[3],"expected.hex");
    FILE *settings=open_output(argv[3],"options.hex");
    if(!input || !output || !history || !base || !scratch || !persisted || !inputs || !expected || !settings)return 3;
    unsigned jobs=exhaustive ? 448U : 1U;
    for(unsigned job=0;job<jobs;++job){
        unsigned pattern=exhaustive ? job/64U : 0U;
        unsigned softness=exhaustive ? (job/16U)%4U : 3U;
        unsigned blur=exhaustive ? (job/4U)%4U : 3U;
        unsigned bloom=exhaustive ? job%4U : 3U;
        mt_video_frame_options_t options={1U,0U,(uint8_t)blur,(uint8_t)bloom,(uint8_t)softness};
        for(uint32_t i=0;i<count;++i){
            uint32_t x=i%width,y=i/width;
            uint16_t pixel=(uint16_t)next_random();
            if(pattern==1)pixel=x==width/2U && y==height/2U ? UINT16_C(0xffff) : 0U;
            if(pattern==2)pixel=(x==0 || y==0) ? UINT16_C(0xf81f) :
                (x==width-1U || y==height-1U) ? UINT16_C(0x07e0) : (uint16_t)(i*337U);
            if(pattern==3)pixel=UINT16_C(0xffff);
            /* The two stripe axes separately expose accidental horizontal
             * smoothing and confirm vertical response at every level. */
            if(pattern==5)pixel=(x&1U)!=0U ? UINT16_C(0xffff) : 0U;
            if(pattern==6)pixel=(y&1U)!=0U ? UINT16_C(0xffff) : 0U;
            input[i]=pixel;
            persisted[i]=rgb888(pixel);
            history[i]=0U;
            if(pattern==4){
                /* Decay zero and black current expose arbitrary RGB888
                 * history unchanged, including values not present in 565. */
                input[i]=0U;
                persisted[i]=next_random()&UINT32_C(0xffffff);
                history[i]=persisted[i];
                options.persistence=1U;
            }
            fprintf(inputs,"%06" PRIx32 "\n",persisted[i]);
        }
        if(!mt_video_frame_process_scaled(input,output,history,base,scratch,width,height,
                                          scale_num,scale_den,&options,0U,pattern!=4))return 4;
        for(uint32_t i=0;i<count;++i)fprintf(expected,"%04x\n",(unsigned)output[i]);
        /* Filter must ignore the persistence bits owned by its DMA caller. */
        fprintf(settings,"%02x\n",(softness<<6)|(bloom<<4)|(blur<<2)|(job&3U));
    }
    if(fclose(inputs) || fclose(expected) || fclose(settings))return 5;
    free(input);free(output);free(history);free(base);free(scratch);free(persisted);
    printf("Generated %u CPU-oracle jobs at %" PRIu32 "x%" PRIu32 "\n",jobs,width,height);
    return 0;
}
