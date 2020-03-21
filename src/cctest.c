#include<stdio.h>

void main(void)
{
    int x = 1;
    char *p = (char *)&x;

    printf("sizeof(char)=%d\n", sizeof(char));
    printf("sizeof(int)=%d\n", sizeof(int));
    printf("sizeof(short)=%d\n", sizeof(short));
    printf("sizeof(long)=%d\n", sizeof(long));

    if (*p == 0) { printf("big endian\n"); }
    else { printf("little endian\n"); }
}