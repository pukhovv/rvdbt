#pragma once

#include <stdio.h>
#include <cstdlib>

int getnum()
{
	int res;
	scanf("%d", &res);
	return res;
}

void putnum(int x)
{
	printf("%d\n", x);
}

void doexit(int x)
{
	exit(x);
}
