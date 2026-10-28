#include "triggerlab/AtomicFile.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("triggerlab-writers-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path = root / "settings.json";
    std::atomic<int> failures{0};
    std::vector<std::thread> writers;
    for (int i = 0; i < 8; ++i)
        writers.emplace_back([&, i] {
            for (int n = 0; n < 30; ++n) {
                std::string error;
                if (!triggerlab::writeAtomicTextFile(path, std::string(65536, char('A' + i)), error))
                    ++failures;
            }
        });
    for (auto &writer : writers)
        writer.join();
    std::ifstream in(path, std::ios::binary);
    const std::string result((std::istreambuf_iterator<char>(in)), {});
    const bool valid = result.size() == 65536 && result.front() >= 'A' && result.front() <= 'H' &&
                       result.find_first_not_of(result.front()) == std::string::npos;
    in.close();
    int remaining = 0;
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (entry.path() != path)
            ++remaining;
    }
    std::filesystem::remove_all(root);
    if (failures || !valid || remaining)
        return 1;
    std::cout << "PASS: concurrent atomic writers, complete payload and clean temporary directories\n";
}
