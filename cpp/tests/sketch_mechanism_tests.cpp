#include "sketch_mechanism_study.hpp"
#include "sketch_tangent_slider_study.hpp"
int main() {
    try {const int mechanism=zima::test::mechanism::run();const int slider=zima::test::mechanism::run_tangent_slider();return mechanism||slider?1:0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
