#ifndef UTILS
#define UTILS

void string_copy(char *d,const char *s)
{
    while ((*d++=*s++));
}

#endif
