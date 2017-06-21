#ifndef AGAIN
#include "misc.h"
#include "emu/cpu.h"
#include "emu/modrm.h"
#include "emu/interrupt.h"
#include "sys/calls.h"

// instructions defined as macros
#include "emu/instructions.h"
#include "emu/sse.h"

#define OP_SIZE 32
#define cpu_step CONCAT(cpu_step, OP_SIZE)
#endif

// this will be the next PyEval_EvalFrameEx.
int cpu_step(struct cpu_state *cpu) {

#undef oax
#undef obx
#undef ocx
#undef odx
#undef osi
#undef odi
#undef obp
#undef osp
#if OP_SIZE == 32
#define oax cpu->eax
#define obx cpu->ebx
#define ocx cpu->ecx
#define odx cpu->edx
#define osi cpu->esi
#define odi cpu->edi
#define obp cpu->ebp
#define osp cpu->esp
#else
#define oax cpu->ax
#define obx cpu->bx
#define ocx cpu->cx
#define odx cpu->dx
#define osi cpu->si
#define odi cpu->di
#define obp cpu->bp
#define osp cpu->sp
#endif
#define uintop_t uint(OP_SIZE)
#define intop_t sint(OP_SIZE)

    // watch out: these macros can evaluate the arguments any number of times
#define MEM_ACCESS(addr, size, type) ({ \
        uint(size) *ptr = mem_##type##_ptr(&cpu->mem, addr); \
        if (ptr == NULL) { \
            cpu->eip = saved_ip; \
            cpu->segfault_addr = addr; \
            return INT_GPF; \
        } \
        ptr; \
    })
    dword_t saved_ip = cpu->eip;
#define MEM_(addr,size) (*MEM_ACCESS(addr, size, read))
#define MEM_W_(addr,size) (*MEM_ACCESS(addr, size, write))
#define MEM(addr) MEM_(addr,OP_SIZE)
#define MEM_W(addr) MEM_W_(addr,OP_SIZE)
#define MEM8(addr) MEM_(addr,8)
#define MEM8_W(addr) MEM_W_(addr,8)
#define REG_(reg_id,size) REG_VAL(cpu, reg_id, size)
#undef REG // was last defined in modrm.h
#define REG(reg_id) REG_(reg_id, OP_SIZE)
#define REGPTR_(regptr,size) REG_(CONCAT3(regptr.reg,size,_id),size)
#define REGPTR(regptr) REGPTR_(regptr, OP_SIZE)
#define REGPTR8(regptr) REGPTR_(regptr, 8)
#define REGPTR64(regptr) REGPTR_(regptr, 64)

    struct modrm_info modrm;
    dword_t addr = 0;
#define READMODRM modrm_decode32(cpu, &addr, &modrm)
#define modrm_val_(size) \
    (*(modrm.type == mod_reg ? &REGPTR_(modrm.modrm_regid, size) : &MEM_(addr, size)))
#define modrm_val_w_(size) \
    (*(modrm.type == mod_reg ? &REGPTR_(modrm.modrm_regid, size) : &MEM_W_(addr, size)))
#define modrm_val modrm_val_(OP_SIZE)
#define modrm_val_w modrm_val_w_(OP_SIZE)
#define modrm_val8 modrm_val_(8)
#define modrm_val8_w modrm_val_w_(8)
#define modrm_val16 modrm_val_(16)
#define modrm_val32 modrm_val_(32)
#define modrm_val64 modrm_val_(64)
#define modrm_val_sse ((union xmm_reg *) &modrm_val_(64))
#define modrm_reg REGPTR(modrm.reg)
#define modrm_reg8 REGPTR8(modrm.reg)
#define modrm_reg64 REGPTR64(modrm.reg)
#define modrm_reg_sse ((union xmm_reg *) &REGPTR_(modrm.reg, 64))

#undef imm
    byte_t imm8;
    word_t imm16;
    dword_t imm32;
#define READIMM_(name,size) \
    name = MEM_(cpu->eip,size); \
    cpu->eip += size/8; \
    TRACE("imm %x ", name)
#define imm CONCAT(imm, OP_SIZE)
#define READIMM READIMM_(imm, OP_SIZE)
#define READIMM8 READIMM_(imm8, 8)
#define READIMM16 READIMM_(imm16, 16)
    dword_t addr_offset = 0;
#define READADDR READIMM_(addr_offset, 32); addr += addr_offset
    byte_t insn;
#define READINSN \
    insn = MEM8(cpu->eip); \
    cpu->eip++; \
    TRACE("%02x ", insn);

restart:
    TRACE("%08x\t", cpu->eip);
    READINSN;
    switch (insn) {
        // if any instruction handlers declare variables, they should create a
        // new block for those variables.
        // any subtraction that occurs probably needs to have a cast to a
        // signed type, so sign extension happens.

        case 0x00: TRACEI("add reg8, modrm8");
                   READMODRM; ADD(modrm_reg8, modrm_val8_w); break;
        case 0x01: TRACEI("add reg, modrm");
                   READMODRM; ADD(modrm_reg, modrm_val_w); break;
        case 0x03: TRACEI("add modrm, reg");
                   READMODRM; ADD(modrm_val, modrm_reg); break;
        case 0x05: TRACEI("add imm, oax\t");
                   READIMM; ADD(imm, oax); break;

        case 0x08: TRACEI("or reg8, modrm8");
                   READMODRM; OR(modrm_reg8, modrm_val8_w); break;
        case 0x09: TRACEI("or reg, modrm");
                   READMODRM; OR(modrm_reg, modrm_val_w); break;
        case 0x0a: TRACEI("or modrm8, reg8");
                   READMODRM; OR(modrm_val8, modrm_reg8); break;
        case 0x0b: TRACEI("or modrm, reg");
                   READMODRM; OR(modrm_val, modrm_reg); break;
        case 0x0c: TRACEI("or imm8, al\t");
                   READIMM8; OR(imm8, cpu->al); break;
        case 0x0d: TRACEI("or imm, eax\t");
                   READIMM; OR(imm, oax); break;

        case 0x0f:
            // 2-byte opcode prefix
            READINSN;
            switch (insn) {
                case 0x1f: TRACEI("nop modrm\t"); READMODRM; break;

                case 0x28: TRACEI("movp modrm, reg");
                           READMODRM; MOVP(modrm_val_sse, modrm_reg_sse); break;
                case 0x29: TRACEI("movp reg, modrm");
                           READMODRM; MOVP(modrm_reg_sse, modrm_val_sse); break;

                case 0x31: TRACEI("rdtsc");
                           // TODO there's a clang builtin for this
                           __asm__("rdtsc" : "=a" (cpu->eax), "=d" (cpu->edx)); break;

                case 0x40: TRACEI("cmovo modrm, reg");
                           READMODRM; CMOV(O, modrm_val_w, modrm_reg); break;
                case 0x41: TRACEI("cmovno modrm, reg");
                           READMODRM; CMOV(!O, modrm_val_w, modrm_reg); break;
                case 0x42: TRACEI("cmovb modrm, reg");
                           READMODRM; CMOV(B, modrm_val_w, modrm_reg); break;
                case 0x43: TRACEI("cmovnb modrm, reg");
                           READMODRM; CMOV(!B, modrm_val_w, modrm_reg); break;
                case 0x44: TRACEI("cmove modrm, reg");
                           READMODRM; CMOV(E, modrm_val_w, modrm_reg); break;
                case 0x45: TRACEI("cmovne modrm, reg");
                           READMODRM; CMOV(!E, modrm_val_w, modrm_reg); break;
                case 0x46: TRACEI("cmovbe modrm, reg");
                           READMODRM; CMOV(BE, modrm_val_w, modrm_reg); break;
                case 0x47: TRACEI("cmova modrm, reg");
                           READMODRM; CMOV(!BE, modrm_val_w, modrm_reg); break;
                case 0x48: TRACEI("cmovs modrm, reg");
                           READMODRM; CMOV(S, modrm_val_w, modrm_reg); break;
                case 0x49: TRACEI("cmovns modrm, reg");
                           READMODRM; CMOV(!S, modrm_val_w, modrm_reg); break;
                case 0x4a: TRACEI("cmovp modrm, reg");
                           READMODRM; CMOV(P, modrm_val_w, modrm_reg); break;
                case 0x4b: TRACEI("cmovnp modrm, reg");
                           READMODRM; CMOV(!P, modrm_val_w, modrm_reg); break;
                case 0x4c: TRACEI("cmovl modrm, reg");
                           READMODRM; CMOV(L, modrm_val_w, modrm_reg); break;
                case 0x4d: TRACEI("cmovnl modrm, reg");
                           READMODRM; CMOV(!L, modrm_val_w, modrm_reg); break;
                case 0x4e: TRACEI("cmovle modrm, reg");
                           READMODRM; CMOV(LE, modrm_val_w, modrm_reg); break;
                case 0x4f: TRACEI("cmovnle modrm, reg");
                           READMODRM; CMOV(!LE, modrm_val_w, modrm_reg); break;

                case 0x57: TRACEI("xorps modrm, reg");
                           READMODRM; XORP(modrm_val_sse, modrm_reg_sse); break;
                case 0x73: TRACEI("psrlq imm8, reg");
                           // TODO I think this is actually a group
                           READMODRM; READIMM8; PSRLQ(imm8, modrm_val_sse); break;
                case 0x76: TRACEI("pcmpeqd reg, modrm");
                           READMODRM; PCMPEQD(modrm_reg_sse, modrm_val_sse); break;
                case 0x7e: TRACEI("movd reg, modrm32");
                           READMODRM; MOV(modrm_reg_sse->dw[0], modrm_val32); break;

                case 0x80: TRACEI("jo rel\t");
                           READIMM; J_REL(O, imm); break;
                case 0x81: TRACEI("jno rel\t");
                           READIMM; J_REL(!O, imm); break;
                case 0x82: TRACEI("jb rel\t");
                           READIMM; J_REL(B, imm); break;
                case 0x83: TRACEI("jnb rel\t");
                           READIMM; J_REL(!B, imm); break;
                case 0x84: TRACEI("je rel\t");
                           READIMM; J_REL(E, imm); break;
                case 0x85: TRACEI("jne rel\t");
                           READIMM; J_REL(!E, imm); break;
                case 0x86: TRACEI("jbe rel\t");
                           READIMM; J_REL(BE, imm); break;
                case 0x87: TRACEI("ja rel\t");
                           READIMM; J_REL(!BE, imm); break;
                case 0x88: TRACEI("js rel\t");
                           READIMM; J_REL(S, imm); break;
                case 0x89: TRACEI("jns rel\t");
                           READIMM; J_REL(!S, imm); break;
                case 0x8a: TRACEI("jp rel\t");
                           READIMM; J_REL(P, imm); break;
                case 0x8b: TRACEI("jnp rel\t");
                           READIMM; J_REL(!P, imm); break;
                case 0x8c: TRACEI("jl rel\t");
                           READIMM; J_REL(L, imm); break;
                case 0x8d: TRACEI("jnl rel\t");
                           READIMM; J_REL(!L, imm); break;
                case 0x8e: TRACEI("jle rel\t");
                           READIMM; J_REL(LE, imm); break;
                case 0x8f: TRACEI("jnle rel\t");
                           READIMM; J_REL(!LE, imm); break;

                case 0x90: TRACEI("seto\t");
                           READMODRM; SET(O, modrm_val8_w); break;
                case 0x91: TRACEI("setno\t");
                           READMODRM; SET(!O, modrm_val8_w); break;
                case 0x92: TRACEI("setb\t");
                           READMODRM; SET(B, modrm_val8_w); break;
                case 0x93: TRACEI("setnb\t");
                           READMODRM; SET(!B, modrm_val8_w); break;
                case 0x94: TRACEI("sete\t");
                           READMODRM; SET(E, modrm_val8_w); break;
                case 0x95: TRACEI("setne\t");
                           READMODRM; SET(!E, modrm_val8_w); break;
                case 0x96: TRACEI("setbe\t");
                           READMODRM; SET(BE, modrm_val8_w); break;
                case 0x97: TRACEI("setnbe\t");
                           READMODRM; SET(!BE, modrm_val8_w); break;

                case 0xa2:
                    TRACEI("cpuid");
                    do_cpuid(&cpu->eax, &cpu->ebx, &cpu->ecx, &cpu->edx);
                    break;

                case 0xa3: TRACEI("bt reg, modrm");
                           READMODRM; BT(modrm_reg, modrm_val); break;

                case 0xa4: TRACEI("shld imm8, reg, modrm");
                           READMODRM; READIMM8; SHLD(imm8, modrm_reg, modrm_val); break;
                case 0xa5: TRACEI("shld cl, reg, modrm");
                           READMODRM; SHLD(cpu->cl, modrm_reg, modrm_val); break;

                case 0xac: TRACEI("shrd imm8, reg, modrm");
                           READMODRM; READIMM8; SHRD(imm8, modrm_reg, modrm_val); break;

                case 0xaf: TRACEI("imul modrm, reg");
                           READMODRM; MUL2((int32_t) modrm_val, modrm_reg); break;

                case 0xb1: TRACEI("cmpxchg reg, modrm");
                           READMODRM; CMPXCHG(modrm_reg, modrm_val); break;

                case 0xb6: TRACEI("movz modrm8, reg");
                           READMODRM; MOV(modrm_val8, modrm_reg); break;
                case 0xb7: TRACEI("movz modrm16, reg");
                           READMODRM; MOV(modrm_val16, modrm_reg); break;

                case 0xba: TRACEI("grp8 imm8, modrm");
                           READMODRM; READIMM8; GRP8(imm8, modrm_val); break;

                case 0xbc: TRACEI("bsf modrm, reg");
                           READMODRM; BSF(modrm_val, modrm_reg); break;

                case 0xbe: TRACEI("movs modrm8, reg");
                           READMODRM; MOV((int8_t) modrm_val8, modrm_reg); break;
                case 0xbf: TRACEI("movs modrm16, reg");
                           READMODRM; MOV((int16_t) modrm_val16, modrm_reg); break;

                case 0xc0: TRACEI("xadd reg8, modrm8");
                           READMODRM; XADD(modrm_reg8, modrm_val8); break;
                case 0xc1: TRACEI("xadd reg, modrm");
                           READMODRM; XADD(modrm_reg, modrm_val); break;

                case 0xc8: TRACEI("bswap eax");
                           BSWAP(cpu->eax); break;
                case 0xc9: TRACEI("bswap ecx");
                           BSWAP(cpu->ecx); break;
                case 0xca: TRACEI("bswap edx");
                           BSWAP(cpu->edx); break;
                case 0xcb: TRACEI("bswap ebx");
                           BSWAP(cpu->ebx); break;
                case 0xcc: TRACEI("bswap esp");
                           BSWAP(cpu->esp); break;
                case 0xcd: TRACEI("bswap ebp");
                           BSWAP(cpu->ebp); break;
                case 0xce: TRACEI("bswap esi");
                           BSWAP(cpu->esi); break;
                case 0xcf: TRACEI("bswap edi");
                           BSWAP(cpu->edi); break;

                case 0xd4: TRACEI("paddq modrm, reg");
                           READMODRM; PADD(modrm_val_sse, modrm_reg_sse); break;
                case 0xd6:
                    // someone tell intel to get a life
                    if (OP_SIZE == 16) {
                        TRACEI("movq reg, modrm");
                        READMODRM; MOV(modrm_reg64, modrm_val64);
                    } else {
                        return INT_UNDEFINED;
                    }
                    break;

                case 0xfb: TRACEI("psubq modrm, reg");
                           READMODRM; PSUB(modrm_val_sse, modrm_reg_sse); break;
                default:
                    TRACEI("undefined");
                    return INT_UNDEFINED;
            }
            break;

        case 0x11: TRACEI("adc reg, modrm");
                   READMODRM; ADC(modrm_reg, modrm_val_w); break;
        case 0x13: TRACEI("adc modrm, reg");
                   READMODRM; ADC(modrm_val, modrm_reg); break;

        case 0x19: TRACEI("sbb reg, modrm");
                   READMODRM; SBB(modrm_reg, modrm_val); break;
        case 0x1b: TRACEI("sbb modrm, reg");
                   READMODRM; SBB(modrm_val, modrm_reg); break;

        case 0x20: TRACEI("and reg8, modrm8");
                   READMODRM; AND(modrm_reg8, modrm_val8_w); break;
        case 0x21: TRACEI("and reg, modrm");
                   READMODRM; AND(modrm_reg, modrm_val_w); break;
        case 0x22: TRACEI("and modrm8, reg8");
                   READMODRM; AND(modrm_val8, modrm_reg8); break;
        case 0x23: TRACEI("and modrm, reg");
                   READMODRM; AND(modrm_val, modrm_reg); break;
        case 0x25: TRACEI("and imm, oax\t");
                   READIMM; AND(imm, oax); break;

        case 0x29: TRACEI("sub reg, modrm");
                   READMODRM; SUB(modrm_reg, modrm_val_w); break;
        case 0x2b: TRACEI("sub modrm, reg");
                   READMODRM; SUB(modrm_val, modrm_reg); break;
        case 0x2d: TRACEI("sub imm, oax\t");
                   READIMM; SUB(imm, oax); break;

        case 0x2e: TRACEI("segment cs (ignoring)"); goto restart;

        case 0x30: TRACEI("xor reg8, modrm8");
                   READMODRM; XOR(modrm_reg8, modrm_val8_w); break;
        case 0x31: TRACEI("xor reg, modrm");
                   READMODRM; XOR(modrm_reg, modrm_val_w); break;
        case 0x32: TRACEI("xor modrm8, reg8");
                   READMODRM; XOR(modrm_val8, modrm_reg8); break;
        case 0x33: TRACEI("xor modrm, reg");
                   READMODRM; XOR(modrm_val, modrm_reg); break;

        case 0x38: TRACEI("cmp reg8, modrm8");
                   READMODRM; CMP(modrm_reg8, modrm_val8); break;
        case 0x39: TRACEI("cmp reg, modrm");
                   READMODRM; CMP(modrm_reg, modrm_val); break;
        case 0x3a: TRACEI("cmp modrm8, reg8");
                   READMODRM; CMP(modrm_val8, modrm_reg8); break;
        case 0x3b: TRACEI("cmp modrm, reg");
                   READMODRM; CMP(modrm_val, modrm_reg); break;
        case 0x3c: TRACEI("cmp imm8, al\t");
                   READIMM8; CMP(imm8, cpu->al); break;
        case 0x3d: TRACEI("cmp imm, oax\t");
                   READIMM; CMP(imm, oax); break;

        case 0x40: TRACEI("inc oax"); INC(oax); break;
        case 0x41: TRACEI("inc ocx"); INC(ocx); break;
        case 0x42: TRACEI("inc odx"); INC(odx); break;
        case 0x43: TRACEI("inc obx"); INC(obx); break;
        case 0x44: TRACEI("inc osp"); INC(osp); break;
        case 0x45: TRACEI("inc obp"); INC(obp); break;
        case 0x46: TRACEI("inc osi"); INC(osi); break;
        case 0x47: TRACEI("inc odi"); INC(odi); break;
        case 0x48: TRACEI("dec oax"); DEC(oax); break;
        case 0x49: TRACEI("dec ocx"); DEC(ocx); break;
        case 0x4a: TRACEI("dec odx"); DEC(odx); break;
        case 0x4b: TRACEI("dec obx"); DEC(obx); break;
        case 0x4c: TRACEI("dec osp"); DEC(osp); break;
        case 0x4d: TRACEI("dec obp"); DEC(obp); break;
        case 0x4e: TRACEI("dec osi"); DEC(osi); break;
        case 0x4f: TRACEI("dec odi"); DEC(odi); break;

        case 0x50: TRACEI("push oax"); PUSH(oax); break;
        case 0x51: TRACEI("push ocx"); PUSH(ocx); break;
        case 0x52: TRACEI("push odx"); PUSH(odx); break;
        case 0x53: TRACEI("push obx"); PUSH(obx); break;
        case 0x54: {
            TRACEI("push osp");
            // need to make sure to push the old value
            dword_t old_sp = osp;
            PUSH(old_sp); break;
        }
        case 0x55: TRACEI("push obp"); PUSH(obp); break;
        case 0x56: TRACEI("push osi"); PUSH(osi); break;
        case 0x57: TRACEI("push odi"); PUSH(odi); break;

        case 0x58: TRACEI("pop oax"); POP(oax); break;
        case 0x59: TRACEI("pop ocx"); POP(ocx); break;
        case 0x5a: TRACEI("pop odx"); POP(odx); break;
        case 0x5b: TRACEI("pop obx"); POP(obx); break;
        case 0x5c: {
            TRACEI("pop osp");
            dword_t new_sp;
            POP(new_sp);
            osp = new_sp;
            break;
        }
        case 0x5d: TRACEI("pop obp"); POP(obp); break;
        case 0x5e: TRACEI("pop osi"); POP(osi); break;
        case 0x5f: TRACEI("pop odi"); POP(odi); break;

        case 0x65: TRACE("segment gs\n");
                   addr += cpu->tls_ptr; goto restart;

        case 0x66:
#if OP_SIZE == 32
            TRACE("entering 16 bit mode\n");
            return cpu_step16(cpu);
#else
            TRACE("entering 32 bit mode\n");
            return cpu_step32(cpu);
#endif

        case 0x68: TRACEI("push imm\t");
                   READIMM; PUSH(imm); break;
        case 0x69: TRACEI("imul imm\t");
                   READMODRM; READIMM; MUL3((int32_t) imm, (int32_t) modrm_val, modrm_reg); break;
        case 0x6a: TRACEI("push imm8\t");
                   READIMM8; PUSH((int8_t) imm8); break;
        case 0x6b: TRACEI("imul imm8\t");
                   READMODRM; READIMM8; MUL3((int8_t) imm8, (int32_t) modrm_val, modrm_reg); break;

        case 0x70: TRACEI("jo rel8\t");
                   READIMM8; J_REL(O, (int8_t) imm8); break;
        case 0x71: TRACEI("jno rel8\t");
                   READIMM8; J_REL(!O, (int8_t) imm8); break;
        case 0x72: TRACEI("jb rel8\t");
                   READIMM8; J_REL(B, (int8_t) imm8); break;
        case 0x73: TRACEI("jnb rel8\t");
                   READIMM8; J_REL(!B, (int8_t) imm8); break;
        case 0x74: TRACEI("je rel8\t");
                   READIMM8; J_REL(E, (int8_t) imm8); break;
        case 0x75: TRACEI("jne rel8\t");
                   READIMM8; J_REL(!E, (int8_t) imm8); break;
        case 0x76: TRACEI("jbe rel8\t");
                   READIMM8; J_REL(BE, (int8_t) imm8); break;
        case 0x77: TRACEI("ja rel8\t");
                   READIMM8; J_REL(!BE, (int8_t) imm8); break;
        case 0x78: TRACEI("js rel8\t");
                   READIMM8; J_REL(S, (int8_t) imm8); break;
        case 0x79: TRACEI("jns rel8\t");
                   READIMM8; J_REL(!S, (int8_t) imm8); break;
        case 0x7a: TRACEI("jp rel8\t");
                   READIMM8; J_REL(P, (int8_t) imm8); break;
        case 0x7b: TRACEI("jnp rel8\t");
                   READIMM8; J_REL(!P, (int8_t) imm8); break;
        case 0x7c: TRACEI("jl rel8\t");
                   READIMM8; J_REL(L, (int8_t) imm8); break;
        case 0x7d: TRACEI("jnl rel8\t");
                   READIMM8; J_REL(!L, (int8_t) imm8); break;
        case 0x7e: TRACEI("jle rel8\t");
                   READIMM8; J_REL(LE, (int8_t) imm8); break;
        case 0x7f: TRACEI("jnle rel8\t");
                   READIMM8; J_REL(!LE, (int8_t) imm8); break;

        case 0x80: TRACEI("grp1 imm8, modrm8");
                   // FIXME this casts uint8 to int32 which is wrong
                   READMODRM; READIMM8; GRP1(imm8, modrm_val8); break;
        case 0x81: TRACEI("grp1 imm, modrm");
                   READMODRM; READIMM; GRP1(imm, modrm_val); break;
        case 0x83: TRACEI("grp1 imm8, modrm");
                   READMODRM; READIMM8; GRP1((uint32_t) (int8_t) imm8, modrm_val); break;

        case 0x84: TRACEI("test reg8, modrm8");
                   READMODRM; TEST(modrm_reg8, modrm_val8); break;
        case 0x85: TRACEI("test reg, modrm");
                   READMODRM; TEST(modrm_reg, modrm_val); break;

        case 0x86: TRACEI("xchg reg8, modrm8");
                   READMODRM; XCHG(modrm_reg8, modrm_val8); break;
        case 0x87: TRACEI("xchg reg, modrm");
                   READMODRM; XCHG(modrm_reg, modrm_val); break;

        case 0x88: TRACEI("mov reg8, modrm8");
                   READMODRM; MOV(modrm_reg8, modrm_val8_w); break;
        case 0x89: TRACEI("mov reg, modrm");
                   READMODRM; MOV(modrm_reg, modrm_val_w); break;
        case 0x8a: TRACEI("mov modrm8, reg8");
                   READMODRM; MOV(modrm_val8, modrm_reg8); break;
        case 0x8b: TRACEI("mov modrm, reg");
                   READMODRM; MOV(modrm_val, modrm_reg); break;

        case 0x8d:
            TRACEI("lea\t\t");
            READMODRM;
            if (modrm.type == mod_reg) {
                return INT_UNDEFINED;
            }
            modrm_reg = addr; break;
        case 0x8e:
            TRACEI("mov modrm, seg\t");
            // only gs is supported, and it does nothing
            // see comment in sys/tls.c
            READMODRM;
            if (modrm.reg.reg32_id != REG_ID(ebp)) {
                return INT_UNDEFINED;
            }
            break;

        case 0x90: TRACEI("nop"); break;
        case 0x97: TRACEI("xchg odi, oax");
                   XCHG(odi, oax); break;

        case 0x99: TRACEI("cdq");
                   // TODO make this its own macro 
                   // (also it's probably wrong in some subtle way)
                   odx = oax & (1 << (OP_SIZE - 1)) ? (uintop_t) -1 : 0; break;

        case 0xa1: TRACEI("mov mem, eax\t");
                   READADDR; MOV(MEM(addr), oax); break;
        case 0xa2: TRACEI("mov al, mem\t");
                   READADDR; MOV(cpu->al, MEM8_W(addr)); break;
        case 0xa3: TRACEI("mov oax, mem\t");
                   READADDR; MOV(oax, MEM_W(addr)); break;
        case 0xa4: TRACEI("movsb"); MOVSB; break;
        case 0xa5: TRACEI("movs"); MOVS; break;

        case 0xa8: TRACEI("test imm8, al");
                   READIMM8; TEST(imm8, cpu->al); break;
        case 0xa9: TRACEI("test imm, oax");
                   READIMM; TEST(imm, oax); break;

        case 0xaa: TRACEI("stosb"); STOSB; break;

        case 0xb0: TRACEI("mov imm, al\t");
                   READIMM8; MOV(imm8, cpu->al); break;
        case 0xb1: TRACEI("mov imm, cl\t");
                   READIMM8; MOV(imm8, cpu->cl); break;
        case 0xb2: TRACEI("mov imm, dl\t");
                   READIMM8; MOV(imm8, cpu->dl); break;
        case 0xb3: TRACEI("mov imm, bl\t");
                   READIMM8; MOV(imm8, cpu->bl); break;
        case 0xb4: TRACEI("mov imm, ah\t");
                   READIMM8; MOV(imm8, cpu->ah); break;
        case 0xb5: TRACEI("mov imm, ch\t");
                   READIMM8; MOV(imm8, cpu->ch); break;
        case 0xb6: TRACEI("mov imm, dh\t");
                   READIMM8; MOV(imm8, cpu->dh); break;
        case 0xb7: TRACEI("mov imm, bh\t");
                   READIMM8; MOV(imm8, cpu->bh); break;

        case 0xb8: TRACEI("mov imm, oax\t");
                   READIMM; MOV(imm, oax); break;
        case 0xb9: TRACEI("mov imm, ocx\t");
                   READIMM; MOV(imm, ocx); break;
        case 0xba: TRACEI("mov imm, odx\t");
                   READIMM; MOV(imm, odx); break;
        case 0xbb: TRACEI("mov imm, obx\t");
                   READIMM; MOV(imm, obx); break;
        case 0xbc: TRACEI("mov imm, osp\t");
                   READIMM; MOV(imm, osp); break;
        case 0xbd: TRACEI("mov imm, obp\t");
                   READIMM; MOV(imm, obp); break;
        case 0xbe: TRACEI("mov imm, osi\t");
                   READIMM; MOV(imm, osi); break;
        case 0xbf: TRACEI("mov imm, odi\t");
                   READIMM; MOV(imm, odi); break;

        case 0xc0: TRACEI("grp2 imm8, modrm8");
                   READMODRM; READIMM8; GRP2(imm8, modrm_val8_w); break;
        case 0xc1: TRACEI("grp2 imm8, modrm");
                   READMODRM; READIMM8; GRP2(imm8, modrm_val_w); break;

        case 0xc2: TRACEI("ret near imm\t");
                   READIMM16; RET_NEAR_IMM(imm16); break;
        case 0xc3: TRACEI("ret near");
                   RET_NEAR(); break;

        case 0xc9: TRACEI("leave");
                   MOV(cpu->ebp, cpu->esp); POP(cpu->ebp); break;

        case 0xcd: TRACEI("int imm8\t");
                   READIMM8; INT(imm8); break;

        case 0xc6: TRACEI("mov imm8, modrm8");
                   READMODRM; READIMM8; MOV(imm8, modrm_val8_w); break;
        case 0xc7: TRACEI("mov imm, modrm");
                   READMODRM; READIMM; MOV(imm, modrm_val_w); break;

        case 0xd0: TRACEI("grp2 1, modrm8");
                   READMODRM; GRP2(1, modrm_val8_w); break;
        case 0xd1: TRACEI("grp2 1, modrm");
                   READMODRM; GRP2(1, modrm_val_w); break;
        case 0xd3: TRACEI("grp2 cl, modrm");
                   READMODRM; GRP2(cpu->cl, modrm_val_w); break;

        case 0xe3: TRACEI("jcxz rel8\t");
                  READIMM8; if (ocx == 0) JMP_REL((int8_t) imm8); break;

        case 0xe8: TRACEI("call near\t");
                   READIMM; CALL_REL(imm); break;

        case 0xe9: TRACEI("jmp rel\t");
                   READIMM; JMP_REL(imm); break;
        case 0xeb: TRACEI("jmp rel8\t");
                   READIMM8; JMP_REL((int8_t) imm8); break;

        case 0xf0: TRACE("lock (ignored for now)\n"); goto restart;

        case 0xf3:
            READINSN;
            switch (insn) {
                case 0x0f:
                    // 2-byte opcode prefix
                    // after a rep prefix, means we have sse/mmx insanity
                    READINSN;
                    switch (insn) {
                        case 0x7e: TRACEI("movq modrm, xmm");
                                   READMODRM; MOVQ(modrm_val_sse, modrm_reg_sse); break;
                    }
                    break;

                case 0xa4: TRACEI("rep movsb"); REP(MOVSB); break;
                case 0xa5: TRACEI("rep movs"); REP(MOVS); break;

                case 0xaa: TRACEI("rep stosb"); REP(STOSB); break;
                case 0xab: TRACEI("rep stos"); REP(STOS); break;

                // repz ret is equivalent to ret but on some amd chips there's
                // a branch prediction penalty if the target of a branch is a
                // ret. gcc used to use nop ret but repz ret is only one
                // instruction
                case 0xc3: TRACEI("repz ret\t"); RET_NEAR(); break;
                default: TRACE("undefined\n"); return INT_UNDEFINED;
            }
            break;

        case 0xf6: TRACEI("grp3 modrm8\t");
                   READMODRM; GRP38(modrm_val8); break;
        case 0xf7: TRACEI("grp3 modrm\t");
                   READMODRM; GRP3(modrm_val); break;

        case 0xfc: TRACEI("cld"); cpu->df = 0; break;
        case 0xfd: TRACEI("std"); cpu->df = 1; break;

        case 0xff: TRACEI("grp5 modrm\t");
                   READMODRM; GRP5(modrm_val); break;

        default:
            TRACE("undefined\n");
            return INT_UNDEFINED;
    }
    TRACE("\n");
    return -1; // everything is ok.
}

#ifndef AGAIN
#define AGAIN

#undef OP_SIZE
#define OP_SIZE 16
#include "cpu.c"

flatten void cpu_run(struct cpu_state *cpu) {
    while (true) {
        int interrupt = cpu_step32(cpu);
        if (interrupt != INT_NONE) {
            handle_interrupt(cpu, interrupt);
        }
    }
}

#endif
