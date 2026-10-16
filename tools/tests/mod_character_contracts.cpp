#include "mod_character.hpp"
#include <iostream>
#include <stdexcept>
void Check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main() {
 try {
    const ModName toan{181,202,188,201}, tora{181,202,205,188}, custom{164,202,191,192,212};
    ModCharacterIdentity identity;
    Check(identity.Configure("Tora"), "Latin name is representable in retail name-entry encoding");
    auto name = toan;
    identity.Loaded(name);
    Check(name==tora && identity.ForSave(name)==toan, "default Toan becomes Tora for display while saving original default");
    name=custom; identity.Loaded(name);
    Check(name==custom && identity.ForSave(name)==custom, "custom names survive default pack policy");
    identity.Configure("Tora",true); identity.Loaded(name);
    Check(name==tora && identity.ForSave(name)==custom, "explicit pack-name policy preserves custom name for disabling");
    name=custom;
    Check(identity.ForSave(name)==custom, "a name edited after applying pack is saved as the new custom name");
    identity.Configure("Tora"); name=toan; identity.Defaulted(name);
    Check(name==tora && identity.ForSave(name)==toan, "new-game default is reversible");
    name=tora; identity.Loaded(name);
    Check(identity.ForSave(name)==tora, "a pre-existing custom name literally Tora must remain Tora");
    identity.Configure(""); name=toan; identity.Loaded(name);
    Check(name==toan, "disabled pack restores vanilla default from unchanged save");
    Check(!identity.Configure("TooLongForName") && !identity.Configure("bad{name}"), "invalid name encoding rejected");
    std::cout << "PASS: default/custom names, new games, reversible serialization and explicit override\n"; return 0;
 } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
