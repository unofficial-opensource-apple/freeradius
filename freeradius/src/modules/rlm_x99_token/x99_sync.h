/*
 * x99_sync.h
 * $Id: x99_sync.h,v 1.2 2005/10/28 23:34:08 snsimon Exp $
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright 2001,2002  Google, Inc.
 */

#ifndef X99_SYNC_H
#define X99_SYNC_H

static int x99_get_failcount(const char *syncdir, const char *username,
			     int *failcount);
static char * x99_acquire_sd_lock(const char *syncdir, const char *username);
static void x99_release_sd_lock(char *lockfile);

static int x99_get_sd(const char *syncdir, const char *username,
		      char challenge[MAX_CHALLENGE_LEN + 1], int *failures,
		      time_t *last_async, unsigned *pos);
static int x99_set_sd(const char *syncdir, const char *username,
		      const char *challenge, int failures, time_t last_async,
		      unsigned pos);

#endif /* X99_SYNC_H */
