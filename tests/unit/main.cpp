#include "framework.h"

int main() {
    int passed = 0, failed = 0;
    for (const auto& c : test::registry()) {
        try {
            c.fn();
            passed++;
        } catch (const test::Failure& f) {
            std::cout << "FAIL " << c.name << "\n    " << f.message << "\n";
            failed++;
        } catch (const std::exception& e) {
            std::cout << "FAIL " << c.name << "\n    exception: " << e.what() << "\n";
            failed++;
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
