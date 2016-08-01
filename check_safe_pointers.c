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

	type = get_real_type(expr);
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

	if (is_safe(expr->left) && !is_safe_expr(expr->right)) {
		char *namel, *namer;

		namel = expr_to_str(expr->left);
		namer = expr_to_str(expr->right);
		sm_msg("'%s' can only take 'safe' pointers, not '%s'",
		       namel, namer);
		free_string(namel);
		free_string(namer);
	}
}

static void match_call(struct expression *call)
{
	struct expression *arg;
	struct symbol *type;
	char *name;
	int i;

	i = -1;
	FOR_EACH_PTR(call->args, arg) {
		i++;
		type = get_real_arg_type(call->fn, i);

		if (!type)
			/* We've reached the variable part of
			 * a var-args function call.
			 */
			break;
		if (!(type->ctype.modifiers & MOD_SAFE))
			continue;

		if (is_safe_expr(arg))
			continue;

		name = expr_to_str(arg);
		sm_msg("parameter %d requires safe value, not '%s'", i+1, name);
		free_string(name);
	} END_FOR_EACH_PTR(arg);
}

static char *unsafe = NULL;
static int unsafe_line;
static void match_return(struct expression *ret_value)
{
	if (__inline_fn)
		return;
	if (!ret_value)
		return;
	if (unsafe)
		return;
	if (!is_safe_expr(ret_value)) {
		unsafe = expr_to_str(ret_value);
		unsafe_line = ret_value->pos.line;
	}
}

static void match_func_end(struct symbol *sym)
{
	if (__inline_fn)
		return;
	if (unsafe && (sym->ctype.modifiers & MOD_SAFE))
		sm_msg("function %s returns unsafe '%s' at line %d",
		       sym->ident->name, unsafe, unsafe_line);
	free_string(unsafe);
	unsafe = NULL;
}

void check_safe_pointers(int id)
{
	my_id = id;

	if (getenv("SMATCH_CHECK_SAFE") == NULL)
		return;

	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_func_end, END_FUNC_HOOK);
}
