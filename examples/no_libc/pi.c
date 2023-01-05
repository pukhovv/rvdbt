#include "uklibc.h"

float find_pi(int prec)
{
	float sgn = (prec % 2) ? 1.0 : -1.0;
	float sum = 0;
	for (int i = prec; i > 0; --i) {
		sum += sgn / (2.0f * i - 1);
		sgn = -sgn;
	}
	return 4 * sum;
}

int main(int argc, char **argv)
{
	int prec = getnum();
	int res = 100000000L * find_pi(prec);
	putnum(res);
	return 0;
}
