
void free(void*);

void frob(void)
{
	char *a = malloc(10);

	free(a);
	/* a should not be dereferenced */
	a[3] += 1;
}

/*
 * check-name: check indexing of freed array.
 * check-command: smatch free_array.c
 *
 * check-output-start
free_array.c:10 frob() error: dereferencing freed memory 'a'
 * check-output-end
 */
