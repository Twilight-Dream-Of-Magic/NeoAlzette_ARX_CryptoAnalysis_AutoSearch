#include <iostream>
#include "pddt.hpp"

using namespace neoalz;

int main(int argc, char** argv){
    int n = (argc>1)? std::atoi(argv[1]) : 8;
    int wth = (argc>2)? std::atoi(argv[2]) : 4;
    PDDTAdder gen({n,wth});
    auto tbl = gen.compute();
    std::cout << "pDDT(ADD) n="<<n<<", w<= "<<wth<<" -> triples = "<< tbl.size() << "\n";
    // print a few
    for(size_t i=0;i<std::min<size_t>(10, tbl.size()); ++i){
        auto &t = tbl[i];
        std::cout << std::hex
                  << "a=" << t.alpha << " b=" << t.beta << " g=" << t.gamma
                  << std::dec << " w=" << t.weight << "\n";
    }
    return 0;
}
