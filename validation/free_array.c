
#define INC(x)  ((*(x))+= 1)

void free(void*);

void frob(void)
{
	char *a = malloc(10);
	char *b = malloc(10);

	free(a);
	free(b);
	/* a and b should not be dereferenced */
	a[3] += 1;
	INC(&b[3]);
}

/*
 * check-name: check indexing of freed array.
 * check-command: smatch free_array.c
 *
 * check-output-start
free_array.c:14 frob() error: dereferencing freed memory 'a'
free_array.c:15 frob() error: dereferencing freed memory 'b'
 * check-output-end
 */
