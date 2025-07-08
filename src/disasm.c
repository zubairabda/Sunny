#include "disasm.h"

static const char* primary_op_str_table[] = 
{
    "special", "bcond", "j"   , "jal"  , "beq" , "bne", "blez", "bgtz", 
    "addi"   , "addiu", "slti", "sltiu", "andi", "ori", "xori", "lui" , 
    "cop0"   , "cop1" , "cop2", 0      , 0     , 0    , 0     , 0     , 
    0        , 0      , 0     , 0      , 0     , 0    , 0     , 0     , 
    "lb"     , "lh"   , "lwl" , "lw"   , "lbu" , "lhu", "lwr" , 0     ,
    "sb"     , "sh"   , "swl" , "sw"   , 0     , 0    , "swr" , 0     ,
    0        , 0      , "lwc2", 0      , 0     , 0    , 0     , 0     ,
    0        , 0      , "swc2", 0      , 0     , 0    , 0     , 0
};

static const char* secondary_op_str_table[] = 
{
    "sll" , 0      , "srl" , "sra" , "sllv"   , 0      , "srlv", "srav",
    "jr"  , "jalr" , 0     , 0     , "syscall", "break", 0     , 0     ,
    "mfhi", "mthi" , "mflo", "mtlo", 0        , 0      , 0     , 0     ,
    "mult", "multu", "div" , "divu", 0        , 0      , 0     , 0     ,
    "add" , "addu" , "sub" , "subu", "and"    , "or"   , "xor" , "nor" ,
    0     , 0      , "slt" , "sltu", 0        , 0      , 0     , 0     ,
    0     , 0      , 0     , 0     , 0        , 0      , 0     , 0     ,
    0     , 0      , 0     , 0     , 0        , 0      , 0     , 0
};

// TODO: this is still incorrect...
const char *instr_to_string(u32 i, char *buffer, u32 len)
{
    instruction ins = {.value = i};
    const char *op;
    u8 primary = ins.op;
    if (ins.value == 0)
    {
        buffer[0] = '\0';
        return "nop";
    }
    if (ins.op) 
    {
        op = primary_op_str_table[primary];
        if (!op)
        {
            buffer[0] = '\0';
            return "unknown";
        }
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
            snprintf(buffer, len, "$%s, %+d", register_names[ins.rs], sign_extend16_32(immediate) << 2);
            return bcond;
        }
        else if ((primary & 0x3e) == 0x2)
        {
            snprintf(buffer, len, "%#x", (ins.value & 0x3ffffff) << 2);
        }
        else if ((primary & 0x3e) == 0x4)
        {
            snprintf(buffer, len, "$%s, $%s, %+d", register_names[ins.rs], register_names[ins.rt], sign_extend16_32(immediate) << 2);
        }
        else if ((primary & 0x3e) == 0x6)
        {
            snprintf(buffer, len, "$%s, %+d", register_names[ins.rs], sign_extend16_32(immediate) << 2);
        }
        else if (primary == LUI)
        {
            snprintf(buffer, len, "$%s, %#x", register_names[ins.rt], immediate);
        }
        else if ((primary & 0x38) == 0x8)
        {
            snprintf(buffer, len, "$%s, $%s, %#x", register_names[ins.rt], register_names[ins.rs], immediate);
        }
        else if ((primary & 0x30) == 0x20)
        {
            snprintf(buffer, len, "$%s, %+d($%s)", register_names[ins.rt], sign_extend16_32(immediate), register_names[ins.rs]);
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
        if (!op)
        {
            buffer[0] = '\0';
            return "unknown";
        }
        if ((secondary & (1 << 5)))
        {
            snprintf(buffer, len, "$%s, $%s, $%s", register_names[ins.rd], register_names[ins.rs], register_names[ins.rt]);
        }
        else if ((secondary & (0xf << 2)) == 0x4)
        {
            snprintf(buffer, len, "$%s, $%s, $%s", register_names[ins.rd], register_names[ins.rt], register_names[ins.rs]);
        }
        else if ((secondary & (0xf << 2)) == 0x0)
        {
            snprintf(buffer, len, "$%s, $%s, %#x", register_names[ins.rd], register_names[ins.rt], ins.sa);
        }
        else if (secondary == JR || (secondary & 0x3d) == 0x11)
        {
            snprintf(buffer, len, "$%s", register_names[ins.rs]);
        }
        else if (secondary == JALR)
        {
            snprintf(buffer, len, "$%s, $%s", register_names[ins.rd], register_names[ins.rs]);
        }
        else if ((secondary & 0x3e) == 0xc)
        {
            snprintf(buffer, len, "");
        }
        else
        {
            snprintf(buffer, len, "unknown");
        }
    }
    return op;
}
