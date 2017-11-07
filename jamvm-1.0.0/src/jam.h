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

#include <stdarg.h>

#ifndef	TRUE
#define		TRUE	1
#define 	FALSE	0
#endif

/* These should go in the interpreter file */

#define OPC_NOP				0
#define OPC_ACONST_NULL			1
#define OPC_ICONST_M1			2
#define OPC_ICONST_0			3
#define OPC_ICONST_1			4
#define OPC_ICONST_2			5
#define OPC_ICONST_3			6
#define OPC_ICONST_4			7
#define OPC_ICONST_5			8
#define OPC_LCONST_0			9
#define OPC_LCONST_1			10
#define OPC_FCONST_0			11
#define OPC_FCONST_1			12
#define OPC_FCONST_2			13
#define OPC_DCONST_0			14
#define OPC_DCONST_1			15
#define OPC_BIPUSH			16
#define OPC_SIPUSH			17
#define OPC_LDC				18
#define OPC_LDC_W			19
#define OPC_LDC2_W			20
#define OPC_ILOAD			21
#define OPC_LLOAD			22
#define OPC_FLOAD			23
#define OPC_DLOAD			24
#define OPC_ALOAD			25
#define OPC_ILOAD_0			26
#define OPC_ILOAD_1			27
#define OPC_ILOAD_2			28
#define OPC_ILOAD_3			29
#define OPC_LLOAD_0			30
#define OPC_LLOAD_1			31
#define OPC_LLOAD_2			32
#define OPC_LLOAD_3			33
#define OPC_FLOAD_0			34
#define OPC_FLOAD_1			35
#define OPC_FLOAD_2			36
#define OPC_FLOAD_3			37
#define OPC_DLOAD_0			38
#define OPC_DLOAD_1			39
#define OPC_DLOAD_2			40
#define OPC_DLOAD_3			41
#define OPC_ALOAD_0			42
#define OPC_ALOAD_1			43
#define OPC_ALOAD_2			44
#define OPC_ALOAD_3			45
#define OPC_IALOAD			46
#define OPC_LALOAD			47
#define OPC_FALOAD			48
#define OPC_DALOAD			49
#define OPC_AALOAD			50
#define OPC_BALOAD			51
#define OPC_CALOAD			52
#define OPC_SALOAD			53
#define OPC_ISTORE			54
#define	OPC_LSTORE			55
#define	OPC_FSTORE			56
#define	OPC_DSTORE			57
#define OPC_ASTORE			58
#define OPC_ISTORE_0			59
#define OPC_ISTORE_1			60
#define OPC_ISTORE_2			61
#define OPC_ISTORE_3			62
#define OPC_LSTORE_0			63
#define OPC_LSTORE_1			64
#define OPC_LSTORE_2			65
#define OPC_LSTORE_3			66
#define OPC_FSTORE_0			67
#define OPC_FSTORE_1			68
#define OPC_FSTORE_2			69
#define OPC_FSTORE_3			70
#define OPC_DSTORE_0			71
#define OPC_DSTORE_1			72
#define OPC_DSTORE_2			73
#define OPC_DSTORE_3			74
#define OPC_ASTORE_0			75
#define OPC_ASTORE_1			76
#define OPC_ASTORE_2			77
#define OPC_ASTORE_3			78
#define OPC_IASTORE			79
#define OPC_LASTORE			80
#define OPC_FASTORE			81
#define OPC_DASTORE			82
#define OPC_AASTORE			83
#define OPC_BASTORE			84
#define OPC_CASTORE			85
#define OPC_SASTORE			86
#define OPC_POP				87
#define OPC_POP2			88
#define OPC_DUP				89
#define OPC_DUP_X1			90
#define OPC_DUP_X2			91
#define OPC_DUP2			92
#define OPC_DUP2_X1			93
#define OPC_DUP2_X2			94
#define OPC_SWAP			95
#define OPC_IADD			96
#define OPC_LADD			97
#define OPC_FADD			98
#define OPC_DADD			99
#define OPC_ISUB			100
#define OPC_LSUB			101
#define OPC_FSUB			102
#define OPC_DSUB			103
#define OPC_IMUL			104
#define OPC_LMUL			105
#define OPC_FMUL			106
#define OPC_DMUL			107
#define OPC_IDIV			108
#define OPC_LDIV			109
#define OPC_FDIV			110
#define OPC_DDIV			111
#define OPC_IREM			112
#define OPC_LREM			113
#define OPC_FREM			114
#define OPC_DREM			115
#define OPC_INEG			116
#define OPC_LNEG			117
#define OPC_FNEG			118
#define OPC_DNEG			119
#define OPC_ISHL			120
#define OPC_LSHL			121
#define OPC_ISHR			122
#define OPC_LSHR			123
#define OPC_IUSHR			124
#define OPC_LUSHR			125
#define OPC_IAND			126
#define OPC_LAND			127
#define OPC_IOR				128	
#define OPC_LOR				129	
#define OPC_IXOR			130	
#define OPC_LXOR			131	
#define OPC_IINC			132
#define OPC_I2L				133
#define OPC_I2F				134
#define OPC_I2D				135
#define OPC_L2I				136
#define OPC_L2F                         137
#define OPC_L2D				138
#define OPC_F2I				139
#define OPC_F2L				140
#define OPC_F2D				141
#define OPC_D2I				142
#define OPC_D2L				143
#define OPC_D2F				144
#define OPC_I2B				145
#define OPC_I2C				146
#define OPC_I2S				147
#define OPC_LCMP			148
#define OPC_FCMPL			149
#define OPC_FCMPG			150
#define OPC_DCMPL			151
#define OPC_DCMPG			152
#define OPC_IFEQ			153
#define OPC_IFNE			154
#define OPC_IFLT			155
#define OPC_IFGE			156
#define OPC_IFGT			157
#define OPC_IFLE			158
#define OPC_IF_ICMPEQ			159
#define OPC_IF_ICMPNE			160
#define OPC_IF_ICMPLT			161
#define OPC_IF_ICMPGE			162
#define OPC_IF_ICMPGT			163
#define OPC_IF_ICMPLE			164
#define OPC_IF_ACMPEQ			165
#define OPC_IF_ACMPNE			166
#define OPC_GOTO			167
#define OPC_JSR                         168
#define OPC_RET                         169
#define OPC_TABLESWITCH			170
#define OPC_LOOKUPSWITCH		171
#define OPC_IRETURN			172
#define OPC_LRETURN			173
#define OPC_FRETURN			174
#define OPC_DRETURN			175
#define OPC_ARETURN			176
#define OPC_RETURN			177
#define OPC_GETSTATIC			178
#define OPC_PUTSTATIC			179
#define OPC_GETFIELD			180
#define OPC_PUTFIELD			181
#define OPC_INVOKEVIRTUAL		182
#define OPC_INVOKESPECIAL		183
#define OPC_INVOKESTATIC		184
#define OPC_INVOKEINTERFACE		185
#define OPC_NEW				187
#define OPC_NEWARRAY			188
#define OPC_ANEWARRAY			189
#define OPC_ARRAYLENGTH			190
#define OPC_ATHROW			191
#define OPC_CHECKCAST			192
#define OPC_INSTANCEOF			193
#define OPC_MONITORENTER		194
#define OPC_MONITOREXIT			195
#define OPC_WIDE			196
#define OPC_MULTIANEWARRAY		197
#define OPC_IFNULL			198
#define OPC_IFNONNULL			199
#define OPC_GOTO_W			200
#define OPC_JSR_W			201
#define OPC_LDC_QUICK			203
#define OPC_LDC_W_QUICK			204
#define OPC_GETFIELD_QUICK		206
#define OPC_PUTFIELD_QUICK		207
#define OPC_GETFIELD2_QUICK		208
#define OPC_PUTFIELD2_QUICK		209
#define OPC_GETSTATIC_QUICK		210
#define OPC_PUTSTATIC_QUICK		211
#define OPC_GETSTATIC2_QUICK		212
#define OPC_PUTSTATIC2_QUICK		213
#define OPC_INVOKEVIRTUAL_QUICK		214
#define OPC_INVOKENONVIRTUAL_QUICK	215
#define OPC_INVOKESUPER_QUICK		216
#define OPC_INVOKEVIRTUAL_QUICK_W	226
#define OPC_GETFIELD_QUICK_W		227
#define OPC_PUTFIELD_QUICK_W		228
#define OPC_GETFIELD_THIS		229
#define OPC_LOCK			230
#define OPC_ALOAD_THIS			231
#define OPC_INVOKESTATIC_QUICK		232

#define	CONSTANT_Utf8			1
#define CONSTANT_Integer		3
#define	CONSTANT_Float			4
#define	CONSTANT_Long			5
#define	CONSTANT_Double			6
#define	CONSTANT_Class			7
#define	CONSTANT_String			8
#define	CONSTANT_Fieldref		9
#define	CONSTANT_Methodref		10
#define	CONSTANT_InterfaceMethodref	11
#define	CONSTANT_NameAndType		12

#define CONSTANT_Resolved		20
#define CONSTANT_Locked			21

#define	ACC_PUBLIC		0x0001
#define	ACC_PRIVATE		0x0002
#define	ACC_PROTECTED		0x0004
#define ACC_STATIC		0x0008
#define ACC_FINAL		0x0010
#define ACC_SYNCHRONIZED	0x0020
#define ACC_SUPER		0x0020
#define ACC_VOLATILE		0x0040
#define ACC_TRANSIENT		0x0080
#define ACC_NATIVE		0x0100
#define ACC_INTERFACE		0x0200
#define ACC_ABSTRACT		0x0400

#define T_BOOLEAN		4
#define T_CHAR			5	
#define T_FLOAT			6
#define T_DOUBLE		7
#define T_BYTE			8
#define T_SHORT			9
#define T_INT			10
#define T_LONG			11

#define	CLASS_LOADED		0
#define	CLASS_LINKED		1
#define CLASS_BAD               2
#define	CLASS_INITING		3
#define	CLASS_INITED		4

#define CLASS_INTERNAL		5

typedef unsigned char		u1;
typedef unsigned short		u2;
typedef unsigned int		u4;
typedef unsigned long long	u8;

typedef u4 ConstantPoolEntry;

typedef struct constant_pool {
    volatile u1 *type;
    ConstantPoolEntry *info;
} ConstantPool;

typedef struct exception_table_entry {
    u2 start_pc;
    u2 end_pc;
    u2 handler_pc;
    u2 catch_type;
} ExceptionTableEntry;

typedef struct line_no_table_entry {
    u2 start_pc;
    u2 line_no;
} LineNoTableEntry;

typedef struct class {
   unsigned int lock;
   struct class *class;
} Class;

typedef struct object {
   unsigned int lock;
   struct class *class;
} Object;

typedef struct methodblock {
   Class *class;
   char *name;
   char *type;
   u2 access_flags;
   u2 max_stack;
   u2 max_locals;
   u2 args_count;
   u2 throw_table_size;
   u2 exception_table_size;
   u2 line_no_table_size;
   u2 native_extra_args;
   void *native_invoker;
   unsigned char *code;
   u2 *throw_table;
   ExceptionTableEntry *exception_table;
   LineNoTableEntry *line_no_table;
   int method_table_index;
} MethodBlock;

typedef struct fieldblock {
   char *name;
   char *type;
   u2 access_flags;
   u2 constant;
   u4 static_value;
   u4 offset;
} FieldBlock;

typedef struct classblock {
   int pad[2];
   char *name;
   char *super_name;
   char *source_file_name;
   Class *super;
   u2 access_flags;
   u2 flags;
   u2 interfaces_count;
   u2 fields_count;
   u2 methods_count;
   u2 constant_pool_count;
   int object_size;
   FieldBlock *fields;
   MethodBlock *methods;
   Class **interfaces;
   ConstantPool constant_pool;
   int method_table_size;
   MethodBlock **method_table;
   MethodBlock *finalizer;
   Class *element_class;
   int initing_tid;
   int dim;
   Object *class_loader;
} ClassBlock;

typedef struct frame {
   MethodBlock *mb;
   unsigned char *last_pc;
   u4 *lvars;
   u4 *ostack;
   struct frame *prev;
} Frame;

typedef struct jni_frame {
   MethodBlock *mb;
   Object **next_ref;
   Object **lrefs;
   u4 *ostack;
   struct frame *prev;
} JNIFrame;

typedef struct exec_env {
    Object *exception;
    char *stack;
    char *stack_end;
    Frame *last_frame;
    Object *thread;
} ExecEnv;

#define CLASS_CB(classRef)		((ClassBlock*)(classRef+1))
#define INST_DATA(objectRef)		((u4*)(objectRef+1))

#define IS_CLASS(object)		(!object->class || (object->class == java_lang_Class))

#define IS_INTERFACE(cb) (cb->access_flags & ACC_INTERFACE)
#define IS_PRIMITIVE(cb) ((cb->flags == CLASS_INTERNAL) && (cb->name[0] != '['))

/* Should remove these */

#define CP_TYPE(cp,i)			cp->type[i]
#define CP_INFO(cp,i)			cp->info[i]
#define CP_CLASS(cp,i)			(u2)cp->info[i]
#define CP_STRING(cp,i)			(u2)cp->info[i]
#define CP_METHOD_CLASS(cp,i)		(u2)cp->info[i]
#define CP_METHOD_NAME_TYPE(cp,i)	(u2)(cp->info[i]>>16)
#define CP_INTERFACE_CLASS(cp,i)	(u2)cp->info[i]
#define CP_INTERFACE_NAME_TYPE(cp,i)	(u2)(cp->info[i]>>16)
#define CP_FIELD_CLASS(cp,i)		(u2)cp->info[i]
#define CP_FIELD_NAME_TYPE(cp,i)	(u2)(cp->info[i]>>16)
#define CP_NAME_TYPE_NAME(cp,i)		(u2)cp->info[i]
#define CP_NAME_TYPE_TYPE(cp,i)		(u2)(cp->info[i]>>16)
#define CP_UTF8(cp,i)			(char *)(cp->info[i])

#define CP_INTEGER(cp,i)		(int)(cp->info[i])	
#define CP_FLOAT(cp,i)			*(float *)&(cp->info[i])
#define CP_LONG(cp,i)			*(long long *)&(cp->info[i])
#define CP_DOUBLE(cp,i)			*(double *)&(cp->info[i])

/* --------------------- Function prototypes  --------------------------- */

/* Alloc */

extern void initialiseAlloc(int min, int max, int verbose);
extern void initialiseGC(int noasyncgc);
extern Class *allocClass();
extern Object *allocHandle();
extern Object *allocObject(Class *class);
extern Object *allocTypeArray(int type, int size);
extern Object *allocArray(Class *class, int size, int el_size);
extern Object *allocMultiArray(Class *array_class, int dim, int *count);

extern Object *cloneObject(Object *ob);

extern int gc0();
extern int gc1();

extern int freeHeapMem();
extern int totalHeapMem();
extern int maxHeapMem();

/* Class */

extern Class *java_lang_Class;

extern Class *defineClass(char *data, int offset, int len, Object *class_loader);
extern Class *initClass(Class *class);
extern Class *findSystemClass(char *);
extern Class *findSystemClass0(char *);
extern Class *loadSystemClass(char *);

extern Class *findArrayClassFromClassLoader(char *, Object *);

#define findArrayClassFromClass(name, class) \
                    findArrayClassFromClassLoader(name, CLASS_CB(class)->class_loader)
#define findArrayClass(name) findArrayClassFromClassLoader(name, NULL)

extern Class *findClassFromClassLoader(char *, Object *);
#define findClassFromClass(name, class) \
                    findClassFromClassLoader(name, CLASS_CB(class)->class_loader)

extern char *getClassPath();
extern void initialiseClass(int verbose);

/* From jam - should be resolve? */

extern FieldBlock *findField(Class *, char *, char *);
extern MethodBlock *findMethod(Class *class, char *methodname, char *type);
extern FieldBlock *lookupField(Class *, char *, char *);
extern MethodBlock *lookupMethod(Class *class, char *methodname, char *type);
extern Class *resolveClass(Class *class, int index, int init);
extern MethodBlock *resolveMethod(Class *class, int index);
extern MethodBlock *resolveInterfaceMethod(Class *class, int index);
extern FieldBlock *resolveField(Class *class, int index);
extern char isInstanceOf(Class *class, Class *test);

/* From jam - should be execute? */

extern void *executeMethodArgs(Object *ob, Class *class, MethodBlock *mb, ...);
extern void *executeMethodVaList(Object *ob, Class *class, MethodBlock *mb, va_list args);
extern void *executeMethodList(Object *ob, Class *class, MethodBlock *mb, u8 *args);

#define executeMethod(ob, mb, args...) \
    executeMethodArgs(ob, ob->class, mb, ##args)

#define executeStaticMethod(clazz, mb, args...) \
    executeMethodArgs(NULL, clazz, mb, ##args)

/* excep */

extern Object *exceptionOccured();
extern void signalException(char *excep_name, char *excep_mess);
extern void setException(Object *excep);
extern void clearExceptiom();
extern void printException();
extern unsigned char *findCatchBlock(Class *exception);
extern void setStackTrace(Object *excep);
extern void printStackTrace(Object *excep, Object *writer);

#define exceptionOccured0(ee) \
    ee->exception

/* interp */

extern u4 *executeJava();

/* String */

extern Object *findInternedString(Object *string);
extern Object *createString(unsigned char *utf8);
extern Object *createStringFromUnicode(short *unicode, int len);
extern Object *Cstr2String(char *cstr);
extern char *String2Cstr(Object *string);
extern int getStringLen(Object *string);
extern short *getStringChars(Object *string);
extern Object *getStringCharsArray(Object *string);
extern int getStringUtf8Len(Object *string);
extern char *String2Utf8(Object *string);
extern void initialiseString();

/* Utf8 */

extern int utf8Len(unsigned char *utf8);
extern void convertUtf8(unsigned char *utf8, short *buff);
extern unsigned char *findUtf8String(unsigned char *string);
extern int utf8CharLen(short *unicode, int len);
extern char *unicode2Utf8(short *unicode, int len);
extern unsigned char *slash2dots(unsigned char *utf8);
extern void initialiseUtf8();

/* Dll */

extern void *resolveNativeMethod(MethodBlock *mb);
extern int resolveDll(char *name);
extern char *getDllPath();
extern char *getDllName(char *path, char *name);
extern void initialiseDll();

extern u4 *resolveNativeWrapper(Class *class, MethodBlock *mb, u4 *ostack);

/* Threading */

extern void initialiseMainThread(int java_stack);
extern ExecEnv *getExecEnv();

extern void createJavaThread(Object *jThread);
extern void mainThreadWaitToExitVM();

/* Monitors */

extern void initialiseMonitor();

/* reflect */

Object *getClassConstructor(Class *class, Object *arg_list);
Object *invoke(Object *ob, Class *class, MethodBlock *mb, Object *arg_array);

extern char *native_methods[][2];
