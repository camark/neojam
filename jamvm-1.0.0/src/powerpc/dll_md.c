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

int extraArgSpace(MethodBlock *mb) {
    char *sig = mb->type;
    int iargs = 0;
    int fargs = 0;
    int margs = 0;

#ifdef DEBUG_DLL
    printf("ExtraArgSpace: sig %s\n", sig);
#endif

    while(*++sig != ')')
        switch(*sig) {
            case 'J':
                iargs = (iargs + 3) & ~1;
                if(iargs > 8)
                    margs = (margs + 3) & ~1;
                break;

            case 'D':
                if(++fargs > 8)
                    margs = (margs + 3) & ~1;
                break;

            case 'F':
                if(++fargs > 8)
                    margs++;
                break;

            default:
                if(++iargs > 8)
                    margs++;

                if(*sig == '[')
                    while(*++sig == '[');
                if(*sig == 'L')
                    while(*++sig != ';');
                break;
        }

    return margs;
}
