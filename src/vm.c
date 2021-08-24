#include <stdio.h>
#include "elf/elf.h"
#include "riscv/riscv.h"
#include "riscv/memory.h"

#define setx(rd, val) x[rd]=(rd)?(val):x[(rd)]

// bool trace = true;
bool trace = false;
bool halt = false;

uint32_t pc;

// perform csrrw
static uint32_t csrrw(uint32_t csri, uint32_t val) {
    uint32_t out = csr[csri];
    csr[csri] = val;
    return out;
}

// perform csrrs (atomic read and set)
static uint32_t csrrs(uint32_t csri, uint32_t val) {
    uint32_t out = csr[csri];
    csr[csri] |= val;
    return out;
}

// perform csrrc (atomic read and clear)
static uint32_t csrrc(uint32_t csri, uint32_t val) {
    uint32_t out = csr[csri];
    csr[csri] &= ~val;
    return out;
}

static bool rv_op() {
    switch (funct3) {
        case 0: x[rd] = (int32_t)x[rs1] + (int32_t)x[rs2]; break; // ADD
        case 1: x[rd] = x[rs1] << (x[rs2]&0x1f); break; // SLL
        case 2: x[rd] = ((int32_t)x[rs1] < (int32_t)x[rs2]) ? 1 : 0; break; // SLT
        case 3: x[rd] = (x[rs1]<x[rs2]) ? 1 : 0; break; // SLTU
        case 4: x[rd] = x[rs1] ^ x[rs2]; break; // XOR
        case 5: x[rd] = x[rs1] >> (x[rs2]&0x1f); break; // SRL
        case 6: x[rd] = x[rs1] | x[rs2]; break; // OR
        case 7: x[rd] = x[rs1] & x[rs2]; break; // AND
        default: ERROR("rv_op() funct3=%d not handled!", funct3);
    }
    pc+=4;
}

static bool rv_op_imm() {
    int32_t imm = (int32_t) i_imm;
    switch (funct3) {
        case 0: x[rd] = (int32_t)x[rs1] + imm; break; // ADDI
        case 1: x[rd] = x[rs1] << (imm&0x1f); break; // SLLI
        case 2: x[rd] = ((int32_t)x[rs1] < imm) ? 1 : 0; break; // SLTI
        case 3: x[rd] = (x[rs1]<imm) ? 1 : 0; break; // SLTIU
        case 4: x[rd] = x[rs1] ^ imm; break; // XORI
        case 5:
            if (imm & ~0x1f)
            x[rd] = ((int32_t) x[rs1]) >> (imm&0x1f); // SRAI
            else
            x[rd] = x[rs1] >> (imm&0x1f); // SRLI
        case 6: x[rd] = x[rs1] | imm; break; // ORI
        case 7: x[rd] = x[rs1] & imm; break; // ANDI
        default: ERROR("rv_op_imm() funct3=%d not handled!", funct3);
    }
    pc+=4;
}

static bool rv_branch() {
    bool jmp;
    switch (funct3) {
        case 0: jmp = (x[rs1] == x[rs2]); break; // BEQ
        case 1: jmp = (x[rs1] != x[rs2]); break; // BNE
        case 4: jmp = ((int32_t) x[rs1] < (int32_t) x[rs2]); break; // BLT
        case 5: jmp = ((int32_t) x[rs1] >= (int32_t) x[rs2]); break; // BGE
        case 6: jmp = (x[rs1] < x[rs2]); break; // BLTU
        case 7: jmp = (x[rs1] >= x[rs2]); break; // BGEU
        default: ERROR("rv_branch() funct3=%d not handled!", funct3);
    }
    if (jmp) {
        pc+=b_imm;
    } else pc+=4;
}

static bool rv_load() {
    int32_t imm = (int32_t) i_imm;
    uint32_t addr = x[rs1] + imm;
    switch (funct3) {
        case 0: // LB
            x[rd] = sign_extend_b(mem_read_b(addr));
            break;
        case 1: // LH
            if (addr & 1) ERROR("rv_load() LH addr should be even number!");
            x[rd] = sign_extend_h(mem_read_s(addr));
            break;
        case 2: // LW
            if (addr & 3) ERROR("rv_load() LW addr should be times of 4!");
            x[rd] = mem_read_w(addr);
            break;
        case 4: // LBU
            x[rd] = mem_read_b(addr);
            break;
        case 5: // LHU
            if (addr & 1) ERROR("rv_load() LHU addr should be times of 2!");
            x[rd] = mem_read_s(addr);
            break;
        default: ERROR("rv_load() funct3=%d not handled!", funct3);
    }
    pc+=4;
}

static bool rv_store() {
    int32_t imm = (int32_t) s_imm;
    uint32_t addr = x[rs1] + imm;
    uint32_t data = x[rs2];
    // printf("rv_store() addr = %x imm=%d rd=%d rs1=%d rs2=%d x[rs1]=%d\n", addr, imm, rd, rs1, rs2, x[rs1]);
    switch (funct3) {
        case 0: // SB
            mem_write_b(addr, data);
            break;
        case 1: // SH
            if (addr & 1) ERROR("rv_store() SH addr=0x%x should be even number!", addr);
            mem_write_s(addr, data);
            break;
        case 2: // SW
            if (addr & 3) ERROR("rv_store() SW addr=0x%x should be times of 4!", addr);
            mem_write_w(addr, data);
            break;
        default: ERROR("rv_store() funct3=%d not handled!", funct3);
    }
    pc+=4;
}

static bool rv_system() {
    uint32_t tmp;
    switch (funct3) {
        /*
        case 0: // 特殊指令，暫不處理
            switch (i_imm) {
            case 0: // ecall
                // syscall_handler();
                break;
            case 1: // ebreak
                halt = true;
                break;
            case 0x002: // uret
            case 0x102: // sret
            case 0x202: // hret
            case 0x105: // wfi
                ERROR();
            case 0x302: // mret
                pc = csr[mepc];
            default: ERROR();
            }
            break;
            */
        case 1: // CSRRW (Atomic Read/Write CSR)
            tmp = csrrw(csr_id, x[rs1]);
            x[rd] = rd ? tmp : x[rd];
            break;
        case 2: // CSRRS (Atomic Read and Set Bits in CSR)
            tmp = csrrs(csr_id, (rs1==0) ? 0u : x[rs1]);
            x[rd] = rd ? tmp : x[rd];
            break;
        case 3: // CSRRC (Atomic Read and Clear Bits in CSR)
            tmp = csrrs(csr_id, (rs1==0) ? ~0u : x[rs1]);
            x[rd] = rd ? tmp : x[rd];
            break;
        case 5: // CSRRWI
            tmp = csrrw(csr_id, rs1); // rv32emu-next 寫成 csr_csrrc(rv, csr, rv->X[rs1]); 是否錯誤
            x[rd] = rd ? tmp : x[rd];
            break;
        case 6: // CSRRSI
            tmp = csrrs(csr_id, rs1);
            x[rd] = rd ? tmp : x[rd];
            break;
        case 7: // CSRRCI
            tmp = csrrc(csr_id, rs1);
            x[rd] = rd ? tmp : x[rd];
            break;
        default: ERROR("rv_system() funct3=%d not handled!", funct3);
    }
    pc+=4;
}

bool rv_step()
{
    char *cmd, line[256];

    inst = mem_read_w(pc);
    rv_decode(inst);

    if (trace) {
        rv_dasm_inst(inst, line, pc);
        printf("%09x %09x %s", pc, inst, line);
    }

    uint32_t val, ra, addr, data, tmp, rel;
    bool jmp;
    
    // x[0] = 0;
    switch (op) {
        case 0b01101: val=u_imm; x[rd]=val; pc+=4; break;  // U-Type: lui a5,0x10000
        case 0b00101: val=u_imm+pc; x[rd]=val; pc+=4; break;  // U-Type: auipc a0,0x0
        case 0b11011: rel=j_imm; ra=pc+4; pc+=rel; x[rd]=ra; break;  // J-Type: jal ra,80008028 <lib_putc>
        case 0b11001: ra=pc+4; pc=(x[rs1]+i_imm)&~1u; x[rd]=ra; break;  // I-Type:  JALR rd, rs1, imm
        case 0b01100: rv_op(); break;     // OP     ex: add 
        case 0b00100: rv_op_imm(); break; // OP_IMM ex: addi
        case 0b11000: rv_branch(); break; // BRANCH ex: bne
        case 0b00000: rv_load(); break;   // LOAD   ex: lw
        case 0b01000: rv_store(); break;  // STORE  ex: sb
        case 0b11100: rv_system(); break; // SYSTEM ex: mret
        case 0b00011: pc += 4; break;     // FENCE  ex: fence (虛擬機中無作用)
        default: ERROR("rv_step() op=%d not handled!", op);
    }
    x[0] = 0;
    if (trace) {
        printf("\tx[rd:%d]=%x\n", rd, x[rd]);
    }
    return true;
}

bool rv_run(int entry) {
    int steps = 0;
    pc = entry;
    while (!halt) { 
        rv_step();
        steps++;
        if (steps == 1000) break;
    }
}

elf_t elf;

int main(int argc, char *argv[]) {
    char *fname=argv[1];
    if (!elf_load(&elf, fname)) {
        fprintf(stderr, "ELF file '%s' load fail \n", fname);
        return 1; 
    }
    elf_section_t s = elf_section(&elf, ".text");
    printf(".text: body=%p offset=0x%x size=%u\n", s.body, s.addr, s.size);
    printf("rv_init()...\n");
    rv_init();
    printf("mem_load_elf()...\n");
    mem_load_elf(&elf);
    printf("rv_run()...\n");
    rv_run(elf.hdr->e_entry);
    printf("mem_free()...\n");
    mem_free(&elf);
    elf_free(&elf);
}