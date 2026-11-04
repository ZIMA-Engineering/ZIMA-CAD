#include "sketch_engineering_profile_study.hpp"
#include "sketch_swivel_profile_study.hpp"
int main() {
    const int u=std::getenv("ZIMA_SWIVEL_ONLY")?0:zima::test::engineering::run();
    const int swivel=zima::test::engineering::run_swivel();return u||swivel?1:0;
}
