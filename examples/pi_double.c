#include <inttypes.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor)) void preinit_locale()
{
	uselocale(LC_GLOBAL_LOCALE);
}

double find_pi(int prec)
{
	double sgn = (prec % 2) ? 1.0 : -1.0;
	double sum = 0;
	for (int i = prec; i > 0; --i) {
		sum += sgn / (2.0f * i - 1);
		sgn *= -1.0f;
	}
	return 4 * sum;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		printf("usage: <test> <int.prec>\n");
		return 1;
	}
	int prec = atoi(argv[1]);
	double res = find_pi(prec);
	uint64_t res_raw = *(uint64_t *)&res;
	printf("prec=%d, res=%.17g, raw=%" PRIx64 "\n", prec, res, res_raw);
	return 0;
}
