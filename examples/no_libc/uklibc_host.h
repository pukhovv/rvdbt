#pragma once

#include <stdio.h>

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
