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
#include <stdlib.h>
#include <string.h>

#include "jam.h"
#include "sig.h"
#include "thread.h"
#include "hash.h"

#include <ctype.h>

#define PREPARE(ptr) ptr
#define SCAVENGE(ptr) FALSE
#define FOUND(ptr)

static int verbose;
static char **classpath;

Class *java_lang_Class = NULL;

/* hash table containing loaded classes and internally
   created arrays */

#define INITSZE 1<<8
static HashTable loaded_classes;

/* Array large enough to hold all primitive classes -
 * access protected by loaded_classes hash table lock */
static Class *prim_classes[10];
static int num_prim = 0;

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

#define READ_U1(v,p,l)	v = *p++
#define READ_U2(v,p,l)  v = (p[0]<<8)|p[1]; p+=2
#define READ_U4(v,p,l)	v = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; p+=4
#define READ_U8(v,p,l)	v = ((u8)p[0]<<56)|((u8)p[1]<<48)|((u8)p[2]<<40) \
                            |((u8)p[3]<<32)|((u8)p[4]<<24)|((u8)p[5]<<16) \
                            |((u8)p[6]<<8)|(u8)p[7]; p+=8

#define	READ_INDEX(v,p,l)		READ_U2(v,p,l)
#define READ_TYPE_INDEX(v,cp,t,p,l)	READ_U2(v,p,l)


static Class *addClassToHash(Class *class) {
    Class *entry;

#define HASH(ptr) utf8Hash(CLASS_CB((Class *)ptr)->name)
#define COMPARE(ptr1, ptr2, hash1, hash2) (hash1 == hash2) && \
                     utf8Comp(CLASS_CB((Class *)ptr1)->name, CLASS_CB((Class *)ptr2)->name) && \
                     (CLASS_CB((Class*)ptr1)->class_loader == CLASS_CB((Class *)ptr2)->class_loader)

    findHashEntry(loaded_classes, class, entry, TRUE, FALSE);

    return entry;
}

Class *defineClass(char *data, int offset, int len, Object *class_loader) {
    unsigned char *ptr = (unsigned char *)data+offset;
    int cp_count, intf_count, i;
    u2 major_version, minor_version, this_idx, super_idx;
    u2 attr_count;
    u4 magic;

    ConstantPool *constant_pool;
    ClassBlock *classblock;
    Class *class, *found;
    Class **interfaces;

    READ_U4(magic, ptr, len);

    if(magic != 0xcafebabe) {
       signalException("java/lang/NoClassDefFoundError", "bad magic");
       return NULL;
    }

    READ_U2(minor_version, ptr, len);
    READ_U2(major_version, ptr, len);

    class = allocClass();
    if(class == NULL)
        return NULL;

    classblock = CLASS_CB(class);
    READ_U2(cp_count = classblock->constant_pool_count, ptr, len);

    constant_pool = &classblock->constant_pool;
    constant_pool->type = (char *)malloc(cp_count);
    constant_pool->info = (ConstantPoolEntry *)
                       malloc(cp_count*sizeof(ConstantPoolEntry));

    for(i = 1; i < cp_count; i++) {
        u1 tag;

        READ_U1(tag, ptr, len);
        CP_TYPE(constant_pool,i) = tag;

        switch(tag) {
	   case CONSTANT_Class:
	   case CONSTANT_String:
               READ_INDEX(CP_INFO(constant_pool,i), ptr, len);
               break;

	   case CONSTANT_Fieldref:
	   case CONSTANT_Methodref:
	   case CONSTANT_NameAndType:
	   case CONSTANT_InterfaceMethodref:
           {
               u2 idx1, idx2;

               READ_INDEX(idx1, ptr, len);
               READ_INDEX(idx2, ptr, len);
               CP_INFO(constant_pool,i) = (idx2<<16)+idx1;
               break;
           }

	   case CONSTANT_Integer:
	   case CONSTANT_Float:
               READ_U4(CP_INFO(constant_pool,i), ptr, len);
               break;

	   case CONSTANT_Long:
	   case CONSTANT_Double:
               READ_U8(*(u8 *)&(CP_INFO(constant_pool,i)), ptr, len);
               i++;
               break;

	   case CONSTANT_Utf8:
           {
               int length;
               unsigned char *buff, *utf8;

               READ_U2(length, ptr, len);
               buff = (unsigned char*)malloc(length+1);

               memcpy(buff, ptr, length);
               buff[length] = '\0';
               ptr += length;

               CP_INFO(constant_pool,i) = (u4)utf8 = findUtf8String(buff);

               if(utf8 != buff)
                   free(buff);

               break;
           }

	   default:
               signalException("java/lang/NoClassDefFoundError", "bad constant pool tag");
               return NULL;
        }
    }

    READ_U2(classblock->access_flags, ptr, len);

    READ_TYPE_INDEX(this_idx, constant_pool, CONSTANT_Class, ptr, len);
    classblock->name = CP_UTF8(constant_pool, CP_CLASS(constant_pool, this_idx));

    if(strcmp(classblock->name, "java/lang/Object") == 0) {
        READ_U2(super_idx, ptr, len);
        if(super_idx) {
           signalException("java/lang/ClassFormatError", "Object has super");
           return NULL;
        }
        classblock->super_name = NULL;
    } else {
        READ_TYPE_INDEX(super_idx, constant_pool, CONSTANT_Class, ptr, len);
        classblock->super_name = CP_UTF8(constant_pool, CP_CLASS(constant_pool, super_idx));
    }

    classblock->class_loader = class_loader;

    READ_U2(intf_count = classblock->interfaces_count, ptr, len);
    interfaces = classblock->interfaces =
                      (Class **)malloc(intf_count * sizeof(Class *));

    for(i = 0; i < intf_count; i++) {
       u2 index;
       READ_TYPE_INDEX(index, constant_pool, CONSTANT_Class, ptr, len);
       interfaces[i] = resolveClass(class, index, FALSE);
       if(exceptionOccured())
           return NULL; 
    }

    READ_U2(classblock->fields_count, ptr, len);
    classblock->fields = (FieldBlock *)
            malloc(classblock->fields_count * sizeof(FieldBlock));

    for(i = 0; i < classblock->fields_count; i++) {
        u2 name_idx, type_idx;

        READ_U2(classblock->fields[i].access_flags, ptr, len);
        READ_TYPE_INDEX(name_idx, constant_pool, CONSTANT_Utf8, ptr, len);
        READ_TYPE_INDEX(type_idx, constant_pool, CONSTANT_Utf8, ptr, len);
        classblock->fields[i].name = CP_UTF8(constant_pool, name_idx);
        classblock->fields[i].type = CP_UTF8(constant_pool, type_idx);

        READ_U2(attr_count, ptr, len);
        for(; attr_count != 0; attr_count--) {
           u2 attr_name_idx;
           char *attr_name;
           u4 attr_length;

           READ_TYPE_INDEX(attr_name_idx, constant_pool, CONSTANT_Utf8, ptr, len);
           attr_name = CP_UTF8(constant_pool, attr_name_idx);
           READ_U4(attr_length, ptr, len);

           if(strcmp(attr_name,"ConstantValue") == 0) {
               READ_INDEX(classblock->fields[i].constant, ptr, len);
           }
           else
               ptr += attr_length;
        }
    }

    READ_U2(classblock->methods_count, ptr, len);

    classblock->methods = (MethodBlock *)
            malloc(classblock->methods_count * sizeof(MethodBlock));

    memset(classblock->methods, 0, classblock->methods_count * sizeof(MethodBlock));

    for(i = 0; i < classblock->methods_count; i++) {
        MethodBlock *method = &classblock->methods[i];
        u2 name_idx, type_idx;

        READ_U2(method->access_flags, ptr, len);
        READ_TYPE_INDEX(name_idx, constant_pool, CONSTANT_Utf8, ptr, len);
        READ_TYPE_INDEX(type_idx, constant_pool, CONSTANT_Utf8, ptr, len);

        method->name = CP_UTF8(constant_pool, name_idx);
        method->type = CP_UTF8(constant_pool, type_idx);

        READ_U2(attr_count, ptr, len);
        for(; attr_count != 0; attr_count--) {
           u2 attr_name_idx;
           char *attr_name;
           u4 attr_length;

           READ_TYPE_INDEX(attr_name_idx, constant_pool, CONSTANT_Utf8, ptr, len);
           READ_U4(attr_length, ptr, len);
           attr_name = CP_UTF8(constant_pool, attr_name_idx);

           if(strcmp(attr_name, "Code") == 0) {
              u4 code_length;
              u2 code_attr_cnt;
              int j;

              READ_U2(method->max_stack, ptr, len);
              READ_U2(method->max_locals, ptr, len);

              READ_U4(code_length, ptr, len);
              method->code = (char *)malloc(code_length);
              memcpy(method->code, ptr, code_length);
              ptr += code_length;

              READ_U2(method->exception_table_size, ptr, len);
              method->exception_table = (ExceptionTableEntry *)
                  malloc(method->exception_table_size*sizeof(ExceptionTableEntry));

              for(j = 0; j < method->exception_table_size; j++) {
                 ExceptionTableEntry *entry = &method->exception_table[j];              

                 READ_U2(entry->start_pc, ptr, len);
                 READ_U2(entry->end_pc, ptr, len);
                 READ_U2(entry->handler_pc, ptr, len);
                 READ_U2(entry->catch_type, ptr, len);
              }

              READ_U2(code_attr_cnt, ptr, len);
              for(; code_attr_cnt != 0; code_attr_cnt--) {
                 u2 attr_name_idx;
                 u4 attr_length;

                 READ_U2(attr_name_idx, ptr, len);
                 READ_U4(attr_length, ptr, len);
                 attr_name = CP_UTF8(constant_pool, attr_name_idx);

                 if(strcmp(attr_name, "LineNumberTable") == 0) {
                     READ_U2(method->line_no_table_size, ptr, len);
                     method->line_no_table = (LineNoTableEntry *)
                         malloc(method->line_no_table_size*sizeof(LineNoTableEntry));

		     for(j = 0; j < method->line_no_table_size; j++) {
                         LineNoTableEntry *entry = &method->line_no_table[j];              
			 
			 READ_U2(entry->start_pc, ptr, len);
			 READ_U2(entry->line_no, ptr, len);
		     }
		 } else
                     ptr += attr_length;
              }
           } else
              if(strcmp(attr_name, "Exceptions") == 0) {
                 int j;

                 READ_U2(method->throw_table_size, ptr, len);
                 method->throw_table = (u2 *)malloc(method->throw_table_size*sizeof(u2));
                 for(j = 0; j < method->throw_table_size; j++) {
                    READ_U2(method->throw_table[j], ptr, len);
                 }
              } else
                 ptr += attr_length;
        }
    }

    READ_U2(attr_count, ptr, len);
    for(; attr_count != 0; attr_count--) {
       u2 attr_name_idx;
       char *attr_name;
       u4 attr_length;

       READ_U2(attr_name_idx, ptr, len);
       READ_U4(attr_length, ptr, len);
       attr_name = CP_UTF8(constant_pool, attr_name_idx);

       if(strcmp(attr_name, "SourceFile") == 0) {
           u2 file_name_idx;
           READ_U2(file_name_idx, ptr, len);
	   classblock->source_file_name = CP_UTF8(constant_pool, file_name_idx);
       } else
           ptr += attr_length;
    }

    classblock->flags = CLASS_LOADED;

    found = addClassToHash(class);

    if(found != class)
        return found;

    classblock->super = super_idx ? resolveClass(class, super_idx, FALSE) : NULL;

    if(exceptionOccured())
       return NULL;

    if(strcmp(classblock->name,"java/lang/Class") == 0)
       class->class = class;
    else {
       if(java_lang_Class == NULL)
          java_lang_Class = loadSystemClass("java/lang/Class");
       class->class = java_lang_Class;
    }

    return class;
}

Class *
createArrayClass(char *classname, Object *class_loader) {
    Class *class;
    ClassBlock *classblock;
    int len = strlen(classname);

    if((class = allocClass()) == NULL)
        return NULL;

    classblock = CLASS_CB(class);

    /* Set all fields to zero */
    memset(classblock, 0, sizeof(ClassBlock));

    classblock->name = strcpy((char*)malloc(len+1), classname);
    classblock->super_name = "java/lang/Object";
    classblock->super = findSystemClass("java/lang/Object");
    classblock->method_table = CLASS_CB(classblock->super)->method_table;

    classblock->interfaces_count = 2;
    classblock->interfaces = (Class**)malloc(2*sizeof(Class*));
    classblock->interfaces[0] = findSystemClass("java/lang/Cloneable");
    classblock->interfaces[1] = findSystemClass("java/io/Serializable");
    
    classblock->flags = CLASS_INTERNAL;

    if(classname[len-1] == ';') {

        /* Element type is an object rather than a primitive type -
           find the element class and dimension and store in cb */

        char buff[256];
        char *ptr;
        int dim;

        /* Calculate dimension by counting the brackets */
        for(ptr = classname; *ptr == '['; ptr++);
        dim = ptr - classname;

        strcpy(buff, &classname[dim+1]);
        buff[len-dim-2] = '\0';

        classblock->element_class = findClassFromClassLoader(buff, class_loader);
        classblock->class_loader = CLASS_CB(classblock->element_class)->class_loader;
        classblock->dim = dim;
    }

    if(java_lang_Class == NULL)
      java_lang_Class = loadSystemClass("java/lang/Class");

    class->class = java_lang_Class;
    return addClassToHash(class);
}

Class *
createPrimClass(char *classname) {
    Class *class;
    ClassBlock *classblock;
    int i;
 
    if((class = allocClass()) == NULL)
        return NULL;

    classblock = CLASS_CB(class);
    classblock->name = strcpy((char*)malloc(strlen(classname)+1), classname);
    classblock->methods_count = 0;
    classblock->fields_count = 0;
    classblock->interfaces_count = 0;
    
    classblock->flags = CLASS_INTERNAL;

    if(java_lang_Class == NULL)
      java_lang_Class = loadSystemClass("java/lang/Class");

    class->class = java_lang_Class;

    lockHashTable(loaded_classes);

    for(i = 0; (i < num_prim) && (strcmp(CLASS_CB(prim_classes[i])->name, classname) == 0); i++);
    if(i == num_prim)
        prim_classes[num_prim++] = class;
    else
        class = prim_classes[i];

    unlockHashTable(loaded_classes);
    return class;
}

void linkClass(Class *class) {
   ClassBlock *cb = CLASS_CB(class);
   MethodBlock *mb = cb->methods;
   FieldBlock *fb = cb->fields;
   int spr_mthd_tbl_sze = 0;
   MethodBlock **spr_mthd_tbl;
   MethodBlock **method_table;
   MethodBlock *overridden;
   MethodBlock *finalizer = 0;
   int new_methods_count = 0;
   int offset = 0;
   int i;

   if(cb->flags >= CLASS_LINKED)
       return;

   if(verbose)
       printf("[Linking class %s]\n", cb->name);

   if(!(cb->access_flags & ACC_INTERFACE) && cb->super && (CLASS_CB(cb->super)->flags < CLASS_LINKED))
      linkClass(cb->super);

   if(cb->super) {
      offset = CLASS_CB(cb->super)->object_size;
      spr_mthd_tbl = CLASS_CB(cb->super)->method_table;
      spr_mthd_tbl_sze = CLASS_CB(cb->super)->method_table_size;
      finalizer = CLASS_CB(cb->super)->finalizer;
   }

   /* prepare fields */

   for(i = 0; i < cb->fields_count; i++,fb++) {
      if(fb->access_flags & ACC_STATIC) {
         /* init to default value */
         if((*fb->type == 'J') || (*fb->type == 'D'))
            *(long long *)&fb->static_value = 0;
         else
            fb->static_value = 0;
      } else {
         /* calc field offset */
         fb->offset = offset;
         if((*fb->type == 'J') || (*fb->type == 'D'))
             offset += 2;
         else
             offset += 1;
      }
   }

   cb->object_size = offset;

   /* prepare methods */

   for(i = 0; i < cb->methods_count; i++,mb++) {

       /* calculate argument count from signature */

       int count = 0;
       char *sig = mb->type;
       SCAN_SIG(sig, count+=2, count++);

       if(mb->access_flags & ACC_STATIC)
           mb->args_count = count;
       else
           mb->args_count = count + 1;

       mb->class = class;

       if(mb->access_flags & ACC_NATIVE) {

           /* set up native invoker to wrapper to resolve function 
              on first invocation */

           mb->native_invoker = (void *) resolveNativeWrapper;

           /* native methods have no code attribute so these aren't filled
              in at load time - as these values are used when creating frame
              set to appropriate values */

           mb->max_locals = mb->args_count;
           mb->max_stack = 0;
       }

       /* Static, private or init methods aren't dynamically invoked, so
	 don't stick them in the table to save space */

       if((mb->access_flags & ACC_STATIC) || (mb->access_flags & ACC_PRIVATE) ||
	       (mb->name[0] == '<'))
           continue;

       /* if it's overriding an inherited method, replace in method table */

       if(cb->super &&
             (overridden = lookupMethod(cb->super, mb->name, mb->type)))
           mb->method_table_index = overridden->method_table_index;
       else
           mb->method_table_index = spr_mthd_tbl_sze + new_methods_count++;

       /* check for finalizer */

       if(cb->super && strcmp(mb->name, "finalize") == 0 && strcmp(mb->type, "()V") == 0)
	   finalizer = mb;
   }

   cb->finalizer = finalizer;

   /* construct method table */

   cb->method_table_size = spr_mthd_tbl_sze + new_methods_count;
   method_table = cb->method_table = 
           (MethodBlock**)malloc(cb->method_table_size * sizeof(MethodBlock*));

   memcpy(method_table, spr_mthd_tbl, spr_mthd_tbl_sze * sizeof(MethodBlock*));

   /* fill in method table */

   mb = cb->methods;
   for(i = 0; i < cb->methods_count; i++,mb++) {
       if((mb->access_flags & ACC_STATIC) || (mb->access_flags & ACC_PRIVATE) ||
	       (mb->name[0] == '<'))
           continue;
       method_table[mb->method_table_index] = mb;
   }

   cb->flags = CLASS_LINKED;
}

Class *initClass(Class *class) {
   ClassBlock *cb = CLASS_CB(class);
   FieldBlock *fb = cb->fields;
   ConstantPool *cp = &cb->constant_pool;
   MethodBlock *mb;
   Object *excep;
   int i;

   linkClass(class);
   objectLock((Object *)class);

   while(cb->flags == CLASS_INITING)
      if(cb->initing_tid == threadSelf()->id)
          goto unlock;
      else
          objectWait((Object *)class);

   if(cb->flags == CLASS_INITED)
      goto unlock;

   if(cb->flags == CLASS_BAD) {
       objectUnlock((Object *)class);
       signalException("java/lang/NoClassDefFoundError", cb->name);
       return class;
   }

   cb->flags = CLASS_INITING;
   cb->initing_tid = threadSelf()->id;

   objectUnlock((Object *)class);

   if(!(cb->access_flags & ACC_INTERFACE) && cb->super && (CLASS_CB(cb->super)->flags != CLASS_INITED)) {
      initClass(cb->super);
      if(exceptionOccured()) {
          objectLock((Object *)class);
          cb->flags = CLASS_BAD;
          goto notify;
      }
   }

   if((mb = findMethod(class, "<clinit>", "()V")) != NULL)
      executeStaticMethod(class, mb);

   if(excep = exceptionOccured()) {
       Class *error, *eiie;
       Object *ob;

       clearException(); 

       /* Don't wrap exceptions of type java/lang/Error...  Sun VM
	* doesn't appear to do this, and it leads to errors such as
	* UnsatisfiedLinkError being reported as ExceptionInInit... */

       if((error = findSystemClass0("java/lang/Error")) && !isInstanceOf(error, excep->class)
                 && (eiie = findSystemClass("java/lang/ExceptionInInitializerError"))
                 && (mb = findMethod(eiie, "<init>", "(Ljava/lang/Throwable;)V"))
                 && (ob = allocObject(eiie))) {
           executeMethod(ob, mb, excep);
           setException(ob);
       } else
           setException(excep);

       objectLock((Object *)class);
       cb->flags = CLASS_BAD;
   } else {
       objectLock((Object *)class);
       cb->flags = CLASS_INITED;
   }
   
notify:
   objectNotifyAll((Object *)class);

unlock:
   objectUnlock((Object *)class);
   return class;
}

Class *loadSystemClass(char *classname) {
    char filename[256] = "/";
    char buff[256];
    char *data;
    char **cp_ptr;
    FILE *cfd;
    int  flen;
    Class *class;

    strcat(strcat(filename, classname),".class");
    for(cp_ptr = classpath;
        *cp_ptr && !(cfd = fopen(strcat(strcpy(buff, *cp_ptr), filename),"r"));
        cp_ptr++);

    if(!*cp_ptr) {
        signalException("java/lang/NoClassDefFoundError", classname);
        return NULL;
    }

    fseek(cfd, 0L, SEEK_END);
    flen = ftell(cfd);
    fseek(cfd, 0L, SEEK_SET);

    data = (char *)malloc(flen);
    if(fread(data, sizeof(char), flen, cfd) != flen) {
        signalException("java/lang/NoClassDefFoundError", classname);
        return NULL;
    }

    class = defineClass(data, 0, flen, NULL);
    free(data);

   if(verbose)
       printf("[Loaded %s]\n", buff);

    return class;
}

Class *findHashedClass(char *classname, Object *class_loader) {
   Class *class;

#undef HASH
#undef COMPARE
#define HASH(ptr) utf8Hash(ptr)
#define COMPARE(ptr1, ptr2, hash1, hash2) (hash1 == hash2) && \
                     utf8Comp(ptr1, CLASS_CB((Class *)ptr2)->name) && \
                     (CLASS_CB((Class *)ptr2)->class_loader == class_loader)

   findHashEntry(loaded_classes, classname, class, FALSE, FALSE);

   return class;
}

Class *findSystemClass0(char *classname) {
   Class *class = findHashedClass(classname, NULL);

   if(class == NULL)
       class = loadSystemClass(classname);

   if(!exceptionOccured())
       linkClass(class);

   return class;
}

Class *findSystemClass(char *classname) {
   Class *class = findSystemClass0(classname);

   if(!exceptionOccured())
       initClass(class);

   return class;
}

Class *findArrayClassFromClassLoader(char *classname, Object *class_loader) {
   Class *class = findHashedClass(classname, class_loader);

   if(class == NULL)
       class = createArrayClass(classname, class_loader);

   return class;
}

Class *findPrimClass(char *classname) {
   int i;
   Class *prim = NULL;

   lockHashTable(loaded_classes);
   for(i = 0; (i < num_prim) && (strcmp(CLASS_CB(prim_classes[i])->name, classname) == 0); i++);
   if(i < num_prim)
       prim = prim_classes[i];

   unlockHashTable(loaded_classes);
   return prim ? prim : createPrimClass(classname);
}

Class *findClassFromClassLoader(char *classname, Object *loader) {
    if(*classname == '[')
        return findArrayClassFromClassLoader(classname, loader);

    if(loader != NULL) {
        Class *class;
	char *dot_name = slash2dots(classname);
        MethodBlock *mb = lookupMethod(loader->class,
			"loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        Object *string = createString(dot_name);
	free(dot_name);

        class = *(Class**)executeMethod(loader, mb, string);

	if(exceptionOccured())
            return NULL;

	return class;
    }

    return findSystemClass0(classname);
}

/* gc support for marking classes */

#define ITERATE(ptr)  markClass(ptr)

extern void markClass(Class *ob);
 
void markClasses() {
   int i;

   hashIterate(loaded_classes);

   for(i = 0; i < num_prim; i++)
       markClass(prim_classes[i]);
}

int parseClassPath(char *cp_var) {
    char *cp, *pntr, *start;
    int i;

    cp = (char*)malloc(strlen(cp_var)+1);
    strcpy(cp, cp_var);

    for(i = 0, start = pntr = cp; *pntr; pntr++) {
        if(*pntr == ':') {
	    if(start != pntr)
	        i++;
	    start = pntr+1;
        }
    }
    if(start != pntr)
        i++;

    classpath = (char **)malloc(sizeof(char*)*(i+1));

    for(i = 0, start = pntr = cp; *pntr; pntr++) {
        if(*pntr == ':') {
	    if(start != pntr) {
                *pntr = '\0';
	        classpath[i++] = start;
            }
	    start = pntr+1;
        }
    }
    if(start != pntr)
        classpath[i++] = start;

    classpath[i] = NULL;
    return i;
}

char *getClassPath() {
    return getenv("CLASSPATH");
}

void initialiseClass(int verboseclass) {
    char *cp = getClassPath();
    if(!(cp && parseClassPath(cp))) {
        printf("Classpath variable is empty!\n");
	exit(1);
    }
    verbose = verboseclass;
    initHashTable(loaded_classes, INITSZE);
}
