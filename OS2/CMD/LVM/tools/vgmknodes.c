/*
 * Copyright (C) 2003-2004 Sistina Software, Inc. All rights reserved. 
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is part of LVM2.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "tools.h"

static int _vgmknodes_single(struct cmd_context *cmd, struct logical_volume *lv,
			     void *handle) // __attribute((unused)))
{
	if (!lv_mknodes(cmd, lv))
		return ECMD_FAILED;

	return ECMD_PROCESSED;
}

int vgmknodes(struct cmd_context *cmd, int argc, char **argv)
{
	int r;

	r = process_each_lv(cmd, argc, argv, LCK_VG_READ, NULL,
			    &_vgmknodes_single);

	if (!lv_mknodes(cmd, NULL) && (r < ECMD_FAILED))
		r = ECMD_FAILED;

	return r;
}
