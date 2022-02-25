#include "fasttrace.h"
#include <stdio.h>

VALUE mFasttrace;

void Init_fasttrace(void)
{
    mFasttrace = rb_define_module("Fasttrace");
    ft_init_trace();
}
