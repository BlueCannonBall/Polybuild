#pragma once

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stddef.h>
#include <vector>

class SortedDirectoryIterator {
public:
    typedef std::filesystem::directory_entry value_type;
    typedef ptrdiff_t difference_type;
    typedef const std::filesystem::directory_entry* pointer;
    typedef const std::filesystem::directory_entry& reference;
    typedef std::input_iterator_tag iterator_category;

    SortedDirectoryIterator() noexcept = default;
    explicit SortedDirectoryIterator(const std::filesystem::path& path):
        SortedDirectoryIterator(path, std::filesystem::directory_options::none) {}
    SortedDirectoryIterator(const std::filesystem::path& path, std::filesystem::directory_options options):
        entries(std::make_shared<std::vector<std::filesystem::directory_entry>>()) {
        for (const auto& entry : std::filesystem::directory_iterator(path, options)) {
            entries->push_back(entry);
        }
        init();
    }
    SortedDirectoryIterator(const std::filesystem::path& path, std::error_code& ec) noexcept:
        SortedDirectoryIterator(path, std::filesystem::directory_options::none, ec) {}
    SortedDirectoryIterator(const std::filesystem::path& path, std::filesystem::directory_options options, std::error_code& ec) noexcept:
        entries(std::make_shared<std::vector<std::filesystem::directory_entry>>()) {
        std::filesystem::directory_iterator it(path, options, ec);
        if (!ec) {
            for (const auto& entry : it) {
                entries->push_back(entry);
            }
            init();
        } else {
            entries = nullptr; // Act as an end iterator on failure
        }
    }

    reference operator*() const {
        return (*entries)[cursor];
    }

    pointer operator->() const {
        return &(*entries)[cursor];
    }

    SortedDirectoryIterator& operator++() {
        if (entries) {
            ++cursor;
            if (cursor >= entries->size()) {
                entries = nullptr;
                cursor = 0;
            }
        }
        return *this;
    }

    SortedDirectoryIterator& increment(std::error_code& ec) {
        ec.clear();
        return ++(*this);
    }

    SortedDirectoryIterator operator++(int) {
        SortedDirectoryIterator ret = *this;
        ++(*this);
        return ret;
    }

    bool operator==(const SortedDirectoryIterator& other) const noexcept {
        if (!entries && !other.entries) {
            return true;
        }
        if (entries == other.entries && cursor == other.cursor) {
            return true;
        }
        return false;
    }

    bool operator!=(const SortedDirectoryIterator& other) const noexcept {
        return !(*this == other);
    }

private:
    std::shared_ptr<std::vector<std::filesystem::directory_entry>> entries;
    size_t cursor = 0;

    void init() {
        if (entries->empty()) {
            entries.reset(); // Instantly become an end iterator if empty
        } else {
            std::sort(entries->begin(), entries->end(), [](const auto& a, const auto& b) {
                return a.path().filename().string() < b.path().filename().string();
            });
        }
    }
};

inline SortedDirectoryIterator begin(SortedDirectoryIterator it) noexcept {
    return it;
}

inline SortedDirectoryIterator end(const SortedDirectoryIterator&) noexcept {
    return SortedDirectoryIterator();
}
