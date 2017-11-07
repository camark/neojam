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
#include "jam.h"
#include "frame.h"

char *convertClassArray2Sig(Object *array) {
    int count = 0, len;
    char *sig, *pntr;
    u4 *data;

    /* Calculate size of signature first */

    for(data = INST_DATA(array), len = *data++; len > 0; len--) {
        ClassBlock *cb = CLASS_CB((Class*)*data++);
	int nlen = strlen(cb->name);
	count += cb->flags == CLASS_INTERNAL ? nlen : nlen+2;
    }

    sig = pntr = (char*)malloc(count + 1);

    /* Loop through again, and construct signature - "normal"
       loaded class names need wrapping but internally
       created class names (arrays and prim classes) are
       already in signature form... */

    for(data = INST_DATA(array), len = *data++; len > 0; len--) {
        ClassBlock *cb = CLASS_CB((Class*)*data++);
        pntr += sprintf(pntr, cb->flags == CLASS_INTERNAL ? "%s" : "L%s;", cb->name);
    }

    *pntr == '\0';
    return sig;
}

Object *createConstructor(Class *class, char *sig) {
    Object *constructor = NULL;
    MethodBlock *mb;

    mb = findMethod(class, "<init>", sig);

    if(mb != NULL) {
        Class *con_class = findSystemClass("java/lang/reflect/Constructor");
        if(con_class != NULL) {
            MethodBlock *init = findMethod(con_class, "<init>", "(Ljava/lang/Class;I)V");
            constructor = allocObject(con_class);

	    executeMethod(constructor, init, class, mb);
        }
    }

    return constructor;
}

Object *getClassConstructor(Class *class, Object *arg_list) {
    Object *constructor;

    if(arg_list) {
        char *params = convertClassArray2Sig(arg_list);
        char *sig = (char*)malloc(strlen(params)+4);

        sprintf(sig, "(%s)V", params);
        constructor = createConstructor(class, sig);

        free(sig);
        free(params);
    } else
        constructor = createConstructor(class, "()V");

    return constructor;
}

Object *invoke(Object *ob, Class *class, MethodBlock *mb, Object *arg_array) {
    Object **args = (Object**)(INST_DATA(arg_array)+1);
    char *sig = mb->type;

    ExecEnv *ee = getExecEnv();
    void *ret;
    u4 *sp;

    CREATE_TOP_FRAME(ee, class, mb, sp, ret);

    if(ob) *sp++ = (u4)ob;

    while(*++sig != ')')
        switch(*sig) {
            case 'J':
            case 'D':
                *((u8*)sp)++ = *(u8*)(INST_DATA(*args++));
                break;

            case 'Z':
            case 'B':
            case 'C':
            case 'I':
            case 'F':
                *sp++ = *(INST_DATA(*args++));
                break;

            default:
		*sp++ = (u4) *args++;

                if(*sig == '[')
                    while(*++sig == '[');
                if(*sig == 'L')
                    while(*++sig != ';');
                break;
        }

    if(mb->access_flags & ACC_SYNCHRONIZED)
        objectLock(ob ? ob : (Object*)mb->class);

    if(mb->access_flags & ACC_NATIVE)
        (*(u4 *(*)(Class*, MethodBlock*, u4*))mb->native_invoker)(class, mb, ret);
    else
        executeJava();

    if(mb->access_flags & ACC_SYNCHRONIZED)
        objectUnlock(ob ? ob : (Object*)mb->class);

    POP_TOP_FRAME(ee);
    return ret;
}
