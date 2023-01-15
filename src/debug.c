#define U32FromPtr(ptr) (*(u32*)(ptr))
#define U16FromPtr(ptr) (*(u16*)(ptr))
#define U8FromPtr(ptr) (*(u8*)(ptr))

static char* primary_op_str_table[] = 
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

static char* secondary_op_str_table[] = 
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


struct debug_state
{
    struct cpu_state* psx;
    u8* loaded_exe;
    void* output_handle;
};

static struct debug_state g_debug;

static char* opcode_to_string(Instruction ins)
{
    char* result = primary_op_str_table[ins.op];
    return result;
}

static void log_write(int level, char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(g_debug.output_handle, msg, args);
    va_end(args);
}

static void log_char(int c)
{
    fputc(c, g_debug.output_handle);
}

#if SY_DEBUG
#define log_init() 
#define debug_log(...) log_write(0, __VA_ARGS__)
#define debug_putchar(char) log_char(char)
#else
#define log_init()
#define debug_log(...)
#define debug_putchar(char)
#endif