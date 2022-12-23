#include "exposure.h"
#include "trace.h"
#include <stdio.h>

VALUE mExposure;

void Init_exposure(void)
{
    mExposure = rb_define_module("Exposure");
    ft_init_trace();
}
