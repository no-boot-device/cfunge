/* -*- mode: C; coding: utf-8; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*-
 *
 * cfunge - A standard-conforming Befunge93/98/109 interpreter in C.
 * Copyright (C) 2008-2013 Arvid Norlander <VorpalBlade AT users.noreply.github.com>
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

#include "FILE.h"
#include "../../stack.h"
#include "../../../lib/stringbuffer/stringbuffer.h"
#include "../../diagnostic.h"

#include <assert.h>
#include <stdio.h> /* fclose, fopen, fread, fwrite ... */
#include <unistd.h> /* fcntl, unlink */
#include <fcntl.h> /* fcntl */

// Based on how CCBI does it.

typedef struct sFungeFileHandle {
	FILE      * file;
	funge_vector buffvect; // IO buffer in Funge-Space
} FungeFileHandle;

#define ALLOCCHUNK 2
// Array of pointers
static FungeFileHandle** handles = NULL;
static size_t maxHandle = 0;

/// Used by allocate_handle() below to find next free handle.
FUNGE_ATTR_FAST FUNGE_ATTR_WARN_UNUSED
static inline funge_cell find_next_free_handle(void)
{
	for (size_t i = 0; i < maxHandle; i++) {
		if (handles[i] == NULL)
			return (funge_cell)i;
	}
	// No free one, extend array..
	{
		FungeFileHandle** newlist = (FungeFileHandle**)realloc(handles, (maxHandle + ALLOCCHUNK) * sizeof(FungeFileHandle*));
		if (!newlist)
			return -1;
		handles = newlist;
		for (size_t i = maxHandle; i < (maxHandle + ALLOCCHUNK); i++)
			handles[i] = NULL;
		maxHandle += ALLOCCHUNK;
		return (funge_cell)(maxHandle - ALLOCCHUNK);
	}
}

/// Get a new handle to use for a file, also allocates buffer for it.
/// @return Handle, or -1 on failure
FUNGE_ATTR_FAST FUNGE_ATTR_WARN_UNUSED
static inline funge_cell allocate_handle(void)
{
	funge_cell h;

	h = find_next_free_handle();
	if (h < 0)
		return -1;

	handles[h] = malloc(sizeof(FungeFileHandle));
	if (!handles[h])
		return -1;
	handles[h]->file = NULL;
	return h;
}

/// Free a handle. fclose() the file before calling this.
FUNGE_ATTR_FAST
static inline void free_handle(funge_cell h)
{
	if (!handles[h])
		return;
	// Should be closed first!
	if (handles[h]->file != NULL) {
		handles[h]->file = NULL;
	}
	free(handles[h]);
	handles[h] = NULL;
}

/// Checks if handle is valid.
FUNGE_ATTR_FAST FUNGE_ATTR_WARN_UNUSED
static inline bool valid_handle(funge_cell h)
{
	if ((h < 0) || ((size_t)h >= maxHandle) || (!handles[h])) {
		return false;
	} else {
		return true;
	}
}

/// C - Close a file
static void finger_FILE_fclose(instructionPointer * ip)
{
	funge_cell h;

	h = stack_pop(ip->stack);
	if (!valid_handle(h)) {
		ip_reverse(ip);
		return;
	}

	if (fclose(handles[h]->file) != 0)
		ip_reverse(ip);

	free_handle(h);
}

/// D - Delete specified file
static void finger_FILE_delete(instructionPointer * ip)
{
	char * restrict filename;

	filename = (char*)stack_pop_string(ip->stack, NULL);
	if (!filename || (unlink(filename) != 0)) {
		ip_reverse(ip);
	}

	stack_free_string(filename);
}


/// G - Get string from file (like c fgets)
static void finger_FILE_fgets(instructionPointer * ip)
{
	funge_cell h;
	FILE * fp;

	h = stack_peek(ip->stack);
	if (!valid_handle(h)) {
		ip_reverse(ip);
		return;
	}

	fp = handles[h]->file;

	{
		StringBuffer *sb;
		int ch;
		sb = stringbuffer_new();
		if (!sb) {
			ip_reverse(ip);
			return;
		}

		while (true) {
			ch = fgetc(fp);
			switch (ch) {
				case '\r':
					stringbuffer_append_char(sb, (char)ch);
					ch = fgetc(fp);
					if (ch != '\n') {
						ungetc(ch, fp);
						goto endofloop;
					}
				// Fallthrough intentional.
				case '\n':
					stringbuffer_append_char(sb, (char)ch);
					goto endofloop;

				case EOF:
					if (ferror(fp)) {
						clearerr(fp);
						ip_reverse(ip);
						stringbuffer_destroy(sb);
						return;
					} else {
						goto endofloop;
					}

				default:
					stringbuffer_append_char(sb, (char)ch);
					break;
			}
		}
	// Yeah, can't break two levels otherwise...
	endofloop: {
			char * str;
			size_t len;
			str = stringbuffer_finish(sb, &len);
			stack_push_string(ip->stack, (unsigned char*)str, len);
			stack_push(ip->stack, (funge_cell)len);
			free(str);
			return;
		}
	}
}

/// L - Get current location in file
static void finger_FILE_ftell(instructionPointer * ip)
{
	funge_cell h;
	long pos;

	h = stack_peek(ip->stack);
	if (!valid_handle(h)) {
		ip_reverse(ip);
		return;
	}

	pos = ftell(handles[h]->file);

	if (pos == -1) {
		clearerr(handles[h]->file);
		ip_reverse(ip);
		return;
	}

	stack_push(ip->stack, (funge_cell)pos);
}

/// Lookup table below for modes below.
static const char* const mode_table[] = {
	/*0*/"rb",
	/*1*/"wb",
	/*2*/"ab",
	/*3*/"r+b",
	/*4*/"w+b",
	/*5*/"a+b",
};

/// O - Open a file (Va = i/o buffer vector)
static void finger_FILE_fopen(instructionPointer * ip)
{
	char * restrict filename;
	funge_cell mode;
	funge_vector vect;
	funge_cell h;

	filename = (char*)stack_pop_string(ip->stack, NULL);
	if (FUNGE_UNLIKELY(!filename))
		goto error;
	mode = stack_pop(ip->stack);
	vect = stack_pop_vector(ip->stack);

	if (FUNGE_UNLIKELY((mode < 0) || (mode > 5))) {
		goto error;
	}

	h = allocate_handle();
	if (FUNGE_UNLIKELY(h == -1)) {
		goto error;
	}

	handles[h]->file = fopen(filename, mode_table[mode]);

	if (FUNGE_UNLIKELY(!handles[h]->file)) {
		free_handle(h);
		goto error;
	}
	if (fcntl(fileno(handles[h]->file), F_SETFD, FD_CLOEXEC, 1) != 0)
	{
		fclose(handles[h]->file);
		free_handle(h);
		goto error;
	}
	if ((mode == 2) || (mode == 5))
		rewind(handles[h]->file);

	handles[h]->buffvect = vect;
	stack_push(ip->stack, h);
	goto end;
// Look... The alternatives to the goto were worse...
error:
	ip_reverse(ip);
end:
	stack_free_string(filename);
}

/// P - Put string to file (like c fputs)
static void finger_FILE_fputs(instructionPointer * ip)
{
	char * restrict str;
	funge_cell h;

	str = (char*)stack_pop_string(ip->stack, NULL);
	h = stack_peek(ip->stack);
	if (!valid_handle(h) || !str) {
		ip_reverse(ip);
	} else {
		if (fputs(str, handles[h]->file) == EOF) {
			clearerr(handles[h]->file);
			ip_reverse(ip);
		}
	}
	stack_free_string(str);
}

/// R - Read n bytes from file to i/o buffer
static void finger_FILE_fread(instructionPointer * ip)
{
	funge_cell n, h;

	n = stack_pop(ip->stack);
	h = stack_peek(ip->stack);

	if (!valid_handle(h)) {
		ip_reverse(ip);
		return;
	}

	if (n <= 0) {
		ip_reverse(ip);
		return;
	}
	{
		size_t bytes_read;
		FILE * fp = handles[h]->file;
		unsigned char * restrict buf = malloc((size_t)n * sizeof(unsigned char));
		if (!buf) {
			ip_reverse(ip);
			return;
		}

		if ((bytes_read = fread(buf, sizeof(unsigned char), (size_t)n, fp)) != (size_t)n) {
			// Reverse on less bytes read, but if feof() also write out to funge space below.
			ip_reverse(ip);
			if (ferror(fp)) {
				clearerr(fp);
				free(buf);
				return;
			}
		}
		{
			funge_vector v = handles[h]->buffvect;
			for (size_t i = 0; i < bytes_read; i++) {
				fungespace_set(buf[i], &v);
				v.x++;
			}
		}
		free(buf);
	}
}

/// S - Seek to position in file
static void finger_FILE_fseek(instructionPointer * ip)
{
	funge_cell n, m, h;

	n = stack_pop(ip->stack);
	m = stack_pop(ip->stack);
	h = stack_peek(ip->stack);

	if (!valid_handle(h)) {
		ip_reverse(ip);
		return;
	}

	switch (m) {
		case 0:
			if (fseek(handles[h]->file, (long)n, SEEK_SET) != 0)
				break;
			else
				return;
		case 1:
			if (fseek(handles[h]->file, (long)n, SEEK_CUR) != 0)
				break;
			else
				return;
		case 2:
			if (fseek(handles[h]->file, (long)n, SEEK_END) != 0)
				break;
			else
				return;
		default:
			break;
	}
	// An error if we got here...
	clearerr(handles[h]->file);
	ip_reverse(ip);
}

/// W - Write n bytes from i/o buffer to file
static void finger_FILE_fwrite(instructionPointer * ip)
{
	funge_cell n, h;

	n = stack_pop(ip->stack);
	h = stack_peek(ip->stack);

	if (!valid_handle(h)) {
		ip_reverse(ip);
		return;
	}

	if (n <= 0) {
		ip_reverse(ip);
		return;
	}
	{
		FILE * fp = handles[h]->file;
		funge_vector v = handles[h]->buffvect;
		unsigned char * restrict buf = malloc((size_t)n * sizeof(unsigned char));
		if (FUNGE_UNLIKELY(!buf))
			DIAG_OOM("Failed to allocate buffer");
		for (funge_cell i = 0; i < n; i++) {
			buf[i] = (unsigned char)fungespace_get(&v);
			v.x++;
		}
		if (fwrite(buf, sizeof(unsigned char), (size_t)n, fp) != (size_t)n) {
			if (ferror(fp)) {
				clearerr(fp);
				ip_reverse(ip);
			}
		}
		free(buf);
	}
}

FUNGE_ATTR_FAST static inline bool init_handle_list(void)
{
	assert(!handles);
	handles = (FungeFileHandle**)calloc(ALLOCCHUNK, sizeof(FungeFileHandle*));
	if (!handles)
		return false;
	maxHandle = ALLOCCHUNK;
	return true;
}

bool finger_FILE_load(instructionPointer * ip)
{
	if (!handles)
		if (!init_handle_list())
			return false;

	manager_add_opcode(FILE, 'C', fclose);
	manager_add_opcode(FILE, 'D', delete);
	manager_add_opcode(FILE, 'G', fgets);
	manager_add_opcode(FILE, 'L', ftell);
	manager_add_opcode(FILE, 'O', fopen);
	manager_add_opcode(FILE, 'P', fputs);
	manager_add_opcode(FILE, 'R', fread);
	manager_add_opcode(FILE, 'S', fseek);
	manager_add_opcode(FILE, 'W', fwrite);
	return true;
}
