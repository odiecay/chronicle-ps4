/* Port macros in C++, as the port compiles them when PORT is not defined. plain.cpp is the same code with the expansions written out. */
#include "kazarov/port.h"

struct Vec2 {
    int x;
    int y;
};

int scalar_init PORT_INIT(7);
Vec2 vec_init PORT_INIT({3, 4});

class Counter {
public:
    int next(int a)
    {
        int acc PORT_INIT(5);
        Vec2 local PORT_INIT({1, 2});
        if (a > 0) {
            return acc + a + local.x;
        }
        PORT_RETURN(acc);
    }
};

int check(int index)
{
    PORT_FIX(if (index >= 3) return 0;)
    return index + 1;
}

int use_all(int a)
{
    Counter counter;
    return check(a) + counter.next(a) + scalar_init + vec_init.y;
}
