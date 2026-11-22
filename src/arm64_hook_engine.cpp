#include "arm64_hook_engine.hpp"
#include <limits>

namespace astrae::hooks::arm64 {
namespace {
int64_t sign_extend(uint64_t value,unsigned bits){const uint64_t sign=uint64_t(1)<<(bits-1);return (value&sign)?static_cast<int64_t>(value)-static_cast<int64_t>(uint64_t(1)<<bits):static_cast<int64_t>(value);}
bool delta_between(uintptr_t destination,uintptr_t pc,int64_t &delta){if(destination>=pc){uintptr_t d=destination-pc;if(d>static_cast<uintptr_t>(std::numeric_limits<int64_t>::max()))return false;delta=static_cast<int64_t>(d);}else{uintptr_t d=pc-destination;if(d>static_cast<uintptr_t>(std::numeric_limits<int64_t>::max()))return false;delta=-static_cast<int64_t>(d);}return true;}
bool encode_scaled(int64_t delta,unsigned shift,unsigned bits,uint64_t &immediate){const int64_t scale=int64_t(1)<<shift;if((delta&(scale-1))!=0)return false;const int64_t scaled=delta/scale;const int64_t minimum=-(int64_t(1)<<(bits-1));const int64_t maximum=(int64_t(1)<<(bits-1))-1;if(scaled<minimum||scaled>maximum)return false;immediate=static_cast<uint64_t>(scaled)&((uint64_t(1)<<bits)-1);return true;}
bool add_signed(uintptr_t base,int64_t offset,uintptr_t &result){if(offset>=0){uintptr_t n=static_cast<uintptr_t>(offset);if(base>std::numeric_limits<uintptr_t>::max()-n)return false;result=base+n;return true;}uintptr_t n=static_cast<uintptr_t>(-(offset+1))+1;if(base<n)return false;result=base-n;return true;}
}
bool is_pc_relative_instruction(uint32_t insn){
    if((insn&0x9f000000u)==0x90000000u)return true; // ADRP
    if((insn&0x9f000000u)==0x10000000u)return true; // ADR
    if((insn&0x7c000000u)==0x14000000u)return true; // B / BL
    if((insn&0xff000010u)==0x54000000u)return true; // B.cond
    if((insn&0x7e000000u)==0x34000000u)return true; // CBZ / CBNZ
    if((insn&0x7e000000u)==0x36000000u)return true; // TBZ / TBNZ
    if((insn&0x3b000000u)==0x18000000u)return true; // LDR literal / PRFM
    return false;
}
bool encode_branch(uintptr_t pc,uintptr_t destination,uint32_t &instruction,std::string *error){int64_t delta=0;if((pc&3u)||(destination&3u)||!delta_between(destination,pc,delta)){if(error)*error="AArch64 B target/PC is unaligned or delta overflows";return false;}uint64_t immediate=0;if(!encode_scaled(delta,2,26,immediate)){if(error)*error="AArch64 B target is outside the +/-128MiB branch range";return false;}instruction=0x14000000u|static_cast<uint32_t>(immediate);if(error)error->clear();return true;}
bool relocate_instruction(uint32_t insn,uintptr_t old_pc,uintptr_t new_pc,uint32_t &out,std::string *error){
    out=insn;uint64_t immediate=0;int64_t delta=0;uintptr_t target=0;
    if((old_pc&3u)||(new_pc&3u)){if(error)*error="AArch64 instruction relocation requires 4-byte aligned PCs";return false;}
    // ADRP: keep the original page address, changing only the PC-relative page delta.
    if((insn&0x9f000000u)==0x90000000u){uint64_t raw=((uint64_t(insn>>5)&0x7ffffu)<<2)|((insn>>29)&3u);int64_t pages=sign_extend(raw,21);if(pages>std::numeric_limits<int64_t>::max()/4096||pages<std::numeric_limits<int64_t>::min()/4096){if(error)*error="ADRP page displacement overflow";return false;}uintptr_t old_page=old_pc&~uintptr_t(0xfff);if(!add_signed(old_page,pages*4096,target)){if(error)*error="ADRP resolved target overflows address space";return false;}uintptr_t new_page=new_pc&~uintptr_t(0xfff);if(!delta_between(target&~uintptr_t(0xfff),new_page,delta)||!encode_scaled(delta,12,21,immediate)){if(error)*error="ADRP target is outside relocation range";return false;}out=(insn&~((3u<<29)|(0x7ffffu<<5)))|(static_cast<uint32_t>(immediate&3u)<<29)|(static_cast<uint32_t>((immediate>>2)&0x7ffffu)<<5);if(error)error->clear();return true;}
    // ADR: preserve the absolute address.
    if((insn&0x9f000000u)==0x10000000u){uint64_t raw=((uint64_t(insn>>5)&0x7ffffu)<<2)|((insn>>29)&3u);if(!add_signed(old_pc,sign_extend(raw,21),target)||!delta_between(target,new_pc,delta)||delta<-(int64_t(1)<<20)||delta>((int64_t(1)<<20)-1)){if(error)*error="ADR target is outside relocation range";return false;}immediate=static_cast<uint64_t>(delta)&0x1fffffu;out=(insn&~((3u<<29)|(0x7ffffu<<5)))|(static_cast<uint32_t>(immediate&3u)<<29)|(static_cast<uint32_t>((immediate>>2)&0x7ffffu)<<5);if(error)error->clear();return true;}
    // B / BL. A branch back to the original entry must land at its relocated copy.
    if((insn&0x7c000000u)==0x14000000u){if(!add_signed(old_pc,sign_extend(insn&0x03ffffffu,26)*4,target)){if(error)*error="B/BL target overflows address space";return false;}if(target==old_pc)target=new_pc;if(!delta_between(target,new_pc,delta)||!encode_scaled(delta,2,26,immediate)){if(error)*error="B/BL target is outside relocation range";return false;}out=(insn&0xfc000000u)|static_cast<uint32_t>(immediate);if(error)error->clear();return true;}
    // B.cond.
    if((insn&0xff000010u)==0x54000000u){if(!add_signed(old_pc,sign_extend((insn>>5)&0x7ffffu,19)*4,target)){if(error)*error="B.cond target overflows address space";return false;}if(target==old_pc)target=new_pc;if(!delta_between(target,new_pc,delta)||!encode_scaled(delta,2,19,immediate)){if(error)*error="B.cond target is outside relocation range";return false;}out=(insn&~(0x7ffffu<<5))|(static_cast<uint32_t>(immediate)<<5);if(error)error->clear();return true;}
    // CBZ / CBNZ.
    if((insn&0x7e000000u)==0x34000000u){if(!add_signed(old_pc,sign_extend((insn>>5)&0x7ffffu,19)*4,target)){if(error)*error="CBZ/CBNZ target overflows address space";return false;}if(target==old_pc)target=new_pc;if(!delta_between(target,new_pc,delta)||!encode_scaled(delta,2,19,immediate)){if(error)*error="CBZ/CBNZ target is outside relocation range";return false;}out=(insn&~(0x7ffffu<<5))|(static_cast<uint32_t>(immediate)<<5);if(error)error->clear();return true;}
    // TBZ / TBNZ.
    if((insn&0x7e000000u)==0x36000000u){if(!add_signed(old_pc,sign_extend((insn>>5)&0x3fffu,14)*4,target)){if(error)*error="TBZ/TBNZ target overflows address space";return false;}if(target==old_pc)target=new_pc;if(!delta_between(target,new_pc,delta)||!encode_scaled(delta,2,14,immediate)){if(error)*error="TBZ/TBNZ target is outside relocation range";return false;}out=(insn&~(0x3fffu<<5))|(static_cast<uint32_t>(immediate)<<5);if(error)error->clear();return true;}
    // LDR literal / PRFM: preserve the same literal address; reject if immediate cannot reach.
    if((insn&0x3b000000u)==0x18000000u){if(!add_signed(old_pc,sign_extend((insn>>5)&0x7ffffu,19)*4,target)||!delta_between(target,new_pc,delta)||!encode_scaled(delta,2,19,immediate)){if(error)*error="literal load target is outside relocation range";return false;}out=(insn&~(0x7ffffu<<5))|(static_cast<uint32_t>(immediate)<<5);if(error)error->clear();return true;}
    if(error)error->clear();
    return true;
}
}
