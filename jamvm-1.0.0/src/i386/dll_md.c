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
#include "../jam.h"
#include "../sig.h"

int extraArgSpace(MethodBlock *mb) {
    return (mb->access_flags & ACC_STATIC ? mb->args_count+1 : mb->args_count) + 1;
}

u4 *callJNIMethod(void *env, Class *class, char *sig, int extra, u4 *ostack, unsigned char *f) {
    u4 args[extra];
    u4 *opntr = ostack;
    u4 *apntr = &args[2];
   
    args[0] = (u4)env;
    args[1] = class ? (u4)class : *opntr++;

    //SCAN_SIG(sig, *((u8*)apntr)++ = *((u8*)opntr)++, *apntr++ = *opntr++);
     sig++; 
     while(*sig != ')') { 
       if((*sig == 'J') || (*sig == 'D')) { 
	 //*((u8*)apntr)++ = *((u8*)opntr)++; 
	 u8 *left = (u8 *)apntr;
	 u8 *right = (u8 *)opntr;
	 
	 *left = *right;
	 left++;
	 right++;
	 apntr = left;
	 opntr = right;
	 sig++; 
       } 
       else { 
	 *apntr++ = *opntr++; 
	 if(*sig == '[') 
	   for(sig++; *sig == '['; sig++)
	     ; 
	 if(*sig == 'L') 
	   while(*sig++ != ';')
	     ; 
	 else 
	   sig++; 
       } 
     } 
     sig++;;

     switch(*sig) {
        case 'V':
            (*(void (*)())f)();
            break;

    case 'D':{
      //*((double*)ostack)++ = (*(double (*)())f)();
      double *temp = (double *)ostack;
      *temp = (*(double (*)())f)();
      temp++;
      ostack = (u4 *)temp;
      break;
    }
    case 'F':{
      //*((float*)ostack)++ = (*(float (*)())f)();
       float *temp = (float *)ostack;
      *temp = (*(float (*)())f)();
      temp++;
      ostack = (u4 *)temp;
      break;
    }
    case 'J':{
      //*((long long*)ostack)++ = (*(long long (*)())f)();
      long long *temp = (long long *)ostack;
      *temp = (*(long long (*)())f)();
      temp++;
      ostack = (u4 *)temp;
      break;
    }
        default:
            *ostack++ = (*(u4 (*)())f)();
            break;
    }

    return ostack;
}
