/* Built with -DPORT: PORT_INIT and PORT_RETURN must produce the values the port expects. */
#include <cstdio>
#include <kazarov/port.h>

struct Vec2 {
    int x;
    int y;
};

int scalar PORT_INIT(42);
Vec2 vec PORT_INIT({3, 4});

static int returns(int a)
{
    PORT_RETURN(a * 2);
}

static int fixes(int index)
{
    PORT_FIX(if (index >= 3) return 0;)
    return index + 1;
}

static int local_init()
{
    int acc PORT_INIT(9);
    Vec2 local PORT_INIT({5, 6});
    return acc + local.y;
}

static int failures = 0;

#define EXPECT(cond)                                  \
    do {                                              \
        if (!(cond)) {                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                               \
        }                                             \
    } while (0)

int main()
{
    EXPECT(scalar == 42);
    EXPECT(vec.x == 3 && vec.y == 4);
    EXPECT(returns(21) == 42);
    EXPECT(fixes(2) == 3);
    EXPECT(fixes(3) == 0);
    EXPECT(local_init() == 15);
    if (failures == 0) {
        std::printf("port_run: all checks passed\n");
    }
    return failures ? 1 : 0;
}
