/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the real service's storage/native-port fixture, not a profiler model. */
#define main amiga_service_existing_regressions
#define mt_sd_file_read fixture_file_read
#include "test_amiga_service.c"
#undef mt_sd_file_read
#undef main

static uint64_t ticks,sd_cost=100U;
static unsigned timer_calls,port_reads,port_writes,short_read;
static bool diag_capable,diag_running,diag_bad_ack;
static uint16_t diag_words[MT_AMIGA_DIAG_WORDS],diag_status=MT_AMIGA_DIAG_FROZEN|MT_AMIGA_DIAG_VALID;
static unsigned diag_arms,diag_stops,diag_reads;
static uint64_t timer(void *context)
{ assert(context==&ticks);++timer_calls;return ticks; }
static uint32_t profile_rd(void *context,uint32_t address)
{ ++port_reads;++ticks;return rd(context,address)|(address==MT_AMIGA_HOST_PORT && diag_capable ? MT_AMIGA_HOST_DIAGNOSTICS:0U); }
static void profile_wr(void *context,uint32_t address,uint32_t value)
{
    ++port_writes;ticks+=2U;
    const unsigned op=value>>24U;
    if(address==MT_AMIGA_HOST_PORT && (op==MT_AMIGA_HOST_DIAG_CONTROL || op==MT_AMIGA_HOST_DIAG_READ)) {
        assert(diag_capable);ticks+=1000U;uint16_t answer=0U;
        if(op==MT_AMIGA_HOST_DIAG_CONTROL) {
            answer=(uint16_t)(value>>8U);
            if((uint8_t)value==MT_AMIGA_DIAG_ARM) {
                assert(!diag_running);diag_running=true;++diag_arms;diag_words[3]=answer;
            } else { assert(diag_running && (uint8_t)value==MT_AMIGA_DIAG_STOP);diag_running=false;++diag_stops;diag_words[4]=diag_status; }
            if(diag_bad_ack)++answer;
        } else { assert(!diag_running && (value&0xffffffU)<MT_AMIGA_DIAG_WORDS);answer=diag_words[value&0xffffffU];++diag_reads; }
        native_status=((native_status^MT_AMIGA_HOST_TOGGLE)&MT_AMIGA_HOST_TOGGLE)|answer;
    } else wr(context,address,value);
}
static void diagnostic_bank(void)
{
    memset(diag_words,0,sizeof(diag_words));diag_running=false;diag_bad_ack=false;
    diag_status=MT_AMIGA_DIAG_FROZEN|MT_AMIGA_DIAG_VALID;
    diag_words[0]=0x4447U;diag_words[1]=1U;diag_words[2]=MT_AMIGA_DIAG_WORDS;
    diag_words[5]=diag_words[6]=0x1663U;diag_words[8]=(uint16_t)28375196U;diag_words[9]=(uint16_t)(28375196U>>16U);
    diag_words[10]=7U;diag_words[12]=diag_words[13]=1U;diag_words[14]=MT_AMIGA_DIAG_COUNTERS;
}
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *file,void *data,size_t capacity)
{
    ticks+=sd_cost;
    if(short_read!=0U) {
        assert(capacity==512U);memcpy(data,file->file->data+file->pos,128U);file->pos+=128U;
        return result(short_read==1U ? MT_SD_READ_FAILED:MT_SD_OK,128U);
    }
    return fixture_file_read(file,data,capacity);
}
static mt_amiga_ide_profile_t snapshot(void)
{ mt_amiga_ide_profile_t value;amiga_content_service_profile_snapshot(&value);return value; }
static void retry(void)
{ short_read=0U;ide_request=0U;assert(amiga_content_service_retry()==0); }
int main(void)
{
    mt_amiga_ide_profile_t p=snapshot();assert(!p.running && !p.valid && timer_calls==0U);
    assert(amiga_content_service_profile_start(timer,&ticks)!=0 && timer_calls==0U);
    (void)create("0:/multitini/cores/amiga/roms/kick.rom",524288U);
    file_t *disk=create(ROOT "/Profile.hdf",8192U);uint8_t original[8192];memcpy(original,disk->data,sizeof(original));
    mt_core_link_t link;mt_core_link_init(&link,NULL,profile_rd,profile_wr);
    amiga_content_service_init(&link,ROOT,false,progress,NULL);assert(amiga_content_service_boot()==0);
    assert(amiga_content_service_mount(2U,"Profile.hdf")==0);bulk_capable=true;
    amiga_content_service_poll();assert(timer_calls==0U); /* Disabled hot path has no clock calls. */
    const unsigned reads=port_reads,writes=port_writes,roms=rom_streams;
    const uint32_t sequence=regs[MT_REG_COMMAND_SEQ/4U];
    assert(amiga_content_service_profile_start(NULL,NULL)!=0);
    assert(amiga_content_service_profile_start(timer,&ticks)==0);
    assert(!snapshot().native_available);
    assert(amiga_content_service_profile_start(timer,&ticks)!=0);
    assert(port_reads==reads && port_writes==writes && regs[MT_REG_COMMAND_SEQ/4U]==sequence);
    ticks+=50U;command(0x20U,0U,0U,3U);amiga_content_service_poll();
    ticks+=200U;ide_request=5U;amiga_content_service_poll();
    ticks+=30U;command(0x20U,0U,6U,1U);amiga_content_service_poll();
    p=snapshot();assert(p.running && p.valid && p.polls==3U && p.req4==2U && p.req5==1U);
    assert(p.read_commands==2U && p.requested_read_sectors==4U && p.read_command_hist[2]==1U && p.read_command_hist[0]==1U);
    /* First command fetches all three explicitly requested sectors, although
     * a new command replaces it after two are served. Backend bytes/calls
     * describe fills; logical sectors and published bytes describe service. */
    assert(p.sd_read_calls==2U && p.read_sectors==3U && p.read_bytes==2048U && p.sd_read_ticks==200U && p.sd_read_max_ticks==100U);
    assert(p.fifo_calls==3U && p.fifo_bytes==1536U && p.fifo_ticks>0U && p.ata_other_ticks>0U && p.fdd_ticks>0U);
    assert(p.sequential_reads==1U && p.nonsequential_reads==2U && p.drq_intervals==2U && p.max_drq_to_request_ticks>=200U);
    assert(p.poll_bracketed_intervals==0U && p.poll_unbracketed_intervals==2U && p.poll_tail_upper_ticks==0U);
    assert(p.read_window_started && p.first_read_ticks>=50U && p.last_read_ticks>p.first_read_ticks);
    assert(p.active_read_ticks==p.last_read_ticks-p.first_read_ticks && p.read_window_bytes==1536U);
    assert(p.active_read_ticks<p.elapsed_ticks && p.write_commands==0U);
    assert(p.service_ticks==p.sd_read_ticks+p.fifo_ticks+p.ata_other_ticks+p.fdd_ticks);
    assert(p.elapsed_ticks==p.service_ticks+p.outside_ticks && p.max_gap_ticks==200U);
    assert(rom_streams==roms && memcmp(original,disk->data,sizeof(original))==0);
    assert(strcmp(amiga_content_service_name(2U),"Profile.hdf")==0);
    ticks+=25U;amiga_content_service_profile_stop();p=snapshot();
    assert(!p.running && p.valid && p.reason==MT_AMIGA_PROFILE_STOPPED);
    const unsigned stopped_calls=timer_calls,stopped_reads=port_reads,stopped_writes=port_writes;
    ticks+=100U;amiga_content_service_profile_stop();mt_amiga_ide_profile_t again=snapshot();
    assert(memcmp(&p,&again,sizeof(p))==0 && timer_calls==stopped_calls && port_reads==stopped_reads && port_writes==stopped_writes);
    /* A zero/positive observation supplies an upper bound, not a timestamp
     * of native readiness. Repeated zeros narrow the bound to the last poll;
     * the existing three idle-poll timer samples suffice (no added clocks). */
    ticks=UINT64_MAX-2000U;
    assert(amiga_content_service_profile_start(timer,&ticks)==0);
    command(0x20U,0U,0U,2U);amiga_content_service_poll();
    ticks+=1500U;ide_request=0U;
    unsigned calls_before=timer_calls;amiga_content_service_poll();assert(timer_calls-calls_before==3U);
    ticks+=1000U;const uint64_t last_zero_before=ticks;
    calls_before=timer_calls;amiga_content_service_poll();assert(timer_calls-calls_before==3U);
    ticks+=40U;ide_request=5U;amiga_content_service_poll();p=snapshot();
    assert(p.running && p.valid && p.drq_intervals==1U);
    assert(p.poll_bracketed_intervals==1U && p.poll_unbracketed_intervals==0U);
    assert(p.poll_tail_upper_ticks>=40U && p.poll_tail_upper_ticks<=ticks-last_zero_before);
    assert(p.poll_tail_upper_ticks==p.max_poll_tail_upper_ticks && p.poll_tail_upper_ticks<p.drq_to_request_ticks);
    amiga_content_service_profile_stop();
    /* A prior run's last-zero bracket must never leak into a new capture. */
    assert(amiga_content_service_profile_start(timer,&ticks)==0);
    command(0x20U,0U,0U,2U);amiga_content_service_poll();
    ide_request=5U;amiga_content_service_poll();p=snapshot();
    assert(p.poll_bracketed_intervals==0U && p.poll_unbracketed_intervals==1U && p.poll_tail_upper_ticks==0U);
    amiga_content_service_profile_stop();
    /* New cores arm before ARM timing, then freeze before the native bank
     * transport. The exaggerated diagnostic I/O cost must not enter wall
     * timing; media/FIFO/core commands remain untouched. */
    diag_capable=true;diagnostic_bank();assert(amiga_content_service_boot()==0);
    const unsigned blocks_before=bulk_commands,rom_before=rom_streams;
    const uint32_t command_before=regs[MT_REG_COMMAND_SEQ/4U];
    assert(amiga_content_service_profile_start(timer,&ticks)==0);
    const uint64_t native_started=ticks;assert(diag_running && diag_arms==1U);
    amiga_content_service_poll();assert(diag_running && diag_arms==1U && diag_stops==0U && diag_reads==0U);
    ticks+=100U;const uint64_t native_stopped=ticks;
    amiga_content_service_profile_stop();p=snapshot();
    assert(p.valid && !p.running && p.native_available && p.native_valid && p.native_result==MT_LINK_OK);
    assert(p.elapsed_ticks==native_stopped-native_started && ticks>native_stopped+100000U);
    assert(p.native.sequence!=0U && p.native.epoch==7U && p.native.service_hz==28375196U && diag_reads==133U);
    assert(diag_stops==1U && bulk_commands==blocks_before && rom_streams==rom_before && regs[MT_REG_COMMAND_SEQ/4U]==command_before);
    assert(memcmp(original,disk->data,sizeof(original))==0);
    unsigned reads_after=port_reads,writes_after=port_writes,clocks_after=timer_calls;
    amiga_content_service_profile_stop();again=snapshot();
    assert(memcmp(&p,&again,sizeof(p))==0 && port_reads==reads_after && port_writes==writes_after && timer_calls==clocks_after);
    /* A native reset reason is retained separately from an otherwise valid
     * ARM capture; a subsequent fresh capture must not reuse its bank. */
    diag_status=MT_AMIGA_DIAG_FROZEN|MT_AMIGA_DIAG_ABORT_RESET;
    const uint16_t first_sequence=p.native.sequence;
    assert(amiga_content_service_profile_start(timer,&ticks)==0);amiga_content_service_profile_stop();p=snapshot();
    assert(p.valid && !p.native_valid && p.native_result==MT_LINK_OK && p.native.status==diag_status);
    assert(p.native.sequence!=first_sequence && p.native.sequence!=0U);
    /* Bad/torn schema is not plausible native time, and failure does not
     * fault storage or automatically replay a control transaction. */
    diag_words[0]=0U;
    assert(amiga_content_service_profile_start(timer,&ticks)==0);amiga_content_service_profile_stop();p=snapshot();
    assert(p.valid && !p.native_valid && p.native_result==MT_LINK_ABI_MISMATCH && p.native.sequence==0U);
    assert(!amiga_content_service_faulted());reads_after=port_reads;writes_after=port_writes;
    assert(amiga_content_service_profile_start(timer,&ticks)!=0 && port_reads==reads_after && port_writes==writes_after);
    diagnostic_bank();assert(amiga_content_service_boot()==0);diag_bad_ack=true;
    assert(amiga_content_service_profile_start(timer,&ticks)!=0 && !snapshot().running && !amiga_content_service_faulted());
    reads_after=port_reads;writes_after=port_writes;
    assert(amiga_content_service_profile_start(timer,&ticks)!=0 && port_reads==reads_after && port_writes==writes_after);
    /* Normal guest lifecycle clears optional diagnostic failure. A running
     * native capture is stopped before core reset and the ARM run invalidated. */
    diagnostic_bank();assert(amiga_content_service_boot()==0);
    assert(amiga_content_service_profile_start(timer,&ticks)==0);
    const unsigned stops_before=diag_stops;assert(amiga_content_service_boot()==0);p=snapshot();
    assert(!p.valid && !p.running && p.reason==MT_AMIGA_PROFILE_LIFECYCLE && diag_stops==stops_before+1U);
    diag_capable=false;assert(amiga_content_service_boot()==0);
    /* Unsigned absolute timer wrap is defined, while aggregate overflow must
     * invalidate instead of reporting plausible wrapped timing totals. */
    ticks=UINT64_MAX-10U;assert(amiga_content_service_profile_start(timer,&ticks)==0);ticks+=20U;
    amiga_content_service_profile_stop();p=snapshot();assert(p.valid && p.elapsed_ticks==20U && p.outside_ticks==20U);
    assert(amiga_content_service_profile_start(timer,&ticks)==0);sd_cost=UINT64_MAX/2U+1U;
    command(0x20U,0U,0U,1U);amiga_content_service_poll();assert(snapshot().running);
    command(0x20U,0U,1U,1U);amiga_content_service_poll();p=snapshot();
    assert(!p.running && !p.valid && p.reason==MT_AMIGA_PROFILE_OVERFLOW && !amiga_content_service_faulted());sd_cost=100U;
    /* A real service lifecycle invalidates the run but preserves mounted media. */
    assert(amiga_content_service_profile_start(timer,&ticks)==0);assert(amiga_content_service_boot()==0);
    p=snapshot();assert(!p.running && !p.valid && p.reason==MT_AMIGA_PROFILE_LIFECYCLE);
    assert(strcmp(amiga_content_service_name(2U),"Profile.hdf")==0);
    assert(amiga_content_service_profile_start(timer,&ticks)==0);ide_request=6U;amiga_content_service_poll();
    p=snapshot();assert(!p.running && !p.valid && p.reason==MT_AMIGA_PROFILE_LIFECYCLE && p.req6==1U && p.read_bytes==0U);
    amiga_content_service_profile_stop();
    /* ATA's zero sector count means256. A rejected guest command is visible
     * in the histogram and invalidates the run without inventing data bytes. */
    assert(amiga_content_service_profile_start(timer,&ticks)==0);command(0x20U,0U,0U,0U);amiga_content_service_poll();
    p=snapshot();assert(!p.running && !p.valid && p.reason==MT_AMIGA_PROFILE_IO_ERROR);
    assert(p.requested_read_sectors==256U && p.read_command_hist[8]==1U && p.read_bytes==0U);
    for(unsigned failure=1U;failure<=2U;++failure) {
        assert(amiga_content_service_profile_start(timer,&ticks)==0);short_read=failure;
        const unsigned blocks=bulk_commands;command(0x20U,0U,0U,1U);amiga_content_service_poll();p=snapshot();
        assert(!p.running && !p.valid && p.reason==MT_AMIGA_PROFILE_IO_ERROR);
        assert(p.sd_read_calls==1U && p.read_bytes==128U && p.read_sectors==0U && p.fifo_calls==0U && bulk_commands==blocks);
        assert(p.read_window_started && p.read_window_bytes==0U && p.active_read_ticks==0U);
        retry();
    }
    assert(amiga_content_service_profile_start(timer,&ticks)==0);bulk_timeout=true;
    command(0x20U,0U,1U,1U);amiga_content_service_poll();p=snapshot();
    assert(!p.running && !p.valid && p.reason==MT_AMIGA_PROFILE_IO_ERROR && p.read_bytes==512U);
    assert(p.fifo_calls==1U && p.fifo_bytes==0U && ide_request==4U);
    assert(p.read_window_bytes==0U);
    assert(memcmp(original,disk->data,sizeof(original))==0);
    puts("PASS optional IDE profiler stages, no-I/O controls, partial/error bytes, unsigned wrap, overflow and lifecycle invalidation");
    return 0;
}
