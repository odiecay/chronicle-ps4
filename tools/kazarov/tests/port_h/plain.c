/* The PS2 expansion of uses.c, by hand: PORT_INIT and PORT_FIX expand to nothing, and PORT_RETURN(...); leaves its ; behind. */
struct Vec2 {
    int x;
    int y;
};

int scalar_init ;
struct Vec2 vec_init ;

int add_scalar(int a)
{
    return a + scalar_init;
}

static int conditional(int index)
{
    return index + 1;
}

static int tail(int a)
{
    int acc ;
    struct Vec2 local ;
    if (a > 0) {
        return acc + a + local.x;
    }
    ;
}

int use_all(int a)
{
    return conditional(a) + tail(a) + add_scalar(a) + vec_init.y;
}
