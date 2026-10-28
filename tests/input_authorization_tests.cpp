#include "triggerlab/InputAuthorization.hpp"
#include <iostream>
int main() {
    for (bool color : {false, true})
        for (bool verified : {false, true})
            for (bool dirty : {false, true})
                for (bool dry : {false, true}) {
                    triggerlab::InputAuthorization state{color, verified, dirty};
                    if (state.allows(dry) != (color && !dirty && (dry || verified)))
                        return 1;
                }
    std::cout << "PASS: all input authorization states\n";
}
