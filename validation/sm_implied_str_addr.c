#include "../check_debug.h"

void frob(void)
{
	char *foo = "Hello";
	char bar[5];
	char *baz = &bar;
	char *bat = bar;

	__smatch_implied_not_equal(foo, 0);
	__smatch_implied_not_equal(baz, 0);
	__smatch_implied_not_equal(bat, 0);
}
/*
 * check-name: Smatch implies string address
 * check-command: smatch sm_implied_str_addr.c
 *
 * check-output-start
sm_implied_str_addr.c:10 frob() implied not equal: foo != 0
sm_implied_str_addr.c:11 frob() implied not equal: baz != 0
sm_implied_str_addr.c:12 frob() implied not equal: bat != 0
 * check-output-end
 */
