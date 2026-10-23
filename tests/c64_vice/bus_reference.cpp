// SPDX-License-Identifier: GPL-3.0-only
// Share the unchanged VICE extraction scaffold, with a larger BA timing matrix.
#define main reu_functional_fixture_main
#include "reu_harness.cpp"
#undef main
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  initialize(24);clocks=fopen(argv[1],"w");if(!clocks)return 3;
  fprintf(clocks,"case,cycle,access,address,data\n");
  for(int op=0;op<4;op++)for(int length:{1,2,3,6})for(int begin:{1,2,3,5,8}){
    reset_case();case_id=1000+op*100+length*10+begin;ba_start=begin;ba_end=begin+3;
    for(int i=0;i<length;i++)reu_ram[0x10000+i]=c64[0x2000+i];
    rec.base_computer=0x2000;rec.base_reu=0;rec.bank_reu=1;rec.transfer_length=length;rec.command=0x90|op;
    reu_dma(1);drain();fprintf(clocks,"%d,%llu,END,0000,%02x\n",case_id,(unsigned long long)maincpu_clk,rec.status);
  }
  for(int length:{2,3,5})for(int mismatch=0;mismatch<length;mismatch++)for(int begin:{-1,1,2,3,5}){
    reset_case();case_id=2000+length*100+mismatch*10+begin+1;
    ba_start=begin;ba_end=begin<0 ? -1:begin+3;
    for(int i=0;i<length;i++)reu_ram[0x10000+i]=c64[0x2000+i]^(i==mismatch?1:0);
    rec.base_computer=0x2000;rec.base_reu=0;rec.bank_reu=1;rec.transfer_length=length;rec.command=0x93;
    reu_dma(1);drain();fprintf(clocks,"%d,%llu,END,0000,%02x\n",case_id,(unsigned long long)maincpu_clk,rec.status);
  }
  fflush(clocks);int error=ferror(clocks);fclose(clocks);return error?4:0;
}
