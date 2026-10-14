/* Port macros in C, as the port compiles them when PORT is not defined. plain.c is the same code with the expansions written out. */
#include "kazarov/port.h"

struct Vec2 {
    int x;
    int y;
};

int scalar_init PORT_INIT(7);
struct Vec2 vec_init PORT_INIT({3, 4});

int add_scalar(int a)
{
    return a + scalar_init;
}

static int conditional(int index)
{
    PORT_FIX(if (index >= 3) return 0;)
    return index + 1;
}

static int tail(int a)
{
    int acc PORT_INIT(5);
    struct Vec2 local PORT_INIT({1, 2});
    if (a > 0) {
        return acc + a + local.x;
    }
    PORT_RETURN(acc);
}

int use_all(int a)
{
    return conditional(a) + tail(a) + add_scalar(a) + vec_init.y;
}
