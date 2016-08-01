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

STATE(safe);

static int is_safe(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (type && (type->ctype.modifiers & MOD_SAFE))
		return 1;

	return 0;
}

static int is_safe_expr(struct expression *expr)
{
	struct smatch_state *st;

	st = get_state_expr(my_id, expr);
	if (st == &safe)
		return 1;

	if (implied_not_equal(expr, 0))
		return 1;

	if (is_safe(expr))
		return 1;

	return 0;
}

static void match_dereferences(struct expression *expr)
{
	char *name;

	if (expr->type != EXPR_PREOP)
		return;

	expr = expr->unop;

	if (is_safe_expr(expr))
		return;

	name = expr_to_str(expr);
	sm_msg("Possible NULL dereference found: %s", name);

	free_string(name);
}

static void match_assign(struct expression *expr)
{

	if (expr->op != '=')
		/* could be '+=' etc */
		return;
	if (is_fake_call(expr->right) || __in_fake_assign)
		/* Fake assignments included the change in
		 * "*a->foo" when you assign to 'a'
		 */
		return;

	if (is_safe_expr(expr->right))
		set_state_expr(my_id, expr->left, &safe);
	else
		set_state_expr(my_id, expr->left, &undefined);
}

void check_safe_pointers(int id)
{
	my_id = id;

	if (getenv("SMATCH_CHECK_SAFE") == NULL)
		return;

	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
