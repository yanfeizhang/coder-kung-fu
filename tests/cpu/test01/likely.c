# include <stdio.h>

#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

int main(int argc, char *argv[])
{
	int n;
	n = atoi (argv[1]);

	if (likely(n == 10)){
		n = n + 2;
	} else {
		n = n - 2;
	}
	printf("%d\n", n);
	return 0;
}
