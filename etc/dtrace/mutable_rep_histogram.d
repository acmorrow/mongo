#!/usr/bin/env dtrace -s

mutable*:::rep_create
/arg0 != 0/
{
    @distribution = quantize(arg0);
}

END
{
    printa(@distribution);
}
