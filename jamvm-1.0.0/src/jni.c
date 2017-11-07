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

#ifndef NO_JNI
#include "jni.h"
#include "jam.h"
#include "thread.h"
#include "lock.h"

/* The extra used in expanding the local refs table.
 * This must be >= size of JNIFrame to be thread safe
 * wrt GC thread suspension */
#define LIST_INCR 16

void initJNILrefs() {
    JNIFrame *frame = (JNIFrame*)getExecEnv()->last_frame;
    frame->next_ref = (Object**)frame;
}

JNIFrame *expandJNILrefs(ExecEnv *ee, JNIFrame *frame, int incr) {
    JNIFrame *new_frame = (JNIFrame*)((Object**)frame + incr);

    if((char*)new_frame > ee->stack_end)
        return NULL;

    memcpy(new_frame, frame, sizeof(JNIFrame));
    new_frame->ostack = (u4*)(new_frame + 1);
    ee->last_frame = (Frame*)new_frame;
    memset(frame, 0, incr * sizeof(Object*));
    return new_frame;
}

void ensureJNILrefCapacity(int cap) {
    ExecEnv *ee = getExecEnv();
    JNIFrame *frame = (JNIFrame*)ee->last_frame;
    int size = (Object**)frame - frame->lrefs;

    if(size < cap) {
        int incr = cap-size;
	if(incr < sizeof(JNIFrame))
            incr = sizeof(JNIFrame);
        expandJNILrefs(ee, frame, incr);
    }
}

Object *addJNILref(Object *ref) {
    ExecEnv *ee = getExecEnv();
    JNIFrame *frame = (JNIFrame*)ee->last_frame;

    if(frame->next_ref == (Object**)frame) {
        JNIFrame *new_frame;
        if((new_frame = expandJNILrefs(ee, frame, LIST_INCR)) == NULL)
            return NULL;
	frame = new_frame;
    }

    return *frame->next_ref++ = ref;
}

void delJNILref(Object *ref) {
    ExecEnv *ee = getExecEnv();
    JNIFrame *frame = (JNIFrame*)ee->last_frame;
    Object **opntr = frame->lrefs;

    for(; opntr < frame->next_ref; opntr++)
        if(*opntr == ref) {
            *opntr = NULL;
	    return;
	}
}
 
static VMLock global_ref_lock;
static Object **global_ref_table;
static int global_ref_size = 0;
static int global_ref_next = 0;
static int global_ref_deleted = 0;

static void initJNIGrefs() {
    initVMLock(global_ref_lock);
}

static Object *addJNIGref(Object *ref) {
    Thread *self = threadSelf();
    disableSuspend(self);
    lockVMLock(global_ref_lock, self);

    if(global_ref_next == global_ref_size) {
        int new_size;
        Object **new_table;
        int i, j;
	       
	if(global_ref_deleted >= LIST_INCR) {
            new_size = global_ref_size;
	    new_table = global_ref_table;
	} else {
	    new_size = global_ref_size + LIST_INCR - global_ref_deleted;
       	    new_table = (Object**)malloc(new_size * sizeof(Object*));

	    if(new_table == NULL)
                return NULL;
	}

        for(i = 0, j = 0; i < global_ref_size; i++)
            if(global_ref_table[i])
                new_table[j++] = global_ref_table[i];

	global_ref_next = j;
	global_ref_size = new_size;
	global_ref_table = new_table;
	global_ref_deleted = 0;
    }

    global_ref_table[global_ref_next++] = ref;

    unlockVMLock(global_ref_lock, self);
    enableSuspend(self);

    return ref;
}

static void delJNIGref(Object *ref) {
    Thread *self = threadSelf();
    int i;

    disableSuspend(self);
    lockVMLock(global_ref_lock, self);

    for(i = 0; i < global_ref_next; i++)
        if(global_ref_table[i] == ref) {
            global_ref_table[i] = NULL;
	    global_ref_deleted++;
	    break;
	}

    unlockVMLock(global_ref_lock, self);
    enableSuspend(self);
}

extern void markObject(Object *obj);

void markJNIGlobalRefs() {
    Thread *self = threadSelf();
    int i;

    disableSuspend(self);
    lockVMLock(global_ref_lock, self);

    for(i = 0; i < global_ref_next; i++)
        if(global_ref_table[i])
            markObject(global_ref_table[i]);

    unlockVMLock(global_ref_lock, self);
    enableSuspend(self);
}

jint Jam_GetVersion(JNIEnv *env) {
    return 0x00010001;
}

jclass Jam_DefineClass(JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize bufLen) {
    return (jclass)defineClass((char *)buf, 0, (int)bufLen, (Object *)loader);
}

jclass Jam_FindClass(JNIEnv *env, const char *name) {
    return (jclass) findClassFromClassLoader((char*) name, NULL);
}

jclass Jam_GetSuperClass(JNIEnv *env, jclass clazz) {
    ClassBlock *cb = CLASS_CB((Class *)clazz);
    return IS_INTERFACE(cb) ? NULL : (jclass)cb->super;
}

jboolean Jam_IsAssignableFrom(JNIEnv *env, jclass clazz1, jclass clazz2) {
    return isInstanceOf((Class*)clazz2, (Class*)clazz1);
}

jint Jam_Throw(JNIEnv *env, jthrowable obj) {
    Object *ob = (Object*)obj;
    setStackTrace(ob);
    setException(ob);
    return JNI_TRUE;
}

jint Jam_ThrowNew(JNIEnv *env, jclass clazz, const char *message) {
    signalException(CLASS_CB((Class*)clazz)->name, (char*)message);
    return JNI_TRUE;
}

jthrowable Jam_ExceptionOccurred(JNIEnv *env) {
    return (jthrowable) exceptionOccured();
}

void Jam_ExceptionDescribe(JNIEnv *env) {
    printException();
}

void Jam_ExceptionClear(JNIEnv *env) {
    clearException();
}

void Jam_FatalError(JNIEnv *env, const char *message) {
    fprintf(stderr, "JNI - FatalError: %s\n", message);
    exit(0);
}

jobject Jam_NewGlobalRef(JNIEnv *env, jobject obj) {
    return addJNIGref((Object*)obj);
}

void Jam_DeleteGlobalRef(JNIEnv *env, jobject obj) {
    delJNIGref((Object*)obj);
}

void Jam_DeleteLocalRef(JNIEnv *env, jobject obj) {
    delJNILref((Object*)obj);
}

jboolean Jam_IsSameObject(JNIEnv *env, jobject obj1, jobject obj2) {
    return obj1 == obj2;
}

jobject Jam_AllocObject(JNIEnv *env, jclass clazz) {
    return (jobject) addJNILref(allocObject((Class*)clazz));
}

jclass Jam_GetObjectClass(JNIEnv *env, jobject obj) {
    return (jobject)((Object*)obj)->class;
}

jboolean Jam_IsInstanceOf(JNIEnv *env, jobject obj, jclass clazz) {
    return (obj == NULL) || isInstanceOf((Class*)clazz, ((Object*)obj)->class);
}

jmethodID Jam_GetMethodID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    MethodBlock *mb = lookupMethod((Class*)clazz, (char*)name, (char*)sig);
    if(mb == NULL)
        signalException("java/lang/NoSuchMethodError", (char *)name);

    return (jmethodID) mb;
}

jfieldID Jam_GetFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    FieldBlock *fb = lookupField((Class*)clazz, (char*)name, (char*)sig);
    if(fb == NULL)
        signalException("java/lang/NoSuchFieldError", (char *)name);

    return (jfieldID) fb;
}

jmethodID Jam_GetStaticMethodID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    MethodBlock *mb = lookupMethod((Class*)clazz, (char*)name, (char*)sig);
    if(mb == NULL)
        signalException("java/lang/NoSuchMethodError", (char *)name);

    return (jmethodID) mb;
}

jfieldID Jam_GetStaticFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    FieldBlock *fb = lookupField((Class*)clazz, (char*)name, (char*)sig);
    if(fb == NULL)
        signalException("java/lang/NoSuchFieldError", (char *)name);

    return (jfieldID) fb;
}

jstring Jam_NewString(JNIEnv *env, const jchar *unicodeChars, jsize len) {
    return (jstring) addJNILref(createStringFromUnicode((short*)unicodeChars, len));
}

jsize Jam_GetStringLength(JNIEnv *env, jstring string) {
    return getStringLen((Object*)string);
}

const jchar *Jam_GetStringChars(JNIEnv *env, jstring string, jboolean *isCopy) {
    if(isCopy != NULL)
        *isCopy = JNI_FALSE;
    addJNIGref(getStringCharsArray((Object*)string));
    return (const jchar *)getStringChars((Object*)string);
}

void Jam_ReleaseStringChars(JNIEnv *env, jstring string, const jchar *chars) {
    delJNIGref(getStringCharsArray((Object*)string));
}

jstring Jam_NewStringUTF(JNIEnv *env, const char *bytes) {
    return (jstring) addJNILref(createString((unsigned char*)bytes));
}

jsize Jam_GetStringUTFLength(JNIEnv *env, jstring string) {
    return getStringUtf8Len((Object*)string);
}

const char *Jam_GetStringUTFChars(JNIEnv *env, jstring string, jboolean *isCopy) {
    if(isCopy != NULL)
        *isCopy = JNI_TRUE;
    return (const char*)String2Utf8((Object*)string);
}

void Jam_ReleaseStringUTFChars(JNIEnv *env, jstring string, const char *chars) {
    free(chars);
}

jsize Jam_GetArrayLength(JNIEnv *env, jarray array) {
    return *(int*)INST_DATA((Object*)array);
}

jobject Jam_NewObject(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    Object *ob =  allocObject((Class*)clazz);

    if(ob) {
        va_list jargs;
        va_start(jargs, methodID);
        executeMethodVaList(ob, ob->class, (MethodBlock*)methodID, jargs);
        va_end(jargs);
    }

    return (jobject) addJNILref(ob);
}

jobject Jam_NewObjectA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args) {
    Object *ob =  allocObject((Class*)clazz);

    if(ob) executeMethodList(ob, ob->class, (MethodBlock*)methodID, (u8*)args);
    return (jobject) addJNILref(ob);
}

jobject Jam_NewObjectV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    Object *ob =  allocObject((Class*)clazz);

    if(ob) executeMethodVaList(ob, ob->class, (MethodBlock*)methodID, args);
    return (jobject) addJNILref(ob);
}

jarray Jam_NewObjectArray(JNIEnv *env, jsize length, jclass elementClass, jobject initialElement) {
    Class *class = (Class*)elementClass;
    Class *array_class;
    char ac_name[256];

    if(CLASS_CB(class)->name[0] == '[')
        strcat(strcpy(ac_name, "["), CLASS_CB(class)->name);
    else
        strcat(strcat(strcpy(ac_name, "[L"), CLASS_CB(class)->name), ";");

    array_class = findArrayClass(ac_name);
    if(array_class) {
        Object *array = allocArray(array_class, length, 4);
	if(array && initialElement) {
            u4 *data = INST_DATA(array) + 1;

	    while(length--)
               *data++ = (u4) initialElement;
        }
	return (jarray) addJNILref(array);
    }
    return NULL;
}

jarray Jam_GetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index) {
    return (jarray) addJNILref((Object*)(INST_DATA((Object*)array)[index+1]));
}

void Jam_SetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index, jobject value) {
    INST_DATA((Object*)array)[index+1] = (u4)value;
}

jint Jam_RegisterNatives(JNIEnv *env, jclass clazz, const JNINativeMethod *methods, jint nMethods) {
    return 0;
}

jint Jam_UnregisterNatives(JNIEnv *env, jclass clazz) {
    return 0;
}

jint Jam_MonitorEnter(JNIEnv *env, jobject obj) {
    objectLock((Object*)obj);
    return 0;
}

jint Jam_MonitorExit(JNIEnv *env, jobject obj) {
    objectUnlock((Object*)obj);
    return 0;
}

jint Jam_GetJavaVM(JNIEnv *env, JavaVM **vm) {
    return 0;
}

#define GET_FIELD(type, native_type)                                                       \
native_type Jam_Get##type##Field(JNIEnv *env, jobject obj, jfieldID fieldID) {             \
    FieldBlock *fb = (FieldBlock *) fieldID;                                               \
    Object *ob = (Object*) obj;                                                            \
    return *(native_type *)&(INST_DATA(ob)[fb->offset]);                                   \
}

#define SET_FIELD(type, native_type)                                                       \
void Jam_Set##type##Field(JNIEnv *env, jobject obj, jfieldID fieldID, native_type value) { \
    Object *ob = (Object*) obj;                                                            \
    FieldBlock *fb = (FieldBlock *) fieldID;                                               \
    *(native_type *)&(INST_DATA(ob)[fb->offset]) = value;                                  \
}

#define GET_STATIC_FIELD(type, native_type)                                                \
native_type Jam_GetStatic##type##Field(JNIEnv *env, jclass clazz, jfieldID fieldID) {      \
    FieldBlock *fb = (FieldBlock *) fieldID;                                               \
    return *(native_type *)&fb->static_value;                                              \
}

#define SET_STATIC_FIELD(type, native_type)                                                \
void Jam_SetStatic##type##Field(JNIEnv *env, jclass clazz, jfieldID fieldID,               \
		native_type value) {                                                       \
    FieldBlock *fb = (FieldBlock *) fieldID;                                               \
    *(native_type *)&fb->static_value = value;                                             \
}

#define FIELD_ACCESS(type, native_type)      \
        GET_FIELD(type, native_type);        \
        SET_FIELD(type, native_type);        \
        GET_STATIC_FIELD(type, native_type); \
        SET_STATIC_FIELD(type, native_type);

FIELD_ACCESS(Boolean, jboolean);
FIELD_ACCESS(Byte, jbyte);
FIELD_ACCESS(Char, jchar);
FIELD_ACCESS(Short, jshort);
FIELD_ACCESS(Int, jint);
FIELD_ACCESS(Long, jlong);
FIELD_ACCESS(Float, jfloat);
FIELD_ACCESS(Double, jdouble);

jobject Jam_GetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    FieldBlock *fb = (FieldBlock *) fieldID;
    Object *ob = (Object*) obj;
    return (jobject) addJNILref((Object*)(INST_DATA(ob)[fb->offset]));
}

void Jam_SetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID, jobject value) {
    Object *ob = (Object*) obj;
    FieldBlock *fb = (FieldBlock *) fieldID;
    INST_DATA(ob)[fb->offset] = (u4)value;
}

jobject Jam_GetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    FieldBlock *fb = (FieldBlock *) fieldID;
    return (jobject) addJNILref((Object*)fb->static_value);
}

void Jam_SetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID, jobject value) {
    FieldBlock *fb = (FieldBlock *) fieldID;
    fb->static_value = (u4)value;
}

#define VIRTUAL_METHOD(type, native_type)                                                        \
native_type Jam_Call##type##Method(JNIEnv *env, jobject obj, jmethodID mID, ...) {               \
    Object *ob = (Object *)obj;                                                                  \
    native_type *ret;                                                                            \
    va_list jargs;                                                                               \
                                                                                                 \
    MethodBlock *mb = (CLASS_CB(ob->class))->                                                    \
                        method_table[((MethodBlock*)mID)->method_table_index];                   \
                                                                                                 \
    va_start(jargs, mID);                                                                        \
    ret = (native_type*) executeMethodVaList(ob, ob->class, mb, jargs);                          \
    va_end(jargs);                                                                               \
                                                                                                 \
    return *ret;                                                                                 \
}                                                                                                \
                                                                                                 \
native_type Jam_Call##type##MethodV(JNIEnv *env, jobject obj, jmethodID mID, va_list jargs) {    \
    Object *ob = (Object *)obj;                                                                  \
    MethodBlock *mb = (CLASS_CB(ob->class))->                                                    \
                        method_table[((MethodBlock*)mID)->method_table_index];                   \
    return *(native_type*)executeMethodVaList(ob, ob->class, mb, jargs);                         \
}                                                                                                \
                                                                                                 \
native_type Jam_Call##type##MethodA(JNIEnv *env, jobject obj, jmethodID mID, jvalue *jargs) {    \
    Object *ob = (Object *)obj;                                                                  \
    MethodBlock *mb = (CLASS_CB(ob->class))->                                                    \
                        method_table[((MethodBlock*)mID)->method_table_index];                   \
    return *(native_type*)executeMethodList(ob, ob->class, mb, (u8*)jargs);                      \
}

#define NONVIRTUAL_METHOD(type, native_type)                                                     \
native_type Jam_CallNonvirtual##type##Method(JNIEnv *env, jobject obj, jclass clazz,             \
		jmethodID methodID, ...) {                                                       \
    native_type *ret;                                                                            \
    va_list jargs;                                                                               \
                                                                                                 \
    va_start(jargs, methodID);                                                                   \
    ret = (native_type*)                                                                         \
              executeMethodVaList((Object*)obj, (Class*)clazz, (MethodBlock*)methodID, jargs);   \
    va_end(jargs);                                                                               \
                                                                                                 \
    return *ret;                                                                                 \
}                                                                                                \
                                                                                                 \
native_type Jam_CallNonvirtual##type##MethodV(JNIEnv *env, jobject obj, jclass clazz,            \
		jmethodID methodID, va_list jargs) {                                             \
    return *(native_type*)                                                                       \
	      executeMethodVaList((Object*)obj, (Class*)clazz, (MethodBlock*)methodID, jargs);   \
}                                                                                                \
                                                                                                 \
native_type Jam_CallNonvirtual##type##MethodA(JNIEnv *env, jobject obj, jclass clazz,            \
		jmethodID methodID, jvalue *jargs) {                                             \
    return *(native_type*)                                                                       \
	    executeMethodList((Object*)obj, (Class*)clazz, (MethodBlock*)methodID, (u8*)jargs);  \
}

#define STATIC_METHOD(type, native_type)                                                         \
native_type Jam_CallStatic##type##Method(JNIEnv *env, jclass clazz,                              \
		jmethodID methodID, ...) {                                                       \
    native_type *ret;                                                                            \
    va_list jargs;                                                                               \
                                                                                                 \
    va_start(jargs, methodID);                                                                   \
    ret = (native_type*) executeMethodVaList(NULL, (Class*)clazz, (MethodBlock*)methodID, jargs);\
    va_end(jargs);                                                                               \
                                                                                                 \
    return *ret;                                                                                 \
}                                                                                                \
                                                                                                 \
native_type Jam_CallStatic##type##MethodV(JNIEnv *env, jclass clazz, jmethodID methodID,         \
		va_list jargs) {                                                                 \
    return *(native_type*)                                                                       \
	    executeMethodVaList(NULL, (Class*)clazz, (MethodBlock*)methodID, jargs);             \
}                                                                                                \
                                                                                                 \
native_type Jam_CallStatic##type##MethodA(JNIEnv *env, jclass clazz, jmethodID methodID,         \
		jvalue *jargs) {                                                                 \
    return *(native_type*)                                                                       \
	    executeMethodList(NULL, (Class*)clazz, (MethodBlock*)methodID, (u8*)jargs);          \
}

#define CALL_METHOD(access)         \
access##_METHOD(Boolean, jboolean); \
access##_METHOD(Byte, jbyte);       \
access##_METHOD(Char, jchar);       \
access##_METHOD(Short, jshort);     \
access##_METHOD(Int, jint);         \
access##_METHOD(Long, jlong);       \
access##_METHOD(Float, jfloat);     \
access##_METHOD(Double, jdouble);

CALL_METHOD(VIRTUAL);
CALL_METHOD(NONVIRTUAL);
CALL_METHOD(STATIC);

jobject Jam_CallObjectMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    Object *ob = (Object *)obj;
    Object *ret;
    va_list jargs;
    MethodBlock *mb = (CLASS_CB(ob->class))->
                        method_table[((MethodBlock*)methodID)->method_table_index];

    va_start(jargs, methodID);
    ret = addJNILref(*(Object**) executeMethodVaList(ob, ob->class, mb, jargs));
    va_end(jargs);
    return (jobject)ret;
}

jobject Jam_CallObjectMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list jargs) {
    Object *ob = (Object *)obj;
    MethodBlock *mb = (CLASS_CB(ob->class))->
                        method_table[((MethodBlock*)methodID)->method_table_index];
    return (jobject)addJNILref(*(Object**) executeMethodVaList(ob, ob->class, mb, jargs));
}

jobject Jam_CallObjectMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *jargs) {
    Object *ob = (Object *)obj;
    MethodBlock *mb = (CLASS_CB(ob->class))->
                        method_table[((MethodBlock*)methodID)->method_table_index];
    return (jobject)addJNILref(*(Object**) executeMethodList(ob, ob->class, mb, (u8*)jargs));
}

jobject Jam_CallNonvirtualObjectMethod(JNIEnv *env, jobject obj, jclass clazz,
		jmethodID methodID, ...) {
    Object *ret;
    va_list jargs;
    va_start(jargs, methodID);
    ret = addJNILref(*(Object**) executeMethodVaList((Object*)obj,
			    (Class*)clazz, (MethodBlock*)methodID, jargs));
    va_end(jargs);
    return (jobject)ret;
}

jobject Jam_CallNonvirtualObjectMethodV(JNIEnv *env, jobject obj, jclass clazz,
		jmethodID methodID, va_list jargs) {
    return (jobject)addJNILref(*(Object**) executeMethodVaList((Object*)obj,
			    (Class*)clazz, (MethodBlock*)methodID, jargs));
}

jobject Jam_CallNonvirtualObjectMethodA(JNIEnv *env, jobject obj, jclass clazz,
		jmethodID methodID, jvalue *jargs) {
    return (jobject)addJNILref(*(Object**) executeMethodList((Object*)obj,
			    (Class*)clazz, (MethodBlock*)methodID, (u8*)jargs));
}

jobject Jam_CallStaticObjectMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    Object *ret;
    va_list jargs;
    va_start(jargs, methodID);
    ret = addJNILref(*(Object**) executeMethodVaList(NULL,
			      (Class*)clazz, (MethodBlock*)methodID, jargs));
    va_end(jargs);
    return (jobject)ret;
}

jobject Jam_CallStaticObjectMethodV(JNIEnv *env, jclass clazz,
		jmethodID methodID, va_list jargs) {
    return (jobject)addJNILref(*(Object**) executeMethodVaList(NULL,
		              (Class*)clazz, (MethodBlock*)methodID, jargs));
}

jobject Jam_CallStaticObjectMethodA(JNIEnv *env, jclass clazz,
		jmethodID methodID, jvalue *jargs) {
    return (jobject)addJNILref(*(Object**) executeMethodList(NULL,
		            (Class*)clazz, (MethodBlock*)methodID, (u8*)jargs));
}

void Jam_CallVoidMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    va_list jargs;
    va_start(jargs, methodID);
    executeMethodVaList((Object*)obj, ((Object*)obj)->class, (MethodBlock*)methodID, jargs);
    va_end(jargs);
}

void Jam_CallVoidMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list jargs) {
    executeMethodVaList((Object*)obj, ((Object*)obj)->class, (MethodBlock*)methodID, jargs);
}

void Jam_CallVoidMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *jargs) {
    executeMethodList((Object*)obj, ((Object*)obj)->class, (MethodBlock*)methodID, (u8*)jargs);
}

void Jam_CallNonvirtualVoidMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    va_list jargs;
    va_start(jargs, methodID);
    executeMethodVaList((Object*)obj, (Class*)clazz, (MethodBlock*)methodID, jargs);
    va_end(jargs);
}

void Jam_CallNonvirtualVoidMethodV(JNIEnv *env, jobject obj, jclass clazz,
		jmethodID methodID, va_list jargs) {
      executeMethodVaList((Object*)obj, (Class*)clazz, (MethodBlock*)methodID, jargs);
}

void Jam_CallNonvirtualVoidMethodA(JNIEnv *env, jobject obj, jclass clazz,
		jmethodID methodID, jvalue *jargs) {
    executeMethodList((Object*)obj, (Class*)clazz, (MethodBlock*)methodID, (u8*)jargs);
}

void Jam_CallStaticVoidMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    va_list jargs;
    va_start(jargs, methodID);
    executeMethodVaList(NULL, (Class*)clazz, (MethodBlock*)methodID, jargs);
    va_end(jargs);
}

void Jam_CallStaticVoidMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list jargs) {
    executeMethodVaList(NULL, (Class*)clazz, (MethodBlock*)methodID, jargs);
}

void Jam_CallStaticVoidMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *jargs) {
    executeMethodList(NULL, (Class*)clazz, (MethodBlock*)methodID, (u8*)jargs);
}

#define NEW_PRIM_ARRAY(type, native_type, array_type)                             \
native_type##Array Jam_New##type##Array(JNIEnv *env, jsize length) {              \
    return (native_type##Array) addJNILref(allocTypeArray(array_type, length));   \
}

#define GET_PRIM_ARRAY_ELEMENTS(type, native_type)                                                   \
native_type *Jam_Get##type##ArrayElements(JNIEnv *env, native_type##Array array, jboolean *isCopy) { \
    if(isCopy != NULL)                                                                               \
        *isCopy = JNI_FALSE;                                                                         \
    addJNIGref((Object*)array);                                                                      \
    return (native_type*)(INST_DATA((Object*)array)+1);                                              \
}

#define RELEASE_PRIM_ARRAY_ELEMENTS(type, native_type)                                                        \
void Jam_Release##type##ArrayElements(JNIEnv *env, native_type##Array array, native_type *elems, jint mode) { \
    delJNIGref((Object*)array);                                                                               \
}

#define GET_PRIM_ARRAY_REGION(type, native_type)                                                                   \
void Jam_Get##type##ArrayRegion(JNIEnv *env, native_type##Array array, jsize start, jsize len, native_type *buf) { \
    memcpy(buf, (native_type*)(INST_DATA((Object*)array)+1) + start, len * sizeof(native_type*));                  \
}

#define SET_PRIM_ARRAY_REGION(type, native_type)                                                                   \
void Jam_Set##type##ArrayRegion(JNIEnv *env, native_type##Array array, jsize start, jsize len, native_type *buf) { \
    memcpy((native_type*)(INST_DATA((Object*)array)+1) + start, buf, len * sizeof(native_type*));                  \
}

#define PRIM_ARRAY_OP(type, native_type, array_type) \
    NEW_PRIM_ARRAY(type, native_type, array_type);   \
    GET_PRIM_ARRAY_ELEMENTS(type, native_type);      \
    RELEASE_PRIM_ARRAY_ELEMENTS(type, native_type);  \
    GET_PRIM_ARRAY_REGION(type, native_type);        \
    SET_PRIM_ARRAY_REGION(type, native_type);

PRIM_ARRAY_OP(Boolean, jboolean, T_BOOLEAN);
PRIM_ARRAY_OP(Byte, jbyte, T_BYTE);
PRIM_ARRAY_OP(Char, jchar, T_CHAR);
PRIM_ARRAY_OP(Short, jshort, T_SHORT);
PRIM_ARRAY_OP(Int, jint, T_INT);
PRIM_ARRAY_OP(Long, jlong, T_LONG);
PRIM_ARRAY_OP(Float, jfloat, T_FLOAT);
PRIM_ARRAY_OP(Double, jdouble, T_DOUBLE);

#define METHOD(type, ret_type)                \
    Jam_Call##type##ret_type##Method,         \
    Jam_Call##type##ret_type##MethodV,        \
    Jam_Call##type##ret_type##MethodA

#define METHODS(type)                         \
    METHOD(type, Object),                     \
    METHOD(type, Boolean),                    \
    METHOD(type, Byte),                       \
    METHOD(type, Char),                       \
    METHOD(type, Short),                      \
    METHOD(type, Int),                        \
    METHOD(type, Long),                       \
    METHOD(type, Float),                      \
    METHOD(type, Double),                     \
    METHOD(type, Void)

#define FIELD(direction, type, field_type)    \
    Jam_##direction##type##field_type##Field

#define FIELDS2(direction, type)              \
	FIELD(direction, type,  Object),      \
	FIELD(direction, type, Boolean),      \
	FIELD(direction, type,  Byte),        \
	FIELD(direction, type, Char),         \
	FIELD(direction, type, Short),        \
	FIELD(direction, type, Int),          \
	FIELD(direction, type, Long),         \
	FIELD(direction, type, Float),        \
	FIELD(direction, type, Double)

#define FIELDS(type)                          \
	FIELDS2(Get, type),                   \
	FIELDS2(Set, type)

#define ARRAY(op, el_type, type)              \
	Jam_##op##el_type##Array##type

#define ARRAY_OPS(op, type)                   \
	ARRAY(op, Boolean, type),             \
	ARRAY(op, Byte, type),                \
	ARRAY(op, Char, type),                \
	ARRAY(op, Short, type),               \
	ARRAY(op, Int, type),                 \
	ARRAY(op, Long, type),                \
	ARRAY(op, Float, type),               \
	ARRAY(op, Double, type)

struct _JNINativeInterface Jam_JNINativeInterface = {
    NULL,
    NULL,
    NULL,
    NULL,
    Jam_GetVersion,
    Jam_DefineClass,
    Jam_FindClass,
    NULL,
    NULL,
    NULL,
    Jam_GetSuperClass,
    Jam_IsAssignableFrom,
    NULL,
    Jam_Throw,
    Jam_ThrowNew,
    Jam_ExceptionOccurred,
    Jam_ExceptionDescribe,
    Jam_ExceptionClear,
    Jam_FatalError,
    NULL,
    NULL,
    Jam_NewGlobalRef,
    Jam_DeleteGlobalRef,
    Jam_DeleteLocalRef,
    Jam_IsSameObject,
    NULL,
    NULL,
    Jam_AllocObject,
    Jam_NewObject,
    Jam_NewObjectV,
    Jam_NewObjectA,
    Jam_GetObjectClass,
    Jam_IsInstanceOf,
    Jam_GetMethodID,
    METHODS(/*virtual*/),
    METHODS(Nonvirtual),
    Jam_GetFieldID,
    FIELDS(/*instance*/),
    Jam_GetStaticMethodID,
    METHODS(Static),
    Jam_GetStaticFieldID,
    FIELDS(Static),
    Jam_NewString,
    Jam_GetStringLength,
    Jam_GetStringChars,
    Jam_ReleaseStringChars,
    Jam_NewStringUTF,
    Jam_GetStringUTFLength,
    Jam_GetStringUTFChars,
    Jam_ReleaseStringUTFChars,
    Jam_GetArrayLength,
    ARRAY(New, Object,),
    ARRAY(Get, Object, Element),
    ARRAY(Set, Object, Element),
    ARRAY_OPS(New,),
    ARRAY_OPS(Get, Elements),
    ARRAY_OPS(Release, Elements),
    ARRAY_OPS(Get, Region),
    ARRAY_OPS(Set, Region),
    Jam_RegisterNatives,
    Jam_UnregisterNatives,
    Jam_MonitorEnter,
    Jam_MonitorExit,
    Jam_GetJavaVM
};

void initialiseJNI() {
    initJNIGrefs();
}
#endif
