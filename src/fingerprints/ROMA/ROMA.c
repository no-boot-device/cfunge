/* -*- mode: C; coding: utf-8; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*-
 *
 * cfunge - a conformant Befunge93/98/08 interpreter in C.
 * Copyright (C) 2008 Arvid Norlander <anmaster AT tele2 DOT se>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at the proxy's option) any later version. Arvid Norlander is a
 * proxy who can decide which future versions of the GNU General Public
 * License can be used.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ROMA.h"
#include "../../stack.h"


#define ROMAPUSH(x, y) \
	static void FingerROMApush ## x (instructionPointer * ip) \
	{ \
		StackPush(ip->stack, (FUNGEDATATYPE)y); \
	}

ROMAPUSH(I, 1)
ROMAPUSH(V, 5)
ROMAPUSH(X, 10)
ROMAPUSH(L, 50)
ROMAPUSH(C, 100)
ROMAPUSH(D, 500)
ROMAPUSH(M, 1000)



bool FingerROMAload(instructionPointer * ip)
{
	ManagerAddOpcode(ROMA,  'C', pushC)
	ManagerAddOpcode(ROMA,  'D', pushD)
	ManagerAddOpcode(ROMA,  'I', pushI)
	ManagerAddOpcode(ROMA,  'L', pushL)
	ManagerAddOpcode(ROMA,  'M', pushM)
	ManagerAddOpcode(ROMA,  'V', pushV)
	ManagerAddOpcode(ROMA,  'X', pushX)
	return true;
}
