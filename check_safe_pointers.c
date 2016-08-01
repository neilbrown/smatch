/*
 * Copyright (C) 2016 NeilBrown <neil@brown.name>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */


#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static void match_dereferences(struct expression *expr)
{
	char *name;
	struct symbol *type;

	if (expr->type != EXPR_PREOP)
		return;

	expr = expr->unop;

	if (implied_not_equal(expr, 0))
		return;

	type = get_real_type(expr);
	if (type && (type->ctype.modifiers & MOD_SAFE))
		return;

	name = expr_to_str(expr);
	sm_msg("Possible NULL dereference found: %s", name);

	free_string(name);
}

void check_safe_pointers(int id)
{
	my_id = id;

	if (getenv("SMATCH_CHECK_SAFE") == NULL)
		return;

	add_hook(&match_dereferences, DEREF_HOOK);
}
