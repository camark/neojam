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

MethodBlock *findMethod(Class *class, char *methodname, char *type) {
   ClassBlock *cb = CLASS_CB(class);
   MethodBlock *mb = cb->methods;
   int i;

   for(i = 0; i < cb->methods_count; i++,mb++)
       if((strcmp(mb->name, methodname) == 0) && (strcmp(mb->type, type) == 0))
          return mb;

   return NULL;
}

/* A class can't have two fields with the same name but different types - 
   so we give up if we find a field with the right name but wrong type...
*/
FieldBlock *findField(Class *class, char *fieldname, char *type) {
    ClassBlock *cb = CLASS_CB(class);
    FieldBlock *fb = cb->fields;
    int i;

    for(i = 0; i < cb->fields_count; i++,fb++)
       if(strcmp(fb->name, fieldname) == 0)
          if(strcmp(fb->type, type) == 0)
              return fb;
          else
	      return NULL;

    return NULL;
}

MethodBlock *lookupMethod(Class *class, char *methodname, char *type) {
    MethodBlock *mb;

    if(mb = findMethod(class, methodname, type))
       return mb;

    if (CLASS_CB(class)->super)
        return lookupMethod(CLASS_CB(class)->super, methodname, type);

    return NULL;
}

FieldBlock *lookupField(Class *class, char *fieldname, char *type) {
    FieldBlock *fb;

    if(fb = findField(class, fieldname, type))
       return fb;

    if (CLASS_CB(class)->super)
        return lookupField(CLASS_CB(class)->super, fieldname, type);

    return NULL;
}

Class *resolveClass(Class *class, int cp_index, int init) {
    ConstantPool *cp = &(CLASS_CB(class)->constant_pool);
    Class *resolved_class;

retry:
    switch(CP_TYPE(cp, cp_index)) {
	case CONSTANT_Locked:
	    goto retry;

        case CONSTANT_Resolved:
            resolved_class = (Class *)CP_INFO(cp, cp_index);
	    break;

	case CONSTANT_Class: {
	    char *classname;
            int name_idx = CP_CLASS(cp, cp_index);

	    if(CP_TYPE(cp, cp_index) != CONSTANT_Class)
                goto retry;

            classname = CP_UTF8(cp, name_idx);
            resolved_class = findClassFromClass(classname, class);

            /* If we can't find the class an exception will already have
               been thrown */

            if(resolved_class == NULL)
                return NULL;

            CP_TYPE(cp, cp_index) = CONSTANT_Locked;
            CP_INFO(cp, cp_index) = (u4)resolved_class;
            CP_TYPE(cp, cp_index) = CONSTANT_Resolved;

	    break;
        }
    }

    if(init)
        initClass(resolved_class);

    return resolved_class;
}

MethodBlock *resolveMethod(Class *class, int cp_index) {
    ConstantPool *cp = &(CLASS_CB(class)->constant_pool);
    MethodBlock *mb;

retry:
    switch(CP_TYPE(cp, cp_index)) {
	case CONSTANT_Locked:
	    goto retry;

        case CONSTANT_Resolved:
            mb = (MethodBlock *)CP_INFO(cp, cp_index);
	    break;

        case CONSTANT_Methodref: {
	    Class *resolved_class;
            char *methodname, *methodtype;
            int cl_idx = CP_METHOD_CLASS(cp, cp_index);
            int name_type_idx = CP_METHOD_NAME_TYPE(cp, cp_index);

	    if(CP_TYPE(cp, cp_index) != CONSTANT_Methodref)
                goto retry;

            methodname = CP_UTF8(cp, CP_NAME_TYPE_NAME(cp, name_type_idx));
            methodtype = CP_UTF8(cp, CP_NAME_TYPE_TYPE(cp, name_type_idx));
            resolved_class = resolveClass(class, cl_idx, TRUE);

            if(exceptionOccured())
                return NULL;

            mb = lookupMethod(resolved_class, methodname, methodtype);

            if(mb) {
                CP_TYPE(cp, cp_index) = CONSTANT_Locked;
                CP_INFO(cp, cp_index) = (u4)mb;
                CP_TYPE(cp, cp_index) = CONSTANT_Resolved;
            } else
                signalException("java/lang/NoSuchMethodError", methodname);

	    break;
        }
    }

    return mb;
}

MethodBlock *resolveInterfaceMethod(Class *class, int cp_index) {
    ConstantPool *cp = &(CLASS_CB(class)->constant_pool);
    MethodBlock *mb;

retry:
    switch(CP_TYPE(cp, cp_index)) {
	case CONSTANT_Locked:
	    goto retry;

        case CONSTANT_Resolved:
            mb = (MethodBlock *)CP_INFO(cp, cp_index);
	    break;

        case CONSTANT_InterfaceMethodref: {
	    Class *resolved_class;
            char *methodname, *methodtype;
            int cl_idx = CP_METHOD_CLASS(cp, cp_index);
            int name_type_idx = CP_METHOD_NAME_TYPE(cp, cp_index);

	    if(CP_TYPE(cp, cp_index) != CONSTANT_InterfaceMethodref)
                goto retry;

            methodname = CP_UTF8(cp, CP_NAME_TYPE_NAME(cp, name_type_idx));
            methodtype = CP_UTF8(cp, CP_NAME_TYPE_TYPE(cp, name_type_idx));
            resolved_class = resolveClass(class, cl_idx, TRUE);

            if(exceptionOccured())
                return NULL;

            mb = lookupMethod(resolved_class, methodname, methodtype);

            if(mb) {
                CP_TYPE(cp, cp_index) = CONSTANT_Locked;
                CP_INFO(cp, cp_index) = (u4)mb;
                CP_TYPE(cp, cp_index) = CONSTANT_Resolved;
            } else
                signalException("java/lang/NoSuchMethodError", methodname);

	    break;
        }
    }

    return mb;
}

FieldBlock *resolveField(Class *class, int cp_index) {
    ConstantPool *cp = &(CLASS_CB(class)->constant_pool);
    FieldBlock *fb;

retry:
    switch(CP_TYPE(cp, cp_index)) {
	case CONSTANT_Locked:
	    goto retry;

        case CONSTANT_Resolved:
            fb = (FieldBlock *)CP_INFO(cp, cp_index);
	    break;

        case CONSTANT_Fieldref: {
	    Class *resolved_class;
            char *fieldname, *fieldtype;
            int cl_idx = CP_FIELD_CLASS(cp, cp_index);
            int name_type_idx = CP_FIELD_NAME_TYPE(cp, cp_index);

	    if(CP_TYPE(cp, cp_index) != CONSTANT_Fieldref)
                goto retry;

            fieldname = CP_UTF8(cp, CP_NAME_TYPE_NAME(cp, name_type_idx));
            fieldtype = CP_UTF8(cp, CP_NAME_TYPE_TYPE(cp, name_type_idx));
            resolved_class = resolveClass(class, cl_idx, TRUE);

            if(exceptionOccured())
                return NULL;

            fb = lookupField(resolved_class, fieldname, fieldtype);

            if(fb) {
                CP_TYPE(cp, cp_index) = CONSTANT_Locked;
                CP_INFO(cp, cp_index) = (u4)fb;
                CP_TYPE(cp, cp_index) = CONSTANT_Resolved;
            } else
                signalException("java/lang/NoSuchFieldError", fieldname);

	    break;
        }
    }
 
    return fb;
}

u4 resolveSingleConstant(Class *class, int cp_index) {
    ConstantPool *cp = &(CLASS_CB(class)->constant_pool);

retry:
    switch(CP_TYPE(cp, cp_index)) {
	case CONSTANT_Locked:
	    goto retry;

	case CONSTANT_String: {
            Object *string;
            int idx = CP_STRING(cp, cp_index);
            if(CP_TYPE(cp, cp_index) != CONSTANT_String)
                goto retry;

            string = createString(CP_UTF8(cp, idx));
            CP_TYPE(cp, cp_index) = CONSTANT_Locked;
            CP_INFO(cp, cp_index) = (u4)findInternedString(string);
            CP_TYPE(cp, cp_index) = CONSTANT_Resolved;
            break;
        }

        default:
            break;
    }
    
    return CP_INFO(cp, cp_index);
}
