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
	if (!type)
		return 0;
	if (is_ptr_type(type) && type->ctype.modifiers & MOD_SAFE)
		return 1;

	return 0;
}

static int is_safe_expr(struct expression *expr);

static int is_field_address(struct expression *expr)
{
	/* See if 'expr' is the address of a pointer to a
	 * safe expression, possibly with some member accesses and
	 * casts in the mix
	 */
	expr = strip_expr(expr);
	if (expr->type != EXPR_PREOP || expr->op != '&')
		return 0;
	expr = strip_expr(expr->unop);
	while (expr->type == EXPR_DEREF)
		expr = strip_expr(expr->deref);
	if (expr->type != EXPR_PREOP || expr->op != '*')
		return 0;
	return is_safe_expr(expr->unop);
}

static int is_safe_expr(struct expression *expr)
{
	struct smatch_state *st;
	struct symbol *type;

	st = get_state_expr(my_id, expr);
	if (st == &safe)
		return 1;

	if (implied_not_equal(expr, 0))
		return 1;

	expr = strip_parens(expr);
	if (expr->type == EXPR_CONDITIONAL) {
		sval_t sval;
		/* get_real_type is overly simplistic on conditional expressions,
		 * so we unpack this here
		 */
		if (implied_not_equal(expr->conditional, 0)) {
			/* cond_false is never taken */
			if (expr->cond_true == NULL)
				return 1;
			return is_safe_expr(expr->cond_true);
		}
		if (get_implied_value(expr->conditional, &sval) &&
		    sval.value == 0)
			return is_safe_expr(expr->cond_false);
		if (!is_safe_expr(expr->cond_false))
			return 0;
		if (!expr->cond_true)
			/* is expr->conditional is true, it will be used,
			 * and must be safe
			 */
			return 1;
		return is_safe_expr(expr->cond_true);
	}

	type = get_real_type(expr);
	if (type && is_ptr_type(type) && type->ctype.modifiers & MOD_SAFE)
		return 1;
	if (type && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type && type->type == SYM_ARRAY)
		return 1;

	expr = strip_expr(expr);
	if (expr->type == EXPR_BINOP &&
	    (expr->op == '+' || expr->op == '-' || expr->op == '&'))
		/* pointer arithmetic, assume 'left' is the pointer.
		 * It might have been cast into an int, so we cannot check
		 * the type */
		if (is_safe_expr(expr->left))
			return 1;
	if (expr->type == EXPR_ASSIGNMENT && expr->op == '=') {
		/* get_type returns type of LHS, I need attributes from
		 * RHS
		 */
		return is_safe_expr(expr->right);
	}

	if ((expr->type == EXPR_PREOP || expr->type == EXPR_POSTOP) &&
	    (expr->op == SPECIAL_INCREMENT || expr->op == SPECIAL_DECREMENT))
		return is_safe_expr(expr->unop);

	if (is_field_address(expr))
		/* The address of a member of a safe pointer is safe */
		return 1;

	return 0;
}

static int in_macro(struct expression *expr)
{
	char *name = get_macro_name(expr->pos);

	return name != NULL;
}

static void match_dereferences(struct expression *expr)
{
	char *name;

	if (expr->type != EXPR_PREOP)
		return;

	expr = expr->unop;

	if (is_safe_expr(expr))
		return;

	if (in_macro(expr))
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

static void match_condition(struct expression *expr)
{
	if (!is_pointer(expr))
		return;

	if (expr->type == EXPR_ASSIGNMENT) {
		match_condition(expr->right);
		match_condition(expr->left);
	}
	set_true_false_states_expr(my_id, expr, &safe, &undefined);
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
	add_hook(&match_condition, CONDITION_HOOK);
}
