#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "test_util.h"
#include "tos/base/dynamic_library.h"

namespace {
bool Observe(tos::DynamicLibrary& library, std::atomic<int>& counter) {
    auto observe = library.GetSymbol<void (*)(std::atomic<int>*)>("TosObserveUnload");
    if (!observe) return false;
    observe.value()(&counter);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    auto path = tos::Path::Parse(argv[2]);
    if (!path) return 3;
    std::atomic<int> unloaded{0}, second_unloaded{0};
    const std::string scenario = argv[1];
    if (scenario == "scope") {
        {
            auto library = tos::DynamicLibrary::Load(path.value());
            if (!library || !Observe(library.value(), unloaded)) return 4;
        }
        return unloaded == 1 ? 0 : 5;
    }
    if (scenario == "move") {
        // A distinct image makes release of the old move-assignment target observable.
        tos::test::TemporaryDirectory directory("tos-lifetime-helper-");
        const auto copy = directory.path() / std::filesystem::u8path(path->utf8()).filename();
        std::filesystem::copy_file(std::filesystem::u8path(path->utf8()), copy);
        auto copied_path = tos::Path::Parse(copy.u8string());
        if (!copied_path) return 6;
        auto first = tos::DynamicLibrary::Load(path.value());
        auto second = tos::DynamicLibrary::Load(copied_path.value());
        if (!first || !second || !Observe(first.value(), unloaded) ||
            !Observe(second.value(), second_unloaded))
            return 7;
        tos::DynamicLibrary moved = std::move(first).value();
        if (first->loaded() || unloaded != 0) return 8;
        second.value() = std::move(moved);
        if (second_unloaded != 1 || unloaded != 0 || moved.loaded()) return 9;
        auto function = second->GetSymbol<int (*)(int)>("TosLifetimeIncrement");
        if (!function || function.value()(41) != 42 || !second->Unload()) return 10;
        return unloaded == 1 && second->Unload() ? 0 : 11;
    }
    auto keeper = tos::DynamicLibrary::Load(path.value());
    if (!keeper || !Observe(keeper.value(), unloaded)) return 12;
    if (scenario == "owners") {
        auto second = tos::DynamicLibrary::Load(path.value());
        if (!second || !keeper->Unload() || unloaded != 0) return 13;
        auto function = second->GetSymbol<int (*)(int)>("TosLifetimeIncrement");
        if (!function || function.value()(7) != 8 || !second->Unload()) return 14;
        return unloaded == 1 ? 0 : 15;
    }
    if (scenario != "concurrent") return 16;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    tos::test::ScopeExit cleanup([&] {
        for (auto& thread : threads)
            if (thread.joinable()) thread.join();
    });
    for (int index = 0; index < 4; ++index)
        threads.emplace_back([&] {
            for (int cycle = 0; cycle < 20; ++cycle) {
                auto library = tos::DynamicLibrary::Load(path.value());
                if (!library) {
                    ++failures;
                    continue;
                }
                auto function = library->GetSymbol<int (*)(int)>("TosLifetimeIncrement");
                if (!function || function.value()(cycle) != cycle + 1) ++failures;
            }
        });
    for (auto& thread : threads) thread.join();
    if (failures != 0 || unloaded != 0 || !keeper->Unload()) return 17;
    return unloaded == 1 ? 0 : 18;
}
