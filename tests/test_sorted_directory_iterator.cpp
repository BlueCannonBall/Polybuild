// Compile with C++17 and run with a temporary directory path as the sole argument.
#include "../util.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <new>

static bool fail_allocation = false;

void* operator new(std::size_t size) {
    if (fail_allocation) {
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size ? size : 1)) {
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path root(argv[1]);
    const auto directory = root / "entries";
    const auto empty = root / "empty";
    const auto missing = root / "missing";
    std::filesystem::create_directories(directory);
    std::filesystem::create_directories(empty);
    std::ofstream(directory / "b.cpp").put('\n');
    std::ofstream(directory / "a.cpp").put('\n');
    const auto file = directory / "a.cpp";
    const SortedDirectoryIterator end;

    for (auto options : {std::filesystem::directory_options::none,
             std::filesystem::directory_options::skip_permission_denied}) {
        std::error_code ec = std::make_error_code(std::errc::io_error);
        SortedDirectoryIterator single(file, options, ec);
        assert(!ec);
        assert(single != end);
        assert(single->path() == file);
        assert(single->path() == SortedDirectoryIterator(file, options)->path());
        assert(++single == end);

        ec = std::make_error_code(std::errc::io_error);
        SortedDirectoryIterator it(directory, options, ec);
        assert(!ec);
        assert(it->path().filename() == "a.cpp");
        auto copy = it++;
        assert(copy->path().filename() == "a.cpp");
        assert(it->path().filename() == "b.cpp");
        assert(it.increment(ec) == end);
        assert(!ec);
        assert(copy->path().filename() == "a.cpp");

        ec = std::make_error_code(std::errc::io_error);
        assert(SortedDirectoryIterator(empty, options, ec) == end);
        assert(!ec);
        assert(SortedDirectoryIterator(missing, options, ec) == end);
        assert(ec);
    }

    std::error_code ec;
    assert(SortedDirectoryIterator(file, ec)->path() == file);
    assert(!ec);
    assert(SortedDirectoryIterator(missing, ec) == end);
    assert(ec);

    // Both error-code overloads must allow allocation failures to propagate.
    for (bool explicit_options : {false, true}) {
        bool caught = false;
        fail_allocation = true;
        try {
            if (explicit_options) {
                SortedDirectoryIterator it(directory, std::filesystem::directory_options::none, ec);
            } else {
                SortedDirectoryIterator it(directory, ec);
            }
        } catch (const std::bad_alloc&) {
            caught = true;
        }
        fail_allocation = false;
        assert(caught);
    }
}
