#include "check_debug.h"
int some_func();
int a, b, c, d;
int frob(void) {
	if (a)
		__smatch_print_value("a");
	else
		__smatch_print_value("a");
	__smatch_print_value("a");
	if (a) {
		b = 0;
		__smatch_print_value("b");
	}
	__smatch_print_value("b");
	c = 0;
	c = some_func();
	__smatch_print_value("c");
	if (d < -3 || d > 99)
		return;
	__smatch_print_value("d");
}
/*
 * check-name: Smatch range test #2
 * check-command: smatch -I.. sm_range2.c
 *
 * check-output-start
sm_range2.c +6 frob(2) a = min-(-1),1-max
sm_range2.c +8 frob(4) a = 0
sm_range2.c +9 frob(5) a = min-max
sm_range2.c +12 frob(8) b = 0
sm_range2.c +14 frob(10) b = min-max
sm_range2.c +17 frob(13) c = unknown
sm_range2.c +20 frob(16) d = (-3)-99
 * check-output-end
 */