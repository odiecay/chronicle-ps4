#include "weapon_evolution.hpp"
#include <iostream>
#include <stdexcept>
void Check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main() {
 try {
    using evolution::Weapon;
    std::vector<Weapon> catalog{{100,0,2,1u<<3,1u<<1},{101,0,3,0,1u<<1},{102,0,21},{200,1,3}};
    auto tree=evolution::Tree(catalog,100);
    Check(tree.size()==4 && tree[1].id==101 && tree[2].id==102 && tree[2].depth==2 && tree[3].parent==100,
          "both mask words, branch depth, repeated descendants and owner filtering");
    catalog[2].low=1u<<2;
    tree=evolution::Tree(catalog,100);
    Check(tree.size()==6 && tree[3].cycle && tree[3].id==100, "cycles stop only their own branch");
    Check(evolution::Tree(catalog,100,2).size()==2, "malformed graph cannot exceed node limit");
    Check(evolution::Tree(catalog,999).empty(), "unknown weapons have no fabricated tree");
    Check(evolution::Meets(40,40) && !evolution::Meets(39,40), "positive thresholds are inclusive minimums");
    Check(evolution::Meets(10,-10) && !evolution::Meets(11,-10), "negative retail thresholds are inclusive maximums");
    evolution::Stats current{40,11}, required{40,-10};
    auto r=evolution::Requirements(current,required);
    Check(r.size()==2 && r[0].met && !r[1].met && r[1].maximum && r[1].limit==10, "deficit display follows retail comparison semantics");
    catalog[0].special_next=102;
    auto destinations=evolution::Destinations(catalog,100);
    Check(destinations.size()==1 && destinations[0]==102, "Mardan-style special links override ordinary masks");
    std::cout << "PASS: evolution graph, cycles, bounds, owner contracts and signed requirements\n"; return 0;
 } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
