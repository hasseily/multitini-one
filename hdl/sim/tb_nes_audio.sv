// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_nes_audio;
    logic clk=0,reset=1,machine_reset=0,mute=0;
    logic [15:0] sample=0;
    wire signed [23:0] audio_l,audio_r;
    wire audio_valid;
    always #5 clk=~clk;

    // The physical bench clock is accelerated. Four service ticks represent
    // one logical48kHz sample; waveform frequency is measured in those samples.
    multitini_nes_audio #(.CORE_HZ(192000),.AUDIO_SAMPLE_HZ(48000)) dut(.*);
    localparam real PI=3.14159265358979323846;
    integer i,held_dc,hold_checks=0,sample_count=0,previous_sample_cycle=0,cycles=0;
    integer old_dc;
    logic must_hold,checking=0;
    always @(posedge clk) begin
        old_dc=dut.audio_dc;
        must_hold=!reset&&!machine_reset&&(!dut.sample_ce||mute);
        #1;
        if(must_hold) begin
            if(dut.audio_dc!==old_dc) $fatal(1,"DC estimate advanced between samples or during pause");
            hold_checks=hold_checks+1;
        end
        if(checking) begin
            cycles=cycles+1;
            if(audio_valid) begin
                if(sample_count!=0&&cycles-previous_sample_cycle!=4)
                    $fatal(1,"audio sample cadence depended on non-audio work");
                previous_sample_cycle=cycles;sample_count=sample_count+1;
                if(audio_l!==audio_r) $fatal(1,"mono PCM channels differ");
            end
        end else begin
            cycles=0;sample_count=0;previous_sample_cycle=0;
        end
    end

    task reset_audio;
        begin
            @(negedge clk);checking=0;reset=1;machine_reset=0;mute=0;
            repeat(3) @(negedge clk);
            reset=0;checking=1;
        end
    endtask
    task drive_sample(input integer value);
        begin
            @(negedge clk);sample=value;
            do begin @(posedge clk);#2;end while(!audio_valid);
        end
    endtask
    task check_tone(input integer frequency);
        integer n,value;
        real angle,input_energy,output_energy,ac,pcm,pole,gain,w,expected,measured,error;
        begin
            reset_audio();input_energy=0;output_energy=0;
            //100ms warm-up exceeds nine DC-filter time constants. Measure an
            //integer number of periods over one logical second thereafter.
            for(n=0;n<52800;n=n+1) begin
                angle=2.0*PI*frequency*n/48000.0;
                value=$rtoi(32768.0+12000.0*$sin(angle));
                drive_sample(value);
                if(n>=4800) begin
                    ac=(value-32768)*128.0;pcm=$itor($signed(audio_l));
                    input_energy=input_energy+ac*ac;
                    output_energy=output_energy+pcm*pcm;
                end
            end
            pole=511.0/512.0;gain=1023.0/1024.0;w=2.0*PI*frequency/48000.0;
            expected=gain*gain*(2.0-2.0*$cos(w))/(1.0+pole*pole-2.0*pole*$cos(w));
            measured=output_energy/input_energy;error=measured-expected;
            if(error<0)error=-error;
            if(error>expected*0.01)
                $fatal(1,"%0dHz response gain=%f expected=%f",frequency,$sqrt(measured),$sqrt(expected));
            $display("NES audio %0dHz gain=%f expected=%f",frequency,$sqrt(measured),$sqrt(expected));
        end
    endtask

    initial begin
        check_tone(20);check_tone(50);check_tone(100);check_tone(440);
        reset_audio();
        drive_sample(0);
        if(audio_l>=-24'sd8000000) $fatal(1,"negative mixer excursion wrapped or lost Q7 range");
        for(i=0;i<12000;i=i+1)drive_sample(0);
        if(audio_l>256||audio_l< -256) $fatal(1,"negative DC step did not decay");
        drive_sample(65535);
        if(audio_l<=24'sd8000000) $fatal(1,"positive mixer excursion wrapped or lost Q7 range");
        for(i=0;i<12000;i=i+1)drive_sample(65535);
        if(audio_l>256||audio_l< -256) $fatal(1,"positive DC step did not decay");
        held_dc=dut.audio_dc;mute=1;
        for(i=0;i<100;i=i+1) begin
            drive_sample(i*613);
            if(audio_l!==24'sd0||dut.audio_dc!==held_dc)
                $fatal(1,"pause changed DC state or emitted PCM");
        end
        mute=0;drive_sample(0);
        if(audio_l>=-24'sd8000000) $fatal(1,"resume did not preserve held DC estimate");
        machine_reset=1;drive_sample(0);
        if(audio_l!==24'sd0||dut.audio_dc!==32'h00ffff00)
            $fatal(1,"machine reset did not clear audio state");
        if(hold_checks<100000) $fatal(1,"insufficient between-sample/pause hold coverage");
        $display("PASS NES audio:48kHz-domain DC blocker,20/50/100/440Hz response,DC decay,signed extrema,pause hold,resume and reset");
        $finish;
    end
    initial begin #20000000;$fatal(1,"NES audio timeout");end
endmodule
