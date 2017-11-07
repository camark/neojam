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

#define COMPARE_AND_SWAP(addr, old_val, new_val) \
({                                               \
    int result, read_val;                        \
    __asm__ __volatile__ ("                      \
        	li %0,0;                         \
        1:	lwarx %1,0,%2;                   \
		cmpw %3,%1;                      \
		bne- 2f;                         \
		stwcx. %4,0,%2;                  \
		bne- 1b;                         \
		li %0,1;                         \
        2:"                                      \
    : "=&r" (result), "=&r" (read_val)           \
    : "r" (addr), "r" (old_val), "r" (new_val)   \
    : "cc", "memory");                           \
    result;                                      \
})
