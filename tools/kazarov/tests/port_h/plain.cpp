/* The PS2 expansion of uses.cpp, by hand: PORT_INIT and PORT_FIX expand to nothing, and PORT_RETURN(...); leaves its ; behind. */
struct Vec2 {
    int x;
    int y;
};

int scalar_init ;
Vec2 vec_init ;

class Counter {
public:
    int next(int a)
    {
        int acc ;
        Vec2 local ;
        if (a > 0) {
            return acc + a + local.x;
        }
        ;
    }
};

int check(int index)
{
    return index + 1;
}

int use_all(int a)
{
    Counter counter;
    return check(a) + counter.next(a) + scalar_init + vec_init.y;
}
