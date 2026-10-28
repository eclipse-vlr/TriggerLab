#include "persistence/NumericObject.hpp"
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
} // namespace
int main() {
    using triggerlab::persistence::parseNumericObject;
    const auto real = parseNumericObject<double>(R"({"alpha": -1.25e+2, "beta": 0.5})");
    check(real && real->at("alpha") == -125 && real->at("beta") == 0.5, "real JSON number syntax");
    const auto integer = parseNumericObject<int>(R"({"offset_x": -2000, "side": 31})");
    check(integer && integer->at("offset_x") == -2000, "signed integers");
    for (const auto bad :
         {"", "{}", "[]", R"({"a":1,"a":2})", R"({"a":01})", R"({"a":1,})", R"({"a":1} true)", R"({"a":+1})",
          R"({"a":.5})", R"({"a":1.})", R"({"a":1e})", R"({"a":1e+})", R"({"a":1e999})", R"({"a":NaN})",
          R"({"a":null})", R"({"a":"1"})", R"({"a":{"b":1}})", R"({"":1})", R"({"a-b":1})"}) {
        check(!parseNumericObject<double>(bad), "malformed numeric object rejected");
    }
    for (const auto bad : {R"({"a":1.0})", R"({"a":1e1})", R"({"a":2147483648})", R"({"a":-2147483649})"}) {
        check(!parseNumericObject<int>(bad), "integer schema rejects fractions, exponents and overflow");
    }
    check(!parseNumericObject<double>(std::string(65537, ' ')), "bounded input");
    check(!parseNumericObject<int>("{\"" + std::string(65, 'a') + "\":1}"), "bounded keys");
    std::string many = "{";
    for (int i = 0; i < 33; ++i) {
        if (i)
            many += ',';
        many +=
            "\"" + std::string(1, static_cast<char>('a' + i % 26)) + std::string(i / 26 + 1, '_') + "\":1";
    }
    many += '}';
    check(!parseNumericObject<int>(many), "bounded fields");
    std::cout << "PASS: strict shared numeric parser\n";
}
