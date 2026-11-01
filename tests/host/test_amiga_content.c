/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_content.h"
#include "multitini_core_frontend.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    mt_amiga_config_t config,decoded;uint8_t wire[16];
    mt_amiga_config_defaults(&config);assert(config.cpu==MT_AMIGA_CPU_68020_TURBO && config.fast_mib==8U && config.ide_compat==MT_AMIGA_IDE_AUTO);
    mt_amiga_config_encode(&config,wire);assert(mt_amiga_config_decode(wire,&decoded));
    const uint8_t legacy_auto[16]={'A','M','0','1',2U,0U,0U,0U,8U,2U,0U,0U,0U,0U,0U,0U};
    assert(memcmp(wire,legacy_auto,sizeof(wire))==0);
    assert(!mt_amiga_fast_ide_enabled(&config,0U) && mt_amiga_fast_ide_enabled(&config,MT_AMIGA_HOST_FAST_IDE));
    config.ide_compat=MT_AMIGA_IDE_CLASSIC;mt_amiga_config_encode(&config,wire);
    assert(wire[10]==1U && mt_amiga_config_decode(wire,&decoded) && decoded.ide_compat==1U);
    assert(!mt_amiga_fast_ide_enabled(&config,MT_AMIGA_HOST_FAST_IDE));
    config.ide_compat=MT_AMIGA_IDE_AUTO;config.cpu=MT_AMIGA_CPU_STOCK;
    assert(!mt_amiga_fast_ide_enabled(&config,MT_AMIGA_HOST_FAST_IDE));
    config.cpu=MT_AMIGA_CPU_68020_14;mt_amiga_config_encode(&config,wire);
    assert(mt_amiga_config_decode(wire,&decoded) && decoded.cpu==3U);
    assert(mt_amiga_fast_ide_enabled(&config,MT_AMIGA_HOST_FAST_IDE));
    wire[10]=2U;assert(!mt_amiga_config_decode(wire,&decoded));wire[10]=0U;
    wire[11]=1U;assert(!mt_amiga_config_decode(wire,&decoded));wire[11]=0U;
    wire[4]=1U;assert(!mt_amiga_config_decode(wire,&decoded));
    assert(mt_amiga_media_path_valid("Workbench/Disk 1.adf"));
    assert(!mt_amiga_media_path_valid("../disk.adf"));assert(!mt_amiga_media_path_valid("foo/../disk.adf"));
    assert(!mt_amiga_media_path_valid("/disk.adf"));assert(!mt_amiga_media_path_valid("foo//disk.adf"));
    assert(!mt_amiga_media_path_valid("0:disk.adf"));assert(!mt_amiga_media_path_valid("disk.adf."));
    assert(mt_amiga_media_validate(0U,901120U)==MT_CONTENT_VALID);
    assert(mt_amiga_media_validate(1U,1802240U)!=MT_CONTENT_VALID);
    assert(mt_amiga_media_validate(2U,UINT64_C(0x100000000))==MT_CONTENT_VALID);
    assert(mt_amiga_media_validate(2U,MT_AMIGA_MAX_HDF_BYTES+512U)!=MT_CONTENT_VALID);
    assert(mt_content_classify("Workbench.ADF")==MT_CONTENT_AMIGA_ADF);
    assert(mt_content_classify("System.hdf")==MT_CONTENT_AMIGA_HDF);
    assert(mt_core_frontend_accepts_drive_content(MT_AMIGA_CORE_ID,MT_MENU_BROWSER_DRIVE8,1U,MT_CONTENT_AMIGA_ADF));
    assert(!mt_core_frontend_accepts_drive_content(MT_AMIGA_CORE_ID,MT_MENU_BROWSER_DRIVE8,2U,MT_CONTENT_AMIGA_ADF));
    uint8_t input[512],output[512];uint16_t words[544],previous=0U;
    for(unsigned i=0U;i<512U;++i)input[i]=(uint8_t)(i^(i>>3U));
    for(unsigned t=0U;t<160U;++t)for(unsigned s=0U;s<11U;++s) {
        unsigned got=99U;assert(mt_amiga_mfm_encode(t,s,0x4489U,input,words,&previous));
        assert(words[2]==0x4489U && words[3]==0x4489U);
        assert(mt_amiga_mfm_decode(words,t,&got,output));assert(got==s && memcmp(input,output,512U)==0);
    }
    unsigned got=0U;
    words[45]^=1U;assert(!mt_amiga_mfm_decode(words,159U,&got,output));words[45]^=1U;
    words[4]^=1U;assert(!mt_amiga_mfm_decode(words,159U,&got,output));words[4]^=1U;
    /* Clock bits never alter the data checksum. */
    words[45]^=0xaaaaU;assert(mt_amiga_mfm_decode(words,159U,&got,output));
    assert(!mt_amiga_mfm_decode(words,158U,&got,output));
    return 0;
}
