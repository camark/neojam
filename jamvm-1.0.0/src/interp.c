/*
 * Copyright (C) 2003 Robert Lougher <rob@lougher.demon.co.uk>.
 *
 * This file is part of JamVM.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <math.h>

#include "jam.h"
#include "thread.h"
#include "lock.h"

#define CP_SINDEX(p)  p[1]
#define CP_DINDEX(p)  (p[1]<<8)|p[2]
#define BRANCH(p)     (((signed char)p[1])<<8)|p[2]
#define BRANCH_W(p)   (((signed char)p[1])<<24)|(p[2]<<16)|(p[3]<<8)|p[4]
#define DSIGNED(p)    (((signed char)p[1])<<8)|p[2]

#define THROW_EXCEPTION(excep_name, message)                          \
{                                                                     \
    frame->last_pc = (unsigned char*)pc;                              \
    signalException(excep_name, message);                             \
    goto throwException;                                              \
}

#define ZERO_DIVISOR_CHECK(TYPE, ostack)                              \
    if(((TYPE*)ostack)[-1] == 0)                                      \
        THROW_EXCEPTION("java/lang/ArithmeticException",              \
                        "division by zero");

#define NULL_POINTER_CHECK(ref)                                       \
    if(!ref)                                                          \
        THROW_EXCEPTION("java/lang/NullPointerException", NULL);

#define ARRAY_BOUNDS_CHECK(array, i)                                  \
    if(i >= *INST_DATA(array))                                        \
        THROW_OUT_OF_BOUNDS_EXCEPTION(i);

#define THROW_OUT_OF_BOUNDS_EXCEPTION(index)                          \
{                                                                     \
    char buff[256];                                                   \
    sprintf(buff, "%d", index);                                       \
    THROW_EXCEPTION("java/lang/ArrayIndexOutOfBoundsException",       \
                                                           buff);     \
}

#define UNARY_MINUS(TYPE, ostack, pc)                                 \
    ((TYPE*)ostack)[-1] = -((TYPE*)ostack)[-1];                       \
    pc += 1;                                                          \
    DISPATCH(pc)

#define BINARY_OP(TYPE, OP, ostack, pc)                               \
    ((TYPE*)ostack)[-2] = ((TYPE*)ostack)[-2] OP ((TYPE*)ostack)[-1]; \
    ostack -= sizeof(TYPE)/4;                                         \
    pc += 1;                                                          \
    DISPATCH(pc)

#define SHIFT_OP(TYPE, OP, ostack, pc)                                \
{                                                                     \
    int s = *--ostack;                                                \
    ((TYPE*)ostack)[-1] = ((TYPE*)ostack)[-1] OP s;                   \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define OPC_X2Y(SRC_TYPE, DEST_TYPE)                                  \
{                                                                     \
    SRC_TYPE v = *--(SRC_TYPE *)ostack;                               \
    *((DEST_TYPE *)ostack)++ = (DEST_TYPE)v;                          \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define ARRAY_LOAD(TYPE, ostack, pc)                                  \
{                                                                     \
    TYPE *element;                                                    \
    int i = ostack[-1];						      \
    Object *array = (Object *)ostack[-2];			      \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    element = (TYPE *)(((char *)INST_DATA(array)) +                   \
                              (i * sizeof(TYPE)) + 4);                \
    ostack[-2] = *element;					      \
    ostack -= 1;					              \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define ARRAY_LOAD_LONG(TYPE, ostack, pc)                             \
{                                                                     \
    u8 *element;                                                      \
    int i = ostack[-1];						      \
    Object *array = (Object *)ostack[-2];			      \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    element = (u8 *)(((char *)INST_DATA(array)) + (i << 3) + 4);      \
    ((u8*)ostack)[-1] = *element;                                     \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define ARRAY_STORE(TYPE, ostack, pc)                                 \
{                                                                     \
    int v = ostack[-1];						      \
    int i = ostack[-2];						      \
    Object *array = (Object *)ostack[-3];			      \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    *(TYPE *)(((char *)INST_DATA(array))+(i * sizeof(TYPE)) + 4) = v; \
    ostack -= 3;		 			              \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define ARRAY_STORE_LONG(TYPE, ostack, pc)                            \
{                                                                     \
    u8 v = ((u8*)ostack)[-1];					      \
    int i = ostack[-3];						      \
    Object *array = (Object *)ostack[-4];			      \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    *(u8 *)(((char *)INST_DATA(array)) + (i << 3) + 4) = v;           \
    ostack -= 4;					              \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define IF_ICMP(COND, ostack, pc)	                              \
{					                              \
    int v1 = ostack[-2];		                              \
    int v2 = ostack[-1];		                              \
    if(v1 COND v2) {			                              \
        pc += BRANCH(pc);		                              \
    } else 				                              \
        pc += 3;			                              \
    ostack -= 2;			                              \
    DISPATCH(pc)				                      \
}

#define IF(COND, ostack, pc)		                              \
{					                              \
    int v = *--ostack;			                              \
    if(v COND 0) {			                              \
        pc += BRANCH(pc);		                              \
    } else 				                              \
        pc += 3;			                              \
    DISPATCH(pc) 		                                      \
}

#define CMP(TYPE, ostack, pc)                                         \
{                                                                     \
    TYPE v2 = *--((TYPE *)ostack);                                    \
    TYPE v1 = *--((TYPE *)ostack);                                    \
    if(v1 == v2)                                                      \
        *ostack++ = 0;                                                \
    else if(v1 < v2)                                                  \
            *ostack++ = -1;                                           \
        else                                                          \
            *ostack++ = 1;                                            \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define FCMP(TYPE, ostack, pc, isNan)                                 \
{                                                                     \
    TYPE v2 = *--((TYPE *)ostack);                                    \
    TYPE v1 = *--((TYPE *)ostack);                                    \
    if(v1 == v2)                                                      \
        *ostack++ = 0;                                                \
    else if(v1 < v2)                                                  \
        *ostack++ = -1;                                               \
    else if(v1 > v2)                                                  \
         *ostack++ = 1;                                               \
    else                                                              \
         *ostack++ = isNan;                                           \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define WITH_OPCODE_CHANGE_CP_DINDEX(pc, opcode, index)               \
    index = CP_DINDEX(pc);                                            \
    if(pc[0] != opcode)                                               \
        DISPATCH(pc)

#define OPCODE_REWRITE(pc, opcode)                                    \
    pc[0] = opcode 

#define OPCODE_REWRITE_OPERAND1(pc, opcode, operand1)                 \
{                                                                     \
    pc[0] = OPC_LOCK;                                                 \
    pc[1] = operand1;                                                 \
    pc[0] = opcode;                                                   \
}

#define OPCODE_REWRITE_OPERAND2(pc, opcode, operand1, operand2)       \
{                                                                     \
    pc[0] = OPC_LOCK;                                                 \
    pc[1] = operand1;                                                 \
    pc[2] = operand2;                                                 \
    pc[0] = opcode;                                                   \
}

#ifdef THREADED
/* Two levels of macros are needed to correctly produce the label
 * from the OPC_xxx macro passed into DEF_OPC as cpp doesn't 
 * prescan when concatenating with ##...
 *
 * On gcc <= 2.95, we also get a space inserted before the :
 * e.g DEF_OPC(OPC_NULL) -> opc0 : - the ##: is a hack to fix
 * this, but this generates warnings on >= 2.96...
 */
#if (__GNUC__ == 2) && (__GNUC_MINOR__ <= 95)
#define label(x)         \
opc##x##:
#else
#define label(x)         \
opc##x:
#endif

#define DEF_OPC(opcode)  \
label(opcode)

#define DISPATCH(pc)     \
    goto *handlers[*pc];
#else
#define DEF_OPC(opcode)  \
    case opcode:

#define DISPATCH(pc)     \
    break;
#endif

u4 *executeJava() {
    ExecEnv *ee = getExecEnv();
    Frame *frame = ee->last_frame;
    MethodBlock *mb = frame->mb;
    u4 *lvars = frame->lvars;
    u4 *ostack = frame->ostack;
    volatile unsigned char *pc = mb->code;
    ConstantPool *cp = &(CLASS_CB(mb->class)->constant_pool);

    Object *this = (Object*)lvars[0];
    Class *new_class;
    MethodBlock *new_mb;
    u4 *arg1;

#ifdef THREADED
    static void *handlers[] = {
        &&opc0, &&opc1, &&opc2, &&opc3, &&opc4, &&opc5, &&opc6, &&opc7, &&opc8, &&opc9, &&opc10,
        &&opc11, &&opc12, &&opc13, &&opc14, &&opc15, &&opc16, &&opc17, &&opc18, &&opc19, &&opc20,
        &&opc21, &&opc22, &&opc23, &&opc24, &&opc25, &&opc26, &&opc27, &&opc28, &&opc29, &&opc30,
        &&opc31, &&opc32, &&opc33, &&opc34, &&opc35, &&opc36, &&opc37, &&opc38, &&opc39, &&opc40,
        &&opc41, &&opc42, &&opc43, &&opc44, &&opc45, &&opc46, &&opc47, &&opc48, &&opc49, &&opc50,
        &&opc51, &&opc52, &&opc53, &&opc54, &&opc55, &&opc56, &&opc57, &&opc58, &&opc59, &&opc60,
        &&opc61, &&opc62, &&opc63, &&opc64, &&opc65, &&opc66, &&opc67, &&opc68, &&opc69, &&opc70,
        &&opc71, &&opc72, &&opc73, &&opc74, &&opc75, &&opc76, &&opc77, &&opc78, &&opc79, &&opc80,
        &&opc81, &&opc82, &&opc83, &&opc84, &&opc85, &&opc86, &&opc87, &&opc88, &&opc89, &&opc90,
        &&opc91, &&opc92, &&opc93, &&opc94, &&opc95, &&opc96, &&opc97, &&opc98, &&opc99, &&opc100,
        &&opc101, &&opc102, &&opc103, &&opc104, &&opc105, &&opc106, &&opc107, &&opc108, &&opc109,
        &&opc110, &&opc111, &&opc112, &&opc113, &&opc114, &&opc115, &&opc116, &&opc117, &&opc118,
        &&opc119, &&opc120, &&opc121, &&opc122, &&opc123, &&opc124, &&opc125, &&opc126, &&opc127,
        &&opc128, &&opc129, &&opc130, &&opc131, &&opc132, &&opc133, &&opc134, &&opc135, &&opc136,
        &&opc137, &&opc138, &&opc139, &&opc140, &&opc141, &&opc142, &&opc143, &&opc144, &&opc145,
        &&opc146, &&opc147, &&opc148, &&opc149, &&opc150, &&opc151, &&opc152, &&opc153, &&opc154,
        &&opc155, &&opc156, &&opc157, &&opc158, &&opc159, &&opc160, &&opc161, &&opc162, &&opc163,
        &&opc164, &&opc165, &&opc166, &&opc167, &&opc168, &&opc169, &&opc170, &&opc171, &&opc172,
        &&opc173, &&opc174, &&opc175, &&opc176, &&opc177, &&opc178, &&opc179, &&opc180, &&opc181,
        &&opc182, &&opc183, &&opc184, &&opc185, &&unused, &&opc187, &&opc188, &&opc189, &&opc190,
        &&opc191, &&opc192, &&opc193, &&opc194, &&opc195, &&opc196, &&opc197, &&opc198, &&opc199,
        &&opc200, &&opc201, &&unused, &&opc203, &&opc204, &&unused, &&opc206, &&opc207, &&opc208,
        &&opc209, &&opc210, &&opc211, &&opc212, &&opc213, &&opc214, &&opc215, &&opc216, &&unused,
        &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&opc226,
        &&opc227, &&opc228, &&opc229, &&opc230, &&opc231, &&opc232, &&unused, &&unused, &&unused,
	&&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, 
	&&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, 
	&&unused, &&unused};

    DISPATCH(pc)

#else
    while(TRUE) {
        switch(*pc) {
            default:
#endif

unused:
    printf("Unrecognised opcode %d in: %s.%s\n", *pc, CLASS_CB(mb->class)->name, mb->name);
    exit(0);

    DEF_OPC(OPC_NOP)
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ACONST_NULL)
        *ostack++ = 0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_M1)
        *ostack++ = -1;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_0)
    DEF_OPC(OPC_FCONST_0)
        *ostack++ = 0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_1)
        *ostack++ = 1;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_2)
        *ostack++ = 2;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_3)
        *ostack++ = 3;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_4)
        *ostack++ = 4;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_5)
        *ostack++ = 5;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LCONST_0)
    DEF_OPC(OPC_DCONST_0)
        *((u8*)ostack)++ = 0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_FCONST_1)
        *((float*)ostack)++ = (float) 1.0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_FCONST_2)
        *((float*)ostack)++ = (float) 2.0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DCONST_1)
        *((double*)ostack)++ = (double) 1.0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LCONST_1)
        *((u8*)ostack)++ = 1;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_SIPUSH)
        *ostack++ = (signed short)((pc[1]<<8)+pc[2]);
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_BIPUSH)
        *ostack++ = (signed char)pc[1];
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC)
        *ostack++ = resolveSingleConstant(mb->class, CP_SINDEX(pc));
        OPCODE_REWRITE(pc, OPC_LDC_QUICK);
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC_QUICK)
        *ostack++ = CP_INFO(cp, CP_SINDEX(pc));
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC_W)
        *ostack++ = resolveSingleConstant(mb->class, CP_DINDEX(pc));
        OPCODE_REWRITE(pc, OPC_LDC_W_QUICK);
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC_W_QUICK)
        *ostack++ = CP_INFO(cp, CP_DINDEX(pc));
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC2_W)
        *((u8*)ostack)++ = CP_LONG(cp, CP_DINDEX(pc));
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD)
    DEF_OPC(OPC_FLOAD)
    DEF_OPC(OPC_ALOAD)
        *ostack++ = lvars[CP_SINDEX(pc)];
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD)
    DEF_OPC(OPC_DLOAD)
	*((u8*)ostack)++ = *(u8*)(&lvars[CP_SINDEX(pc)]);
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_ALOAD_0)
	if(mb->access_flags & ACC_STATIC)
            OPCODE_REWRITE(pc, OPC_ILOAD_0);
	else
            OPCODE_REWRITE(pc, OPC_ALOAD_THIS);
        DISPATCH(pc)

    DEF_OPC(OPC_ALOAD_THIS)
        if(pc[1] == OPC_GETFIELD_QUICK) {
            OPCODE_REWRITE(pc, OPC_GETFIELD_THIS);
	    DISPATCH(pc)
	}

    DEF_OPC(OPC_ILOAD_0)
    DEF_OPC(OPC_FLOAD_0)
        *ostack++ = lvars[0];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD_1)
    DEF_OPC(OPC_FLOAD_1)
    DEF_OPC(OPC_ALOAD_1)
        *ostack++ = lvars[1];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD_2)
    DEF_OPC(OPC_FLOAD_2)
    DEF_OPC(OPC_ALOAD_2)
        *ostack++ = lvars[2];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD_3)
    DEF_OPC(OPC_FLOAD_3)
    DEF_OPC(OPC_ALOAD_3)
        *ostack++ = lvars[3];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_0)
    DEF_OPC(OPC_DLOAD_0)
	*((u8*)ostack)++ = *(u8*)(&lvars[0]);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_1)
    DEF_OPC(OPC_DLOAD_1)
	*((u8*)ostack)++ = *(u8*)(&lvars[1]);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_2)
    DEF_OPC(OPC_DLOAD_2)
	*((u8*)ostack)++ = *(u8*)(&lvars[2]);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_3)
    DEF_OPC(OPC_DLOAD_3)
	*((u8*)ostack)++ = *(u8*)(&lvars[3]);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_IALOAD)
    DEF_OPC(OPC_AALOAD)
    DEF_OPC(OPC_FALOAD)
        ARRAY_LOAD(int, ostack, pc);

    DEF_OPC(OPC_LALOAD)
        ARRAY_LOAD_LONG(long long, ostack, pc);

    DEF_OPC(OPC_DALOAD)
        ARRAY_LOAD_LONG(long long, ostack, pc);

    DEF_OPC(OPC_BALOAD)
        ARRAY_LOAD(signed char, ostack, pc);

    DEF_OPC(OPC_CALOAD)
    DEF_OPC(OPC_SALOAD)
        ARRAY_LOAD(short, ostack, pc);

    DEF_OPC(OPC_LSTORE)
    DEF_OPC(OPC_DSTORE)
	*(u8*)(&lvars[CP_SINDEX(pc)]) = *--((u8*)ostack);
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE)
    DEF_OPC(OPC_FSTORE)
    DEF_OPC(OPC_ASTORE)
        lvars[CP_SINDEX(pc)] = *--ostack;
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_0)
    DEF_OPC(OPC_ASTORE_0)
    DEF_OPC(OPC_FSTORE_0)
        lvars[0] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_1)
    DEF_OPC(OPC_ASTORE_1)
    DEF_OPC(OPC_FSTORE_1)
        lvars[1] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_2)
    DEF_OPC(OPC_ASTORE_2)
    DEF_OPC(OPC_FSTORE_2)
        lvars[2] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_3)
    DEF_OPC(OPC_ASTORE_3)
    DEF_OPC(OPC_FSTORE_3)
        lvars[3] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_0)
    DEF_OPC(OPC_DSTORE_0)
        *(u8*)(&lvars[0]) = *--((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_1)
    DEF_OPC(OPC_DSTORE_1)
        *(u8*)(&lvars[1]) = *--((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_2)
    DEF_OPC(OPC_DSTORE_2)
        *(u8*)(&lvars[2]) = *--((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_3)
    DEF_OPC(OPC_DSTORE_3)
        *(u8*)(&lvars[3]) = *--((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_IASTORE)
    DEF_OPC(OPC_AASTORE)
    DEF_OPC(OPC_FASTORE)
        ARRAY_STORE(int, ostack, pc);

    DEF_OPC(OPC_LASTORE)
    DEF_OPC(OPC_DASTORE)
        ARRAY_STORE_LONG(double, ostack, pc);

    DEF_OPC(OPC_BASTORE)
        ARRAY_STORE(char, ostack, pc);

    DEF_OPC(OPC_CASTORE)
    DEF_OPC(OPC_SASTORE)
        ARRAY_STORE(short, ostack, pc);

    DEF_OPC(OPC_POP)
        ostack--;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_POP2)
        ostack -= 2;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DUP)
        *ostack++ = ostack[-1];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DUP_X1) {
        u4 word1 = ostack[-1];
        u4 word2 = ostack[-2];
        ostack[-2] = word1;
        ostack[-1] = word2;
        *ostack++ = word1;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DUP_X2) {
        u4 word1 = ostack[-1];
        u4 word2 = ostack[-2];
        u4 word3 = ostack[-3];
        ostack[-3] = word1;
        ostack[-2] = word3;
        ostack[-1] = word2;
        *ostack++ = word1;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DUP2)
        *((u8*)ostack)++ = ((u8*)ostack)[-1];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DUP2_X1) {
        u4 word1 = ostack[-1];
        u4 word2 = ostack[-2];
        u4 word3 = ostack[-3];
        ostack[-3] = word2;
        ostack[-2] = word1;
        ostack[-1] = word3;
        ostack[0]  = word2;
        ostack[1]  = word1;
        ostack += 2;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DUP2_X2) {
        u4 word1 = ostack[-1];
        u4 word2 = ostack[-2];
        u4 word3 = ostack[-3];
        u4 word4 = ostack[-4];
        ostack[-4] = word2;
        ostack[-3] = word1;
        ostack[-2] = word4;
        ostack[-1] = word3;
        ostack[0]  = word2;
        ostack[1]  = word1;
        ostack += 2;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_SWAP) {
        u4 word1 = ostack[-1];
        ostack[-1] = ostack[-2];
        ostack[-2] = word1;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_IADD)
        BINARY_OP(int, +, ostack, pc);

    DEF_OPC(OPC_LADD)
        BINARY_OP(long long, +, ostack, pc);

    DEF_OPC(OPC_FADD)
        BINARY_OP(float, +, ostack, pc);

    DEF_OPC(OPC_DADD)
        BINARY_OP(double, +, ostack, pc);

    DEF_OPC(OPC_ISUB)
        BINARY_OP(int, -, ostack, pc);

    DEF_OPC(OPC_LSUB)
        BINARY_OP(long long, -, ostack, pc);

    DEF_OPC(OPC_FSUB)
        BINARY_OP(float, -, ostack, pc);

    DEF_OPC(OPC_DSUB)
        BINARY_OP(double, -, ostack, pc);

    DEF_OPC(OPC_IMUL)
        BINARY_OP(int, *, ostack, pc);

    DEF_OPC(OPC_LMUL)
        BINARY_OP(long long, *, ostack, pc);

    DEF_OPC(OPC_FMUL)
        BINARY_OP(float, *, ostack, pc);

    DEF_OPC(OPC_DMUL)
        BINARY_OP(double, *, ostack, pc);

    DEF_OPC(OPC_IDIV)
	ZERO_DIVISOR_CHECK(int, ostack);
        BINARY_OP(int, /, ostack, pc);

    DEF_OPC(OPC_LDIV)
	ZERO_DIVISOR_CHECK(long long, ostack);
        BINARY_OP(long long, /, ostack, pc);

    DEF_OPC(OPC_FDIV)
        BINARY_OP(float, /, ostack, pc);

    DEF_OPC(OPC_DDIV)
        BINARY_OP(double, /, ostack, pc);

    DEF_OPC(OPC_IREM)
	ZERO_DIVISOR_CHECK(int, ostack);
        BINARY_OP(int, %, ostack, pc);

    DEF_OPC(OPC_LREM)
	ZERO_DIVISOR_CHECK(long long, ostack);
        BINARY_OP(long long, %, ostack, pc);

    DEF_OPC(OPC_FREM) {
        float v2 = *--((float *)ostack);
        float v1 = *--((float *)ostack);

        *((float *)ostack)++ = fmod(v1, v2);
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DREM) {
        double v2 = *--((double *)ostack);
        double v1 = *--((double *)ostack);

        *((double *)ostack)++ = fmod(v1, v2);
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INEG)
        UNARY_MINUS(int, ostack, pc);

    DEF_OPC(OPC_LNEG)
        UNARY_MINUS(long long, ostack, pc);

    DEF_OPC(OPC_FNEG)
        UNARY_MINUS(float, ostack, pc);

    DEF_OPC(OPC_DNEG)
        UNARY_MINUS(double, ostack, pc);

    DEF_OPC(OPC_ISHL)
        BINARY_OP(int, <<, ostack, pc);

    DEF_OPC(OPC_LSHL)
        SHIFT_OP(long long, <<, ostack, pc);

    DEF_OPC(OPC_ISHR)
        BINARY_OP(int, >>, ostack, pc);

    DEF_OPC(OPC_LSHR)
        SHIFT_OP(long long, >>, ostack, pc);

    DEF_OPC(OPC_IUSHR)
        SHIFT_OP(unsigned int, >>, ostack, pc);

    DEF_OPC(OPC_LUSHR)
        SHIFT_OP(unsigned long long, >>, ostack, pc);

    DEF_OPC(OPC_IAND)
        BINARY_OP(int, &, ostack, pc);

    DEF_OPC(OPC_LAND)
        BINARY_OP(long long, &, ostack, pc);

    DEF_OPC(OPC_IOR)
        BINARY_OP(int, |, ostack, pc);

    DEF_OPC(OPC_LOR)
        BINARY_OP(long long, |, ostack, pc);

    DEF_OPC(OPC_IXOR)
        BINARY_OP(int, ^, ostack, pc);

    DEF_OPC(OPC_LXOR)
        BINARY_OP(long long, ^, ostack, pc);

    DEF_OPC(OPC_IINC)
        lvars[CP_SINDEX(pc)] += (signed char)pc[2];
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_I2L)
        OPC_X2Y(int, long long);

    DEF_OPC(OPC_I2F)
        OPC_X2Y(int, float);

    DEF_OPC(OPC_I2D)
        OPC_X2Y(int, double);

    DEF_OPC(OPC_L2I)
        OPC_X2Y(long long, int);

    DEF_OPC(OPC_L2F)
        OPC_X2Y(long long, float);

    DEF_OPC(OPC_L2D)
        OPC_X2Y(long long, double);

    DEF_OPC(OPC_F2I)
        OPC_X2Y(float, int);

    DEF_OPC(OPC_F2L)
        OPC_X2Y(float, long long);

    DEF_OPC(OPC_F2D)
        OPC_X2Y(float, double);

    DEF_OPC(OPC_D2I)
        OPC_X2Y(double, int);

    DEF_OPC(OPC_D2L)
        OPC_X2Y(double, long long);

    DEF_OPC(OPC_D2F)
        OPC_X2Y(double, float);

    DEF_OPC(OPC_I2B)
    {
        signed char v = ostack[-1] & 0xff;
        ostack[-1] = (int) v;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_I2C)
    {
        int v = ostack[-1] & 0xffff;
        ostack[-1] = v;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_I2S)
    {
        signed short v = ostack[-1] & 0xffff;
        ostack[-1] = (int) v;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_LCMP)
        CMP(long long, ostack, pc);

    DEF_OPC(OPC_DCMPG)
    DEF_OPC(OPC_DCMPL)
        FCMP(double, ostack, pc, (*pc == OPC_DCMPG ? 1 : -1));

    DEF_OPC(OPC_FCMPG)
    DEF_OPC(OPC_FCMPL)
        FCMP(float, ostack, pc, (*pc == OPC_FCMPG ? 1 : -1));

    DEF_OPC(OPC_IFEQ)
        IF(==, ostack, pc);

    DEF_OPC(OPC_IFNE)
        IF(!=, ostack, pc);

    DEF_OPC(OPC_IFLT)
        IF(<, ostack, pc);

    DEF_OPC(OPC_IFGE)
        IF(>=, ostack, pc);

    DEF_OPC(OPC_IFGT)
        IF(>, ostack, pc);

    DEF_OPC(OPC_IFLE)
        IF(<=, ostack, pc);

    DEF_OPC(OPC_IF_ACMPEQ)
    DEF_OPC(OPC_IF_ICMPEQ)
	IF_ICMP(==, ostack, pc);

    DEF_OPC(OPC_IF_ACMPNE)
    DEF_OPC(OPC_IF_ICMPNE)
	IF_ICMP(!=, ostack, pc);

    DEF_OPC(OPC_IF_ICMPLT)
	IF_ICMP(<, ostack, pc);

    DEF_OPC(OPC_IF_ICMPGE)
	IF_ICMP(>=, ostack, pc);

    DEF_OPC(OPC_IF_ICMPGT)
	IF_ICMP(>, ostack, pc);

    DEF_OPC(OPC_IF_ICMPLE)
	IF_ICMP(<=, ostack, pc);

    DEF_OPC(OPC_GOTO)
        pc += BRANCH(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_JSR)
        *ostack++ = (u4)pc+3;
        pc += BRANCH(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_RET)
        pc = (unsigned char*)lvars[CP_SINDEX(pc)];
        DISPATCH(pc)

    DEF_OPC(OPC_TABLESWITCH)
    {
        int *aligned_pc = (int*)((int)(pc + 4) & ~0x3);
        int deflt = ntohl(aligned_pc[0]);
        int low   = ntohl(aligned_pc[1]);
        int high  = ntohl(aligned_pc[2]);
        int index = *--ostack;

        if(index < low || index > high)
            pc += deflt;
        else
            pc += ntohl(aligned_pc[index - low + 3]);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_LOOKUPSWITCH)
    {
        int *aligned_pc = (int*)((int)(pc + 4) & ~0x3);
        int deflt  = ntohl(aligned_pc[0]);
        int npairs = ntohl(aligned_pc[1]);
        int key    = *--ostack;
        int i;

        for(i = 2; (i < npairs*2+2) && (key != ntohl(aligned_pc[i])); i += 2);

        if(i == npairs*2+2)
            pc += deflt;
        else
            pc += ntohl(aligned_pc[i+1]);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_IRETURN)
    DEF_OPC(OPC_ARETURN)
    DEF_OPC(OPC_FRETURN)
        *lvars++ = *--ostack;
        goto methodReturn;

    DEF_OPC(OPC_LRETURN)
    DEF_OPC(OPC_DRETURN)
	*((u8*)lvars)++ = *(--(u8*)ostack);
        goto methodReturn;

    DEF_OPC(OPC_RETURN)
        goto methodReturn;

    DEF_OPC(OPC_GETSTATIC) 
    {
        FieldBlock *fb;
	       
        frame->last_pc = (unsigned char*)pc;
	fb = resolveField(mb->class, CP_DINDEX(pc));

        if(exceptionOccured0(ee))
            goto throwException;

        if((*fb->type == 'J') || (*fb->type == 'D'))
            OPCODE_REWRITE(pc, OPC_GETSTATIC2_QUICK);
        else
            OPCODE_REWRITE(pc, OPC_GETSTATIC_QUICK);
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTSTATIC) 
    {
        FieldBlock *fb;
	       
        frame->last_pc = (unsigned char*)pc;
	fb = resolveField(mb->class, CP_DINDEX(pc));

        if(exceptionOccured0(ee))
            goto throwException;

        if((*fb->type == 'J') || (*fb->type == 'D'))
            OPCODE_REWRITE(pc, OPC_PUTSTATIC2_QUICK);
        else
            OPCODE_REWRITE(pc, OPC_PUTSTATIC_QUICK);
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETSTATIC_QUICK) 
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        *ostack++ = fb->static_value;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETSTATIC2_QUICK) 
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        *(u8*)ostack = *(u8*)&fb->static_value;
        ostack += 2;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTSTATIC_QUICK) 
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        fb->static_value = *--ostack;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTSTATIC2_QUICK) 
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        ostack -= 2;
        *(u8*)&fb->static_value = *(u8*)ostack;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETFIELD)
    {
        int idx;
        FieldBlock *fb;

        WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_GETFIELD, idx);

        frame->last_pc = (unsigned char*)pc;
        fb = resolveField(mb->class, idx);

        if(exceptionOccured0(ee))
            goto throwException;

        if(fb->offset > 255)
            OPCODE_REWRITE(pc, OPC_GETFIELD_QUICK_W);
        else
            OPCODE_REWRITE_OPERAND1(pc, ((*fb->type == 'J') || (*fb->type == 'D') ? 
                 OPC_GETFIELD2_QUICK : OPC_GETFIELD_QUICK), fb->offset);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD)
    {
        int idx;
        FieldBlock *fb;

	WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_PUTFIELD, idx);

        frame->last_pc = (unsigned char*)pc;
        fb = resolveField(mb->class, idx);

        if(exceptionOccured0(ee))
            goto throwException;

        if(fb->offset > 255)
            OPCODE_REWRITE(pc, OPC_PUTFIELD_QUICK_W);
        else
            OPCODE_REWRITE_OPERAND1(pc, ((*fb->type == 'J') || (*fb->type == 'D') ? 
                 OPC_PUTFIELD2_QUICK : OPC_PUTFIELD_QUICK), fb->offset);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETFIELD_QUICK)
    {
        Object *o = (Object *)ostack[-1];
	NULL_POINTER_CHECK(o);
		
        ostack[-1] = INST_DATA(o)[pc[1]];
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETFIELD_THIS)
        *ostack++ = INST_DATA(this)[pc[2]];
        pc += 4;
        DISPATCH(pc)

    DEF_OPC(OPC_GETFIELD2_QUICK)
    {
        Object *o = (Object *)*--ostack;
	NULL_POINTER_CHECK(o);
		
        *((u8*)ostack)++ = *(u8*)(&(INST_DATA(o)[pc[1]]));
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETFIELD_QUICK_W)
    {
        FieldBlock *fb = (FieldBlock*)CP_INFO(cp, CP_DINDEX(pc));
        Object *o = (Object *)*--ostack;
        u4 *addr;

	NULL_POINTER_CHECK(o);
		
        addr = &(INST_DATA(o)[fb->offset]);

        if((*fb->type == 'J') || (*fb->type == 'D')) {
            u8 v = *(u8*)addr;
            *(u8*)ostack = v;
            ostack += 2;
        } else {
            u4 v = *addr;
            *ostack++ = v; 
        }

        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD_QUICK_W)
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        Object *o;
 
        if((*fb->type == 'J') || (*fb->type == 'D')) {
            u8 v, *addr;
            ostack -= 2;
            v = *(u8*)ostack;
            o = (Object *)*--ostack;
	    NULL_POINTER_CHECK(o);

            addr = (u8*)&(INST_DATA(o)[fb->offset]);
            *addr = v;
        } else {
            u4 *addr, v = *--ostack;
            o = (Object *)*--ostack;
	    NULL_POINTER_CHECK(o);

            addr = &(INST_DATA(o)[fb->offset]);
            *addr = v;
        }

        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD_QUICK)
    {
        Object *o = (Object *)ostack[-2];
	NULL_POINTER_CHECK(o);
		
        INST_DATA(o)[pc[1]] = ostack[-1];
        ostack -= 2;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD2_QUICK)
    {
        Object *o = (Object *)ostack[-3];
	NULL_POINTER_CHECK(o);
		
        *(u8*)(&(INST_DATA(o)[pc[1]])) = *(u8*)(&ostack[-2]);
        ostack -= 3;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INVOKEVIRTUAL)
    {
        int idx;
        WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_INVOKEVIRTUAL, idx);

        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveMethod(mb->class, idx);
 
        if(exceptionOccured0(ee))
            goto throwException;

        if((new_mb->args_count < 256) && (new_mb->method_table_index < 256)) {
            OPCODE_REWRITE_OPERAND2(pc, OPC_INVOKEVIRTUAL_QUICK, new_mb->method_table_index, new_mb->args_count);
	} else
            OPCODE_REWRITE(pc, OPC_INVOKEVIRTUAL_QUICK_W);
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INVOKEVIRTUAL_QUICK_W)
        new_mb = (MethodBlock *)CP_INFO(cp, CP_DINDEX(pc));
        arg1 = ostack - (new_mb->args_count);
	NULL_POINTER_CHECK(*arg1);

        new_class = (*(Object **)arg1)->class;
        new_mb = CLASS_CB(new_class)->method_table[new_mb->method_table_index];

        goto invokeMethod;

    DEF_OPC(OPC_INVOKESPECIAL)
    {
        int idx;
        WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_INVOKESPECIAL, idx);

        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveMethod(mb->class, idx);
 
        if(exceptionOccured0(ee))
            goto throwException;

        /* Check if invoking a super method... */
	if((CLASS_CB(mb->class)->access_flags & ACC_SUPER) &&
              ((new_mb->access_flags & ACC_PRIVATE) == 0) && (new_mb->name[0] != '<')) {
            OPCODE_REWRITE_OPERAND2(pc, OPC_INVOKESUPER_QUICK,
                    new_mb->method_table_index >> 8,
                    new_mb->method_table_index & 0xff);
	} else
            OPCODE_REWRITE(pc, OPC_INVOKENONVIRTUAL_QUICK);
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INVOKESUPER_QUICK)
        new_mb = CLASS_CB(CLASS_CB(mb->class)->super)->method_table[CP_DINDEX(pc)];
        arg1 = ostack - (new_mb->args_count);
	NULL_POINTER_CHECK(*arg1);
	goto invokeMethod;

    DEF_OPC(OPC_INVOKENONVIRTUAL_QUICK)
        new_mb = (MethodBlock *)CP_INFO(cp, CP_DINDEX(pc));
        arg1 = ostack - (new_mb->args_count);
	NULL_POINTER_CHECK(*arg1);
	goto invokeMethod;

    DEF_OPC(OPC_INVOKESTATIC)
        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveMethod(mb->class, CP_DINDEX(pc));
 
        if(exceptionOccured0(ee))
            goto throwException;

        OPCODE_REWRITE(pc, OPC_INVOKESTATIC_QUICK);
        DISPATCH(pc)

        DEF_OPC(OPC_INVOKESTATIC_QUICK)
        new_mb = (MethodBlock *)CP_INFO(cp, CP_DINDEX(pc));
        arg1 = ostack - new_mb->args_count;
        goto invokeMethod;

        DEF_OPC(OPC_INVOKEINTERFACE)
        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveInterfaceMethod(mb->class, CP_DINDEX(pc));
 
        if(exceptionOccured0(ee))
            goto throwException;

        arg1 = ostack - (new_mb->args_count);
	NULL_POINTER_CHECK(*arg1);

        new_class = (*(Object **)arg1)->class;
	new_mb = lookupMethod(new_class, new_mb->name, new_mb->type);

        goto invokeMethod;

    DEF_OPC(OPC_ARRAYLENGTH)
    {
        Object *array = (Object *)ostack[-1];
	NULL_POINTER_CHECK(array);

        ostack[-1] = *INST_DATA(array);
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_ATHROW)
    {
        Object *ob = (Object *)ostack[-1];
        frame->last_pc = (unsigned char*)pc;
	NULL_POINTER_CHECK(ob);
		
        ee->exception = ob;
        goto throwException;
    }

    DEF_OPC(OPC_NEW)
    {
        Class *class;
        Object *ob;
 
        frame->last_pc = (unsigned char*)pc;
        class = resolveClass(mb->class, CP_DINDEX(pc), TRUE);

        if(exceptionOccured0(ee))
            goto throwException;
        
        if((ob = allocObject(class)) == NULL)
            goto throwException;

        *ostack++ = (u4)ob;
        pc += 3;
        DISPATCH(pc)
    }
 
    DEF_OPC(OPC_NEWARRAY)
    {
        int type = pc[1];
        int count = *--ostack;
        Object *ob;

        frame->last_pc = (unsigned char*)pc;
        if((ob = allocTypeArray(type, count)) == NULL)
            goto throwException;

        *ostack++ = (u4)ob;
        pc += 2;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_ANEWARRAY)
    {
        Class *array_class;
        char ac_name[256];
        int count = ostack[-1];
        Class *class;
        Object *ob;
 
        frame->last_pc = (unsigned char*)pc;
        class = resolveClass(mb->class, CP_DINDEX(pc), FALSE);

        if(exceptionOccured0(ee))
            goto throwException;

        if(CLASS_CB(class)->name[0] == '[')
            strcat(strcpy(ac_name, "["), CLASS_CB(class)->name);
        else
            strcat(strcat(strcpy(ac_name, "[L"), CLASS_CB(class)->name), ";");

        array_class = findArrayClass(ac_name);

        if(exceptionOccured0(ee))
            goto throwException;

        if((ob = allocArray(array_class, count, 4)) == NULL)
            goto throwException;

        ostack[-1] = (u4)ob;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_CHECKCAST)
    {
        Object *obj = (Object*)ostack[-1]; 
        Class *class;
	       
        frame->last_pc = (unsigned char*)pc;
	class = resolveClass(mb->class, CP_DINDEX(pc), TRUE);
 
        if(exceptionOccured0(ee))
            goto throwException;

        if((obj != NULL) && !isInstanceOf(class, obj->class))
            THROW_EXCEPTION("java/lang/ClassCastException", CLASS_CB(obj->class)->name);
    
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INSTANCEOF)
    {
        Object *obj = (Object*)ostack[-1]; 
        Class *class;
	       
        frame->last_pc = (unsigned char*)pc;
	class = resolveClass(mb->class, CP_DINDEX(pc), FALSE);

        if(exceptionOccured0(ee))
            goto throwException;

        if(obj != NULL)
            ostack[-1] = isInstanceOf(class, obj->class); 
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_MONITORENTER)
    {
        Object *ob = (Object *)*--ostack;
	NULL_POINTER_CHECK(ob);
	objectLock(ob);
        pc += 1;
	DISPATCH(pc)
    }

    DEF_OPC(OPC_MONITOREXIT)
    {
        Object *ob = (Object *)*--ostack;
	NULL_POINTER_CHECK(ob);
	objectUnlock(ob);
        pc += 1;
	DISPATCH(pc)
    }

    DEF_OPC(OPC_WIDE)
    {
       int opcode = pc[1];
        switch(opcode) {
            case OPC_ILOAD:
            case OPC_FLOAD:
            case OPC_ALOAD:
                *ostack++ = lvars[CP_DINDEX((pc+1))];
                pc += 4;
		break;

            case OPC_LLOAD:
            case OPC_DLOAD:
                *((u8*)ostack)++ = *(u8*)(&lvars[CP_DINDEX((pc+1))]);
                pc += 4;
		break;

            case OPC_ISTORE:
            case OPC_FSTORE:
            case OPC_ASTORE:
                lvars[CP_DINDEX((pc+1))] = *--ostack;
                pc += 4;
		break;

            case OPC_LSTORE:
            case OPC_DSTORE:
                *(u8*)(&lvars[CP_DINDEX((pc+1))]) = *--((u8*)ostack);
                pc += 4;
		break;

            case OPC_RET:
                pc = (unsigned char*)lvars[CP_DINDEX((pc+1))];
		break;

            case OPC_IINC:
                lvars[CP_DINDEX((pc+1))] += DSIGNED((pc+3));
                pc += 6;
		break;
        }
        DISPATCH(pc)
   }

    DEF_OPC(OPC_MULTIANEWARRAY)
    {
        Class *class;
        int dim = pc[3];
	Object *ob;

        frame->last_pc = (unsigned char*)pc;
        class = resolveClass(mb->class, CP_DINDEX(pc), FALSE);

        if(exceptionOccured0(ee))
            goto throwException;

        ostack -= dim;

        if((ob = allocMultiArray(class, dim, ostack)) == NULL)
            goto throwException;

        *ostack++ = (u4)ob;
        pc += 4;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_IFNULL)
    {
        int v = *--ostack;
        if(v == 0) {
           pc += BRANCH(pc);
        } else 
           pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_IFNONNULL)
    {
        int v = *--ostack;
        if(v != 0) {
           pc += BRANCH(pc);
        } else 
           pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GOTO_W)
        pc += BRANCH_W(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_JSR_W)
        *ostack++ = (u4)pc+3;
        pc += BRANCH_W(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_LOCK)
        DISPATCH(pc)

    DEF_OPC(OPC_INVOKEVIRTUAL_QUICK)
        arg1 = ostack - pc[2];

	NULL_POINTER_CHECK(*arg1);

        new_class = (*(Object **)arg1)->class;
        new_mb = CLASS_CB(new_class)->method_table[pc[1]];

invokeMethod:
{
    /* Create new frame first.  This is also created for natives
       so that they appear correctly in the stack trace */

    Frame *new_frame = (Frame *)(arg1 + new_mb->max_locals);
    Object *sync_ob = NULL;

    if((char*)new_frame > ee->stack_end) {
        ee->stack_end += 1024;
        THROW_EXCEPTION("java/lang/StackOverflowError", NULL);
    }

    new_frame->mb = new_mb;
    new_frame->lvars = arg1;
    new_frame->ostack = (u4*)(new_frame+1);
    new_frame->prev = frame;
    frame->last_pc = (unsigned char*)pc;

    ee->last_frame = new_frame;

    if(new_mb->access_flags & ACC_SYNCHRONIZED) {
        sync_ob = new_mb->access_flags & ACC_STATIC ? (Object*)new_mb->class : (Object*)*arg1;
	objectLock(sync_ob);
    }

    if(new_mb->access_flags & ACC_NATIVE) {
        ostack = (*(u4 *(*)(Class*, MethodBlock*, u4*))new_mb->native_invoker)(new_mb->class, new_mb, arg1);

	if(sync_ob)
	    objectUnlock(sync_ob);

        ee->last_frame = frame;

	if(exceptionOccured0(ee))
            goto throwException;
	pc += *pc == OPC_INVOKEINTERFACE ? 5 : 3;
    } else {
        frame = new_frame;
        mb = new_mb;
        lvars = new_frame->lvars;
        this = (Object*)lvars[0];
        ostack = new_frame->ostack;
        pc = mb->code;
        cp = &(CLASS_CB(mb->class)->constant_pool);
    }
    DISPATCH(pc)
}

methodReturn:
    /* Set interpreter state to previous frame */

    frame = frame->prev;

    if(frame->mb == NULL) {
        /* The previous frame is a dummy frame - this indicates
           top of this Java invocation. */
        return ostack;
    }

    if(mb->access_flags & ACC_SYNCHRONIZED) {
        Object *sync_ob = mb->access_flags & ACC_STATIC ? (Object*)mb->class : this;
	objectUnlock(sync_ob);
    }

    mb = frame->mb;
    ostack = lvars;
    lvars = frame->lvars;
    this = (Object*)lvars[0];
    pc = frame->last_pc;
    pc += *pc == OPC_INVOKEINTERFACE ? 5 : 3;
    cp = &(CLASS_CB(mb->class)->constant_pool);

    /* Pop frame */ 
    ee->last_frame = frame;
    DISPATCH(pc)

throwException:
    {
        Object *excep = ee->exception;
        clearException();

        pc = findCatchBlock(excep->class);

        if(pc == NULL) {
            ee->exception = excep;
            return;
        }

        frame = ee->last_frame;
        mb = frame->mb;
        ostack = frame->ostack;
        lvars = frame->lvars;
        this = (Object*)lvars[0];
        cp = &(CLASS_CB(mb->class)->constant_pool);

        *ostack++ = (u4)excep;
        DISPATCH(pc)
    }
#ifndef THREADED
  }}
#endif
}
