/*
 * Copyright (C) 2006 Dan Carpenter.
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

/*
 * Miscellaneous helper functions.
 */

#include <stdlib.h>
#include <stdio.h>
#include "allocate.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

#define VAR_LEN 512

char *alloc_string(const char *str)
{
	char *tmp;

	if (!str)
		return NULL;
	tmp = malloc(strlen(str) + 1);
	strcpy(tmp, str);
	return tmp;
}

void free_string(char *str)
{
	free(str);
}

void remove_parens(char *str)
{
	char *src, *dst;

	dst = src = str;
	while (*src != '\0') {
		if (*src == '(' || *src == ')') {
			src++;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = *src;
}

struct smatch_state *alloc_state_num(int num)
{
	struct smatch_state *state;
	static char buff[256];

	state = __alloc_smatch_state(0);
	snprintf(buff, 255, "%d", num);
	buff[255] = '\0';
	state->name = alloc_string(buff);
	state->data = INT_PTR(num);
	return state;
}

struct smatch_state *alloc_state_str(const char *name)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->name = alloc_string(name);
	return state;
}

struct smatch_state *alloc_state_expr(struct expression *expr)
{
	struct smatch_state *state;
	char *name;

	state = __alloc_smatch_state(0);
	expr = strip_expr(expr);
	name = expr_to_str(expr);
	state->name = alloc_sname(name);
	free_string(name);
	state->data = expr;
	return state;
}

void append(char *dest, const char *data, int buff_len)
{
	strncat(dest, data, buff_len - strlen(dest) - 1);
}

/*
 * If you have "foo(a, b, 1);" then use
 * get_argument_from_call_expr(expr, 0) to return the expression for
 * a.  Yes, it does start counting from 0.
 */
struct expression *get_argument_from_call_expr(struct expression_list *args,
					       int num)
{
	struct expression *expr;
	int i = 0;

	if (!args)
		return NULL;

	FOR_EACH_PTR(args, expr) {
		if (i == num)
			return expr;
		i++;
	} END_FOR_EACH_PTR(expr);
	return NULL;
}

static struct expression *get_array_expr(struct expression *expr)
{
	struct symbol *type;

	if (expr->type != EXPR_BINOP || expr->op != '+')
		return NULL;

	type = get_type(expr->left);
	if (!type || type->type != SYM_ARRAY)
		return NULL;
	return expr->left;
}

static void __get_variable_from_expr(struct symbol **sym_ptr, char *buf,
				     struct expression *expr, int len,
				     int *complicated, int no_parens)
{
	switch (expr->type) {
	case EXPR_DEREF: {
		struct expression *deref;
		int op;

		deref = expr->deref;
		op = deref->op;
		if (op == '*') {
			struct expression *unop = strip_expr(deref->unop);

			if (unop->type == EXPR_PREOP && unop->op == '&') {
				deref = unop->unop;
				op = '.';
			} else {
				deref = deref->unop;
				if (!is_pointer(deref))
					op = '.';
			}
		}

		__get_variable_from_expr(sym_ptr, buf, deref, len, complicated, no_parens);

		if (op == '*')
			append(buf, "->", len);
		else
			append(buf, ".", len);

		if (expr->member)
			append(buf, expr->member->name, len);
		else
			append(buf, "unknown_member", len);

		return;
	}
	case EXPR_SYMBOL:
		if (expr->symbol_name)
			append(buf, expr->symbol_name->name, len);
		if (sym_ptr) {
			if (*sym_ptr)
				*complicated = 1;
			*sym_ptr = expr->symbol;
		}
		return;
	case EXPR_PREOP: {
		const char *tmp;

		if (get_expression_statement(expr)) {
			*complicated = 2;
			return;
		}

		if (expr->op == '(') {
			if (!no_parens)
				append(buf, "(", len);
		} else if (expr->op != '*' || !get_array_expr(expr->unop)) {
			tmp = show_special(expr->op);
			append(buf, tmp, len);
		}
		__get_variable_from_expr(sym_ptr, buf, expr->unop,
						 len, complicated, no_parens);

		if (expr->op == '(' && !no_parens)
			append(buf, ")", len);

		if (expr->op == SPECIAL_DECREMENT ||
				expr->op == SPECIAL_INCREMENT)
			*complicated = 1;

		return;
	}
	case EXPR_POSTOP: {
		const char *tmp;

		__get_variable_from_expr(sym_ptr, buf, expr->unop,
						 len, complicated, no_parens);
		tmp = show_special(expr->op);
		append(buf, tmp, len);

		if (expr->op == SPECIAL_DECREMENT || expr->op == SPECIAL_INCREMENT)
			*complicated = 1;
		return;
	}
	case EXPR_ASSIGNMENT:
	case EXPR_COMPARE:
	case EXPR_LOGICAL:
	case EXPR_BINOP: {
		char tmp[10];
		struct expression *array_expr;

		*complicated = 1;
		array_expr = get_array_expr(expr);
		if (array_expr) {
			__get_variable_from_expr(sym_ptr, buf, array_expr, len, complicated, no_parens);
			append(buf, "[", len);
		} else {
			__get_variable_from_expr(sym_ptr, buf, expr->left, len, complicated, no_parens);
			snprintf(tmp, sizeof(tmp), " %s ", show_special(expr->op));
			append(buf, tmp, len);
		}
		__get_variable_from_expr(NULL, buf, expr->right, len, complicated, no_parens);
		if (array_expr)
			append(buf, "]", len);
		return;
	}
	case EXPR_VALUE: {
		char tmp[25];

		*complicated = 1;
		snprintf(tmp, 25, "%lld", expr->value);
		append(buf, tmp, len);
		return;
	}
	case EXPR_STRING:
		append(buf, "\"", len);
		if (expr->string)
			append(buf, expr->string->data, len);
		append(buf, "\"", len);
		return;
	case EXPR_CALL: {
		struct expression *tmp;
		int i;

		*complicated = 1;
		__get_variable_from_expr(NULL, buf, expr->fn, len, complicated, no_parens);
		append(buf, "(", len);
		i = 0;
		FOR_EACH_PTR(expr->args, tmp) {
			if (i++)
				append(buf, ", ", len);
			__get_variable_from_expr(NULL, buf, tmp, len, complicated, no_parens);
		} END_FOR_EACH_PTR(tmp);
		append(buf, ")", len);
		return;
	}
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
		__get_variable_from_expr(sym_ptr, buf,
					 expr->cast_expression, len,
					 complicated, no_parens);
		return;
	case EXPR_SIZEOF: {
		int size;
		char tmp[25];

		if (expr->cast_type && get_base_type(expr->cast_type)) {
			size = type_bytes(get_base_type(expr->cast_type));
			snprintf(tmp, 25, "%d", size);
			append(buf, tmp, len);
		}
		return;
	}
	case EXPR_IDENTIFIER:
		*complicated = 1;
		if (expr->expr_ident)
			append(buf, expr->expr_ident->name, len);
		return;
	default:
		*complicated = 1;
		//printf("unknown type = %d\n", expr->type);
		return;
	}
}

/*
 * This is returns a stylized "c looking" representation of the
 * variable name.
 *
 * It uses the same buffer every time so you have to save the result
 * yourself if you want to keep it.
 *
 */

char *expr_to_str_sym(struct expression *expr, struct symbol **sym_ptr)
{
	static char var_name[VAR_LEN];
	int complicated = 0;

	if (sym_ptr)
		*sym_ptr = NULL;
	var_name[0] = '\0';

	if (!expr)
		return NULL;
	__get_variable_from_expr(sym_ptr, var_name, expr, sizeof(var_name),
				 &complicated, 0);
	if (complicated < 2)
		return alloc_string(var_name);
	else
		return NULL;
}

char *expr_to_str(struct expression *expr)
{
	return expr_to_str_sym(expr, NULL);
}

/*
 * get_variable_from_expr_simple() only returns simple variables.
 * If it's a complicated variable like a->foo[x] instead of just 'a->foo'
 * then it returns NULL.
 */
char *expr_to_var_sym(struct expression *expr,
				    struct symbol **sym_ptr)
{
	static char var_name[VAR_LEN];
	int complicated = 0;

	if (sym_ptr)
		*sym_ptr = NULL;
	var_name[0] = '\0';

	if (!expr)
		return NULL;
	expr = strip_expr(expr);
	__get_variable_from_expr(sym_ptr, var_name, expr, sizeof(var_name),
				 &complicated, 1);

	if (complicated) {
		if (sym_ptr)
			*sym_ptr = NULL;
		return NULL;
	}
	return alloc_string(var_name);
}

char *expr_to_var(struct expression *expr)
{
	return expr_to_var_sym(expr, NULL);
}

struct symbol *expr_to_sym(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	return sym;
}

int get_complication_score(struct expression *expr)
{
	int score = 0;

	expr = strip_expr(expr);

	/*
	 * Don't forget to keep get_complication_score() and store_all_links()
	 * in sync.
	 *
	 */

	switch (expr->type) {
	case EXPR_CALL:
		return 999;
	case EXPR_COMPARE:
	case EXPR_BINOP:
		score += get_complication_score(expr->left);
		score += get_complication_score(expr->right);
		return score;
	case EXPR_SYMBOL:
		if (is_local_variable(expr))
			return 1;
		return 999;
	case EXPR_PREOP:
		if (expr->op == '*')
			return score + get_complication_score(expr->unop);
		return 999;
	case EXPR_DEREF:
		return score + get_complication_score(expr->deref);
	case EXPR_VALUE:
		return 0;
	default:
		return 999;
	}
}

char *expr_to_chunk_helper(struct expression *expr, struct symbol **sym, struct var_sym_list **vsl)
{
	char *name;
	struct symbol *tmp;
	int score;

	if (vsl)
		*vsl = NULL;
	if (sym)
		*sym = NULL;

	expr = strip_parens(expr);
	if (!expr)
		return NULL;

	name = expr_to_var_sym(expr, &tmp);
	if (name && tmp) {
		if (sym)
			*sym = tmp;
		if (vsl)
			*vsl = expr_to_vsl(expr);
		return name;
	}
	free_string(name);

	score = get_complication_score(expr);
	if (score <= 0 || score > 2)
		return NULL;

	if (vsl) {
		*vsl = expr_to_vsl(expr);
		if (!*vsl)
			return NULL;
	}

	return expr_to_str(expr);
}

char *expr_to_known_chunk_sym(struct expression *expr, struct symbol **sym)
{
	return expr_to_chunk_helper(expr, sym, NULL);
}

char *expr_to_chunk_sym_vsl(struct expression *expr, struct symbol **sym, struct var_sym_list **vsl)
{
	return expr_to_chunk_helper(expr, sym, vsl);
}

int sym_name_is(const char *name, struct expression *expr)
{
	if (!expr)
		return 0;
	if (expr->type != EXPR_SYMBOL)
		return 0;
	if (!strcmp(expr->symbol_name->name, name))
		return 1;
	return 0;
}

int is_zero(struct expression *expr)
{
	sval_t sval;

	if (get_value(expr, &sval) && sval.value == 0)
		return 1;
	return 0;
}

int is_array(struct expression *expr)
{
	struct symbol *type;

	expr = strip_expr(expr);
	if (!expr)
		return 0;

	if (expr->type == EXPR_PREOP && expr->op == '*') {
		expr = strip_expr(expr->unop);
		if (expr->type == EXPR_BINOP && expr->op == '+')
			return 1;
	}

	if (expr->type != EXPR_BINOP || expr->op != '+')
		return 0;

	type = get_type(expr->left);
	if (!type || type->type != SYM_ARRAY)
		return 0;

	return 1;
}

struct expression *get_array_base(struct expression *expr)
{
	if (!is_array(expr))
		return NULL;
	expr = strip_expr(expr);
	if (expr->type == EXPR_PREOP && expr->op == '*')
		expr = strip_expr(expr->unop);
	if (expr->type != EXPR_BINOP || expr->op != '+')
		return NULL;
	return strip_parens(expr->left);
}

struct expression *get_array_offset(struct expression *expr)
{
	if (!is_array(expr))
		return NULL;
	expr = strip_expr(expr);
	if (expr->type == EXPR_PREOP && expr->op == '*')
		expr = strip_expr(expr->unop);
	if (expr->type != EXPR_BINOP || expr->op != '+')
		return NULL;
	return strip_parens(expr->right);
}

const char *show_state(struct smatch_state *state)
{
	if (!state)
		return NULL;
	return state->name;
}

struct statement *get_expression_statement(struct expression *expr)
{
	/* What are those things called? if (({....; ret;})) { ...*/

	if (expr->type != EXPR_PREOP)
		return NULL;
	if (expr->op != '(')
		return NULL;
	if (expr->unop->type != EXPR_STATEMENT)
		return NULL;
	if (expr->unop->statement->type != STMT_COMPOUND)
		return NULL;
	return expr->unop->statement;
}

struct expression *strip_parens(struct expression *expr)
{
	if (!expr)
		return NULL;

	if (expr->type == EXPR_PREOP) {
		if (expr->op == '(' && expr->unop->type == EXPR_STATEMENT &&
			expr->unop->statement->type == STMT_COMPOUND)
			return expr;
		if (expr->op == '(')
			return strip_parens(expr->unop);
	}
	return expr;
}

struct expression *strip_expr(struct expression *expr)
{
	if (!expr)
		return NULL;

	switch (expr->type) {
	case EXPR_FORCE_CAST:
	case EXPR_CAST:
		return strip_expr(expr->cast_expression);
	case EXPR_PREOP: {
		struct expression *unop;

		if (expr->op == '(' && expr->unop->type == EXPR_STATEMENT &&
			expr->unop->statement->type == STMT_COMPOUND)
			return expr;

		unop = strip_expr(expr->unop);

		if (expr->op == '*' &&
		    unop->type == EXPR_PREOP && unop->op == '&') {
			struct symbol *type = get_type(unop->unop);

			if (type && type->type == SYM_ARRAY)
				return expr;
			return strip_expr(unop->unop);
		}

		if (expr->op == '(')
			return unop;

		return expr;
	}
	case EXPR_CONDITIONAL:
		if (known_condition_true(expr->conditional)) {
			if (expr->cond_true)
				return strip_expr(expr->cond_true);
			return strip_expr(expr->conditional);
		}
		if (known_condition_false(expr->conditional))
			return strip_expr(expr->cond_false);
		return expr;
	case EXPR_CALL:
		if (sym_name_is("__builtin_expect", expr->fn)) {
			expr = get_argument_from_call_expr(expr->args, 0);
			return strip_expr(expr);
		}
		return expr;
	}
	return expr;
}

static void delete_state_tracker(struct tracker *t)
{
	delete_state(t->owner, t->name, t->sym);
	__free_tracker(t);
}

void scoped_state(int my_id, const char *name, struct symbol *sym)
{
	struct tracker *t;

	t = alloc_tracker(my_id, name, sym);
	add_scope_hook((scope_hook *)&delete_state_tracker, t);
}

int is_error_return(struct expression *expr)
{
	struct symbol *cur_func = cur_func_sym;
	sval_t sval;

	if (!expr)
		return 0;
	if (cur_func->type != SYM_NODE)
		return 0;
	cur_func = get_base_type(cur_func);
	if (cur_func->type != SYM_FN)
		return 0;
	cur_func = get_base_type(cur_func);
	if (cur_func == &void_ctype)
		return 0;
	if (!get_implied_value(expr, &sval))
		return 0;
	if (sval.value < 0)
		return 1;
	if (cur_func->type == SYM_PTR && sval.value == 0)
		return 1;
	return 0;
}

int getting_address(void)
{
	struct expression *tmp;
	int i = 0;
	int dot_ops = 0;

	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (!i++)
			continue;
		if (tmp->type == EXPR_PREOP && tmp->op == '(')
			continue;
		if (tmp->op == '.' && !dot_ops++)
			continue;
		if (tmp->op == '&')
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

char *get_member_name(struct expression *expr)
{
	char buf[256];
	struct symbol *sym;

	expr = strip_expr(expr);
	if (expr->type != EXPR_DEREF)
		return NULL;
	if (!expr->member)
		return NULL;

	sym = get_type(expr->deref);
	if (!sym)
		return NULL;
	if (sym->type == SYM_UNION) {
		sym = expr_to_sym(expr->deref);
		sym = get_real_base_type(sym);
		if (sym && sym->type == SYM_PTR)
			sym = get_real_base_type(sym);
		if (!sym || !sym->ident) {
			snprintf(buf, sizeof(buf), "(union hack)->%s", expr->member->name);
			return alloc_string(buf);
		}
	}
	if (!sym->ident)
		return NULL;
	snprintf(buf, sizeof(buf), "(struct %s)->%s", sym->ident->name, expr->member->name);
	return alloc_string(buf);
}

int cmp_pos(struct position pos1, struct position pos2)
{
	/* the stream position is ... */
	if (pos1.stream > pos2.stream)
		return -1;
	if (pos1.stream < pos2.stream)
		return 1;

	if (pos1.line < pos2.line)
		return -1;
	if (pos1.line > pos2.line)
		return 1;

	if (pos1.pos < pos2.pos)
		return -1;
	if (pos1.pos > pos2.pos)
		return 1;

	return 0;
}

int positions_eq(struct position pos1, struct position pos2)
{
	if (pos1.line != pos2.line)
		return 0;
	if (pos1.pos != pos2.pos)
		return 0;
	if (pos1.stream != pos2.stream)
		return 0;
	return 1;
}

struct statement *get_current_statement(void)
{
	struct statement *prev, *tmp;

	prev = last_ptr_list((struct ptr_list *)big_statement_stack);

	if (!prev || !get_macro_name(prev->pos))
		return prev;

	FOR_EACH_PTR_REVERSE(big_statement_stack, tmp) {
		if (positions_eq(tmp->pos, prev->pos))
			continue;
		if (prev->pos.line > tmp->pos.line)
			return prev;
		return tmp;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return prev;
}

struct statement *get_prev_statement(void)
{
	struct statement *tmp;
	int i;

	i = 0;
	FOR_EACH_PTR_REVERSE(big_statement_stack, tmp) {
		if (i++ == 1)
			return tmp;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return NULL;
}

int get_param_num_from_sym(struct symbol *sym)
{
	struct symbol *tmp;
	int i;

	if (!cur_func_sym)
		return -1;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		if (tmp == sym)
			return i;
		i++;
	} END_FOR_EACH_PTR(tmp);
	return -1;
}

int get_param_num(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	if (!cur_func_sym)
		return -1;
	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	if (!sym)
		return -1;
	return get_param_num_from_sym(sym);
}

int ms_since(struct timeval *start)
{
	struct timeval end;
	double diff;

	gettimeofday(&end, NULL);
	diff = (end.tv_sec - start->tv_sec) * 1000.0;
	diff += (end.tv_usec - start->tv_usec) / 1000.0;
	return (int)diff;
}

int parent_is_gone_var_sym(const char *name, struct symbol *sym)
{
	if (!name || !sym)
		return 0;

	if (parent_is_null_var_sym(name, sym) ||
	    parent_is_free_var_sym(name, sym))
		return 1;
	return 0;
}

int parent_is_gone(struct expression *expr)
{
	struct symbol *sym;
	char *var;
	int ret = 0;

	expr = strip_expr(expr);
	var = expr_to_var_sym(expr, &sym);
	if (!var || !sym)
		goto free;
	ret = parent_is_gone_var_sym(var, sym);
free:
	free_string(var);
	return ret;
}

int invert_op(int op)
{
	switch (op) {
	case '*':
		return '/';
	case '/':
		return '*';
	case '+':
		return '-';
	case '-':
		return '+';
	case SPECIAL_LEFTSHIFT:
		return SPECIAL_RIGHTSHIFT;
	case SPECIAL_RIGHTSHIFT:
		return SPECIAL_LEFTSHIFT;
	}
	return 0;
}

int expr_equiv(struct expression *one, struct expression *two)
{
	struct symbol *one_sym, *two_sym;
	char *one_name = NULL;
	char *two_name = NULL;
	int ret = 0;

	if (!one || !two)
		return 0;
	if (one->type != two->type)
		return 0;

	one_name = expr_to_str_sym(one, &one_sym);
	if (!one_name || !one_sym)
		goto free;
	two_name = expr_to_str_sym(two, &two_sym);
	if (!two_name || !two_sym)
		goto free;
	if (one_sym != two_sym)
		goto free;
	if (strcmp(one_name, two_name) == 0)
		ret = 1;
free:
	free_string(one_name);
	free_string(two_name);
	return ret;
}

void push_int(struct int_stack **stack, int num)
{
	int *munged;

	/*
	 * Just put the int on directly instead of a pointer to the int.
	 * Shift it to the left because Sparse uses the last two bits.
	 * This is sort of a dirty hack, yes.
	 */

	munged = INT_PTR(num << 2);

	add_ptr_list(stack, munged);
}

int pop_int(struct int_stack **stack)
{
	int *num;

	num = last_ptr_list((struct ptr_list *)*stack);
	delete_ptr_list_last((struct ptr_list **)stack);

	return PTR_INT(num) >> 2;
}
