#ifndef _TRAP_H_
#define _TRAP_H_

#ifndef __ASSEMBLER__

#include <types.h>

/*
0(zero) - 常量 0 值，不可修改
1(at) - 汇编器临时寄存器，供汇编器使用
2(v0) - 函数返回值，也用于系统调用
3(v1) - 函数返回值（第二个）
4(a0) - 函数参数 1
5(a1) - 函数参数 2
6(a2) - 函数参数 3
7(a3) - 函数参数 4
8(t0) - 临时寄存器 1，调用者保存
9(t1) - 临时寄存器 2，调用者保存
10(t2) - 临时寄存器 3，调用者保存
11(t3) - 临时寄存器 4，调用者保存
12(t4) - 临时寄存器 5，调用者保存
13(t5) - 临时寄存器 6，调用者保存
14(t6) - 临时寄存器 7，调用者保存
15(t7) - 临时寄存器 8，调用者保存
16(s0) - 保存寄存器 1，被调用者保存
17(s1) - 保存寄存器 2，被调用者保存
18(s2) - 保存寄存器 3，被调用者保存
19(s3) - 保存寄存器 4，被调用者保存
20(s4) - 保存寄存器 5，被调用者保存
21(s5) - 保存寄存器 6，被调用者保存
22(s6) - 保存寄存器 7，被调用者保存
23(s7) - 保存寄存器 8，被调用者保存
24(t8) - 临时寄存器 9，调用者保存
25(t9) - 临时寄存器 10，调用者保存
26(k0) - 保留给操作系统内核使用 1
27(k1) - 保留给操作系统内核使用 2
28(gp) - 全局指针
29(sp) - 栈指针
30(fp) - 帧指针
31(ra) - 返回地址
*/

struct Trapframe {
	/* Saved main processor registers. */
	unsigned long regs[32];

	/* Saved special registers. */
	unsigned long cp0_status;
	unsigned long hi;
	unsigned long lo;
	unsigned long cp0_badvaddr;
	unsigned long cp0_cause;
	unsigned long cp0_epc;
};

void print_tf(struct Trapframe *tf);

#endif /* !__ASSEMBLER__ */

/*
 * Stack layout for all exceptions
 */

#define TF_REG0 0
#define TF_REG1 ((TF_REG0) + 4)
#define TF_REG2 ((TF_REG1) + 4)
#define TF_REG3 ((TF_REG2) + 4)
#define TF_REG4 ((TF_REG3) + 4)
#define TF_REG5 ((TF_REG4) + 4)
#define TF_REG6 ((TF_REG5) + 4)
#define TF_REG7 ((TF_REG6) + 4)
#define TF_REG8 ((TF_REG7) + 4)
#define TF_REG9 ((TF_REG8) + 4)
#define TF_REG10 ((TF_REG9) + 4)
#define TF_REG11 ((TF_REG10) + 4)
#define TF_REG12 ((TF_REG11) + 4)
#define TF_REG13 ((TF_REG12) + 4)
#define TF_REG14 ((TF_REG13) + 4)
#define TF_REG15 ((TF_REG14) + 4)
#define TF_REG16 ((TF_REG15) + 4)
#define TF_REG17 ((TF_REG16) + 4)
#define TF_REG18 ((TF_REG17) + 4)
#define TF_REG19 ((TF_REG18) + 4)
#define TF_REG20 ((TF_REG19) + 4)
#define TF_REG21 ((TF_REG20) + 4)
#define TF_REG22 ((TF_REG21) + 4)
#define TF_REG23 ((TF_REG22) + 4)
#define TF_REG24 ((TF_REG23) + 4)
#define TF_REG25 ((TF_REG24) + 4)
/*
 * $26 (k0) and $27 (k1) not saved
 */
#define TF_REG26 ((TF_REG25) + 4)
#define TF_REG27 ((TF_REG26) + 4)
#define TF_REG28 ((TF_REG27) + 4)
#define TF_REG29 ((TF_REG28) + 4)
#define TF_REG30 ((TF_REG29) + 4)
#define TF_REG31 ((TF_REG30) + 4)

#define TF_STATUS ((TF_REG31) + 4)

#define TF_HI ((TF_STATUS) + 4)
#define TF_LO ((TF_HI) + 4)

#define TF_BADVADDR ((TF_LO) + 4)
#define TF_CAUSE ((TF_BADVADDR) + 4)
#define TF_EPC ((TF_CAUSE) + 4)
/*
 * Size of stack frame, word/double word alignment
 */
#define TF_SIZE ((TF_EPC) + 4)
#endif /* _TRAP_H_ */
