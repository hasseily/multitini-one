/* SPDX-License-Identifier: GPL-3.0-only */
/* Full persistence/spatial oracle; HDL does not duplicate the reference math. */
#include "multitini_video_frame.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t random_state=UINT32_C(0xb17ea302);
static uint32_t random_next(void)
{
    random_state^=random_state<<13; random_state^=random_state>>17;
    random_state^=random_state<<5; return random_state;
}
static FILE *open_output(const char *directory,const char *name)
{
    char path[1024];
    int length=snprintf(path,sizeof(path),"%s/%s",directory,name);
    return length<0 || (size_t)length>=sizeof(path)?NULL:fopen(path,"w");
}
int main(int argc,char **argv)
{
    if(argc!=5 && argc!=7)return 2;
    uint32_t width=(uint32_t)strtoul(argv[1],NULL,10),height=(uint32_t)strtoul(argv[2],NULL,10);
    unsigned exhaustive=(unsigned)strtoul(argv[4],NULL,10);
    uint32_t scale_num=argc==7 ? (uint32_t)strtoul(argv[5],NULL,10) : 3U;
    uint32_t scale_den=argc==7 ? (uint32_t)strtoul(argv[6],NULL,10) : 2U;
    if(!width || width>640 || !height || height>720 || exhaustive>1)return 2;
    if(scale_num==0 || scale_num>64 || scale_den==0 || scale_den>64)return 2;
    uint32_t count=width*height;
    uint16_t *input=calloc(count,sizeof(*input)),*output=calloc(count,sizeof(*output));
    uint32_t *history=calloc(count,sizeof(*history)),*base=calloc(count,sizeof(*base));
    uint32_t *scratch=calloc(count,sizeof(*scratch));
    FILE *inputs=open_output(argv[3],"input.hex"),*old=open_output(argv[3],"old.hex");
    FILE *expected=open_output(argv[3],"expected.hex"),*persisted=open_output(argv[3],"history.hex");
    FILE *config=open_output(argv[3],"options.hex");
    if(!input || !output || !history || !base || !scratch || !inputs || !old || !expected || !persisted || !config)return 3;
    unsigned jobs=exhaustive?68U:1U;
    for(unsigned job=0;job<jobs;++job){
        unsigned bits=exhaustive?(job<64U?job:(60U|(job&3U))):63U;
        bool reset=job>=64U;
        mt_video_frame_options_t options={1U,(uint8_t)(bits&3U),(uint8_t)((bits>>2)&3U),(uint8_t)(bits>>4)};
        for(uint32_t i=0;i<count;++i){
            /* Black/current impulse plus arbitrary history exposes decay,
             * extinction and all three threshold intervals. */
            input[i]=(i%5U==0U)?0U:(uint16_t)random_next();
            history[i]=random_next()&UINT32_C(0xffffff);
            if(i%8U==0U)history[i]=UINT32_C(0x7f201f);
            if(i%8U==1U)history[i]=UINT32_C(0x040302);
            if(i%8U==2U)history[i]=UINT32_C(0x1f1e10);
            if(i%8U==3U)history[i]=UINT32_C(0x800080);
            fprintf(inputs,"%04x\n",(unsigned)input[i]);
            fprintf(old,"%08" PRIx32 "\n",history[i]);
        }
        if(!mt_video_frame_process_scaled(input,output,history,base,scratch,width,height,
                                          scale_num,scale_den,&options,1U,reset))return 4;
        for(uint32_t i=0;i<count;++i){
            fprintf(expected,"%04x\n",(unsigned)output[i]);
            fprintf(persisted,"%08" PRIx32 "\n",history[i]);
        }
        fprintf(config,"%02x\n",bits|(reset?64U:0U));
    }
    if(fclose(inputs) || fclose(old) || fclose(expected) || fclose(persisted) || fclose(config))return 5;
    free(input);free(output);free(history);free(base);free(scratch);
    printf("Generated %u full-frame CPU-oracle jobs at %" PRIu32 "x%" PRIu32 "\n",jobs,width,height);
    return 0;
}
