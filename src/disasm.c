#include "disasm.h"

static const char *register_names[] = 
{
    "zero",
    "at",
    "v0",
    "v1",
    "a0",
    "a1",
    "a2",
    "a3",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "t8",
    "t9",
    "k0",
    "k1",
    "gp",
    "sp",
    "fp",
    "ra"
};

static const char* primary_op_str_table[] = 
{
    "special", 
    "bcond", 
    "j", 
    "jal", 
    "beq", 
    "bne", 
    "blez", 
    "bgtz", 
    "addi", 
    "addiu", 
    "slti", 
    "sltiu",
    "andi", 
    "ori", 
    "xori", 
    "lui", 
    "cop0", 
    "cop1", 
    "cop2", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "unknown", 
    "lb", 
    "lh", 
    "lwl", 
    "lw", 
    "lbu", 
    "lhu", 
    "lwr", 
    "unknown", 
    "sb", 
    "sh",
    "swl", 
    "sw", 
    "unknown", 
    "unknown", 
    "swr"
};

static const char* secondary_op_str_table[] = 
{
    "sll",
    "unknown",
    "srl",
    "sra",
    "sllv",
    "unknown",
    "srlv",
    "srav",
    "jr",
    "jalr",
    "unknown",
    "unknown",
    "syscall",
    "break",
    "unknown",
    "unknown",
    "mfhi",
    "mthi",
    "mflo",
    "mtlo",
    "unknown",
    "unknown",
    "unknown",
    "unknown",
    "mult",
    "multu",
    "div",
    "divu",
    "unknown",
    "unknown",
    "unknown",
    "unknown",
    "add",
    "addu",
    "sub",
    "subu",
    "and",
    "or",
    "xor",
    "nor",
    "unknown",
    "unknown",
    "slt",
    "sltu"
};

void instr_to_string(instruction ins, char *buffer, u32 len)
{
    const char *op;
    u8 primary = ins.op;
    if (ins.value == 0)
    {
        snprintf(buffer, len, "nop");
        return;
    }
    if (ins.op) 
    {
        op = primary_op_str_table[primary];
        u32 immediate = ins.value & 0xffff;
        if (primary == 1)
        {
            const char *bcond;
            u8 is_gez = (ins.value & (1 << 16)) != 0;
            u8 is_link = (ins.value & (1 << 20)) != 0;
            if (is_gez)
            {
                if (is_link)
                    bcond = "bgezal";
                else
                    bcond = "bgez";
            }
            else
            {
                if (is_link)
                    bcond = "bltzal";
                else
                    bcond = "bltz";
            }
            snprintf(buffer, len, "%s $%s, %+d", bcond, register_names[ins.rs], sign_extend16_32(immediate) << 2);
        }
        else if ((primary & 0x3e) == 0x2)
        {
            snprintf(buffer, len, "%s %#x", op, (ins.value & 0x3ffffff) << 2);
        }
        else if ((primary & 0x3e) == 0x4)
        {
            snprintf(buffer, len, "%s $%s, $%s, %+d", op, register_names[ins.rs], register_names[ins.rt], sign_extend16_32(immediate) << 2);
        }
        else if ((primary & 0x3e) == 0x6)
        {
            snprintf(buffer, len, "%s $%s, %+d", op, register_names[ins.rs], sign_extend16_32(immediate) << 2);
        }
        else if (primary == LUI)
        {
            snprintf(buffer, len, "%s $%s, %#x", op, register_names[ins.rt], immediate);
        }
        else if ((primary & 0x38) == 0x8)
        {
            snprintf(buffer, len, "%s $%s, $%s, %#x", op, register_names[ins.rt], register_names[ins.rs], immediate);
        }
        else if ((primary & 0x30) == 0x20)
        {
            snprintf(buffer, len, "%s $%s, %+d($%s)", op, register_names[ins.rt], sign_extend16_32(immediate), register_names[ins.rs]);
        }
        else
        {
            snprintf(buffer, len, "unknown");
        }
    }
    else 
    {
        u8 secondary = ins.secondary;
        op = secondary_op_str_table[secondary];
        if ((secondary & (1 << 5)))
        {
            snprintf(buffer, len, "%s $%s, $%s, $%s", op, register_names[ins.rd], register_names[ins.rs], register_names[ins.rt]);
        }
        else if ((secondary & (0xf << 2)) == 0x4)
        {
            snprintf(buffer, len, "%s $%s, $%s, $%s", op, register_names[ins.rd], register_names[ins.rt], register_names[ins.rs]);
        }
        else if ((secondary & (0xf << 2)) == 0x0)
        {
            snprintf(buffer, len, "%s $%s, $%s, %#x", op, register_names[ins.rd], register_names[ins.rt], ins.sa);
        }
        else if (secondary == JR || (secondary & 0x3d) == 0x11)
        {
            snprintf(buffer, len, "%s $%s", op, register_names[ins.rs]);
        }
        else if (secondary == JALR)
        {
            snprintf(buffer, len, "%s $%s, $%s", op, register_names[ins.rd], register_names[ins.rs]);
        }
        else if ((secondary & 0x3e) == 0xc)
        {
            snprintf(buffer, len, "%s", op);
        }
        else
        {
            snprintf(buffer, len, "unknown");
        }
    }
}
