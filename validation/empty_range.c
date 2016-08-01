#include "check_debug.h"

#define NULL ((void*)0)
void frob(void)
{
	char *a;
	int i;
	a = NULL;
	for (i = 0; i < 10; i++) {
		if (a)
			__smatch_implied_not_equal(a, NULL);
		a = "Message";
	}
}

/*
 * check-name: check empty range does not imply zero.
 * check-command: smatch -I.. empty_range.c
 *
 * check-output-start
empty_range.c:11 frob() implied not equal: a != 0
 * check-output-end
 */
