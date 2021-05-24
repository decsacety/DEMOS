/*
	The virtual mechine for LC-3
	developed by LOTE

	.ORIG 等是汇编指令而不是机器指令
*/
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h> // uint16_t
#include <stdio.h>  // FILE
#include <signal.h> // SIGINT
/* windows only */
#include <Windows.h>
#include <conio.h>  // _kbhit

HANDLE hStdin = INVALID_HANDLE_VALUE;


/* 65536 locations */
uint16_t memory[UINT16_MAX];

// Register
enum {
	R_R0 = 0,
	R_R1 , 
	R_R2, 
	R_R3,
	R_R4,
	R_R5,
	R_R6, 
	R_R7,
	R_PC,/*program counter*/
	R_COND,
	R_COUNT
};


// For register
uint16_t reg[R_COUNT];

//Operator
enum {
	OP_BR =0,// Branch分支
	OP_ADD,// Add 添加
	OP_LD, // Load 加载
	OP_ST, // Store 存储
	OP_JSR, // Jump to Register 跳转到寄存器
	OP_AND, // bitwise and 按位与
	OP_LDR, // Load register 加载寄存器
	OP_STR, // Store register 存储寄存器
	OP_RTI, // Unused 不使用，，，很奇怪啊这个
	OP_NOT, // BitWise not 按位取反
	OP_LDI, // Load indirect 间接加载
	OP_STI, // store indirectly 间接存储
	OP_JMP, // Jump 跳转
	OP_RES, // Reserved(unused)  保留
	OP_LEA, // Load effective address
	OP_TRAP // execute trap
};

// Condition flags 
enum {
	FL_POS = 1<<0, // P
	FL_ZRO = 1<<1, // Z
	FL_NEG = 1<<2, // N
};

// Trap
enum {
    TRAP_GETC = 0x20,   // get character from keyboard, not ehoed onto the terminal
    TRAP_OUT = 0x21,    // output a character
    TRAP_PUTS = 0x22,   // output a word string
    TRAP_IN = 0x23,     // get character from keyboard, echoed onto the terminal
    TRAP_PUTSP = 0x24,  // output a byte string
    TRAP_HALT = 0x25    // halt the program
};

// Memory Mapped Registers 11
enum {
    MR_KBSR = 0xfe00, // keyboard status
    MR_KBDR = 0xfe02  // keyboard dara
};

/* Part 13: Platform specifics for Windows */
uint16_t check_key() {
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode
        ^ ENABLE_ECHO_INPUT  /* no input echo */
        ^ ENABLE_LINE_INPUT; /* return when one or
                                more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}



/* Sign Extend 6 */
uint16_t sign_extend(uint16_t x, int bit_count)//正数补0， 负数补1
{
    if ((x >> (bit_count - 1))&1) //通过右移至只剩符号位再与1 & 来判断是否为负数
    {
        x |= (0xffff << bit_count); //进行负数的补位操作，将5位的负数补全成16位的负数
    }
    return x;
}
/* Update Flags 6 */
void update_flags(uint16_t r) {
    if (reg[r] == 0)
        reg[R_COND] = FL_ZRO;
    else if (reg[r] >> 15)//右移至只剩符号位来判断正负
        reg[R_COND] = FL_NEG;
    else
        reg[R_COND] = FL_POS;
}

//swap 10 将高位优先的指令转化为低位优先的指令
uint16_t swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}
// Read image file 10
void read_image_file(FILE* file) {
    // The origin tells us where in memory to place the image 
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    // we know the maxinum file size so we only need one fread
    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // swap to little endian
    while (read-- >0)
    {
        *p = swap16(*p);
        ++p;
    }
}
// 作为一个可以通过文件名直接调用的便捷函数
int read_image(const char* image_path) {
    FILE* file = fopen(image_path, "rb");
    if (!file)  return 0;
    read_image_file(file);
    fclose(file);
    return 1;
}

// Memorty access
void mem_write(uint16_t address, uint16_t val) {
    memory[address] = val;
}

uint16_t mem_read(uint16_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

int main(int argc, const char* argv[]) {

	//{Load Arguments, 5}
    if (argc < 2) {
        /*Show usage string*/
        printf("lc3[image-filel] ... \n");
    }

    for (int j = 1; j < argc; ++j) {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s \n", argv[j]);
            exit(1);
        }
    }

	// Widnow setup {Setup, 12}
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

	/* set the PC to stating position */
	/*0x3000 is the default*/
	enum {PC_START = 0x3000};
	reg[R_PC] = PC_START; //将PC_START的值赋给了register数组里的PC寄存器

	int running = 1;
	while (running) {

		// Fetch
		uint16_t instr = mem_read(reg[R_PC]++);//从内存中取下一条指令存到寄存器的pc寄存器里
		uint16_t op = instr >> 12;//操作符是指令里的4位，用移位运算符快速取出指令里的操作码（共16个。用4位来表示
                                  //由于是右移，所以操作码是高4位

        switch (op)//判断操作码来经行操作
        {
            case OP_ADD://ADD, 6
            {
                // destination register (DR) 目标寄存器
                uint16_t r0 = (instr >> 9) & 0x7;
                // first operand (SR1) 第一个操作数
                uint16_t r1 = (instr >> 6) & 0x7;
                // whether we are in immediate mode
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag)
                {
                    uint16_t imm5 = sign_extend(instr & 0x1f, 5);
                    reg[r0] = reg[r1] + imm5;
                }
                else {
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] + reg[r2];
                }

                update_flags(r0);//依据计算结果更新符号寄存器
            }
            break;
            case OP_AND://AND, 7
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag) {
                    uint16_t imm5 = sign_extend(instr & 0x1f, 5);
                    reg[r0] = reg[r1] & imm5;
                }
                else {
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] & reg[r2];
                }
                update_flags(r0);
            }
            break;
            case OP_NOT://NOT, 7
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;

                reg[r0] = ~reg[r1];
                update_flags(r0);
            }
            break;
            case OP_BR://BR, 7
            {
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                uint16_t cond_flag = (instr >> 9) & 0x7;
                if (cond_flag & reg[R_COND]) {
                    reg[R_PC] += pc_offset;
                }
            }
            break;
            case OP_JMP://JMP, 7
            {
                // Also handles RET
                uint16_t r1 = (instr >> 6) & 0x7;
                reg[R_PC] = reg[r1];
            }
            break;
            case OP_JSR://JSR, 7
            {
                uint16_t long_flag = (instr >> 11) & 1;
                reg[R_R7] = reg[R_PC];
                if (long_flag) {
                    uint16_t long_pc_offset = sign_extend(instr & 0x7ff, 11);
                    reg[R_PC] += long_pc_offset; // JSR
                }
                else {
                    uint16_t r1 = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[r1];//JSRR
                }
            }
            break;
            case OP_LD://LD, 7
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                reg[r0] = mem_read(reg[R_PC] + pc_offset);
                update_flags(r0);
            }
            break;
            case OP_LDI://LDI, 6 间接寻址
            {
                /* Destination register (DR)*/
                uint16_t r0 = (instr >> 9) & 0x7;
                /* PCoffset 9 */
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                /* 将pc_offset添加到当前pc， 查看该内存地址来获取最终地址 */
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
            }
            break;
            case OP_LDR://LDR, 7
            {
                uint16_t r0 = (instr >> 9)&0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t offset = sign_extend(instr & 0x3f, 6);
                reg[r0] = mem_read(reg[r1] + offset);
                update_flags(r0);
            }
            break;
            case OP_LEA://LEA, 7
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                reg[r0] = reg[R_PC] + pc_offset;
                update_flags(r0);
            }
            break;
            case OP_ST://ST, 7
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                mem_write(reg[R_PC] + pc_offset, reg[r0]);
            }
            break;
            case OP_STI://STI, 7
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
            }
            break;
            case OP_STR://STR, 7
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t offset = sign_extend(instr & 0x3F, 6);
                mem_write(reg[r1] + offset, reg[r0]);
            }
            break;
            case OP_TRAP://TRAP, 8
            {
                switch (instr & 0xff) {
                case TRAP_GETC:
                {
                    // read a single ASCII char 
                    reg[R_R0] = (uint16_t)getchar();
                }
                    break;
                case TRAP_OUT: {
                    putc((char)reg[R_R0], stdout);
                    fflush(stdout);
                }
                    break;
                case TRAP_PUTS:
                {
                    // one char per word 每个内存单元存了一个字符
                    uint16_t* c = memory + reg[R_R0];
                    while (*c) {
                        putc((char)*c, stdout);
                        ++c;
                    }
                    fflush(stdout);
                }
                    break;
                case TRAP_IN: {
                    printf("Enter a character:");
                    char c = getchar();
                    putc(c, stdout);
                    reg[R_R0] = (uint16_t)c;
                }
                    break;
                case TRAP_PUTSP: {
                    /* one char per byte (two bytes per word)
                    * here we need to swap back to big 
                    * endian format
                    */
                    uint16_t* c = memory + reg[R_R0];
                    while (*c)
                    {
                        char char1 = (*c) & 0xff;
                        putc(char1, stdout);
                        char char2 = (*c) >> 8;
                        if (char2) putc(char2, stdout);
                        ++c;
                    }
                    fflush(stdout);
                }
                    break;
                case TRAP_HALT: {
                    puts("HALT");
                    fflush(stdout);
                    running = 0;
                }
                    break;
                }
            }
            break;
            case OP_RES:
            case OP_RTI:
            default://BAD OPCODE, 7
            {
                abort();
            }
            break;
        }
	}

    //Windows {shotdown, 12}
    restore_input_buffering();
}