#include "toml.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main() {
    auto config = toml::parse("Polybuild.toml");

    auto& paths_table = toml::find(config, "paths");
    auto include_paths = toml::find<std::vector<std::string>>(paths_table, "include");
    auto source_paths = toml::find<std::vector<std::string>>(paths_table, "source");

    auto& options_table = toml::find(config, "options");
    auto compiler = toml::find<std::string>(options_table, "compiler");
    auto compilation_flags = toml::find<std::string>(options_table, "compilation-flags");
    auto libraries = toml::find<std::vector<std::string>>(options_table, "libraries");

    std::ofstream output("Makefile");

    output << "CXX := " << compiler << std::endl;
    output << "CXXFLAGS := " << compilation_flags << std::endl;

    if (!libraries.empty()) {
        output << "LDFLAGS :=";
        for (const auto& library : libraries) {
            output << " -l" << library;
        }
        output << std::endl;
    }

    for (std::filesystem::path source_path : source_paths) {
        for (std::filesystem::directory_entry entry : std::filesystem::directory_iterator(source_path)) {
        }
    }
}
