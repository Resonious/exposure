#include "tracyrb.h"
#include "trace.h"
#include <stdio.h>

VALUE mTracyrb;

void Init_tracyrb(void)
{
    mTracyrb = rb_define_module("Tracyrb");
    ft_init_trace();
}
