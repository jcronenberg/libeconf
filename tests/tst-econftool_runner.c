#include <stdlib.h>

int main(void)
{
    int err;
    err = system("sh ../tests/tst-econftool1.sh");
    if (err)
        return 1;
    err = system("sh ../tests/tst-econftool_show1.sh");
    if (err)
        return 1;
}
