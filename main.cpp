#include "toml.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

int main() {
    auto config = toml::parse("Polybuild.toml");

    auto& paths_table = toml::find(config, "paths");
    auto include_paths = toml::find_or<std::vector<std::string>>(paths_table, "include", {"."});
    auto source_paths = toml::find<std::vector<std::string>>(paths_table, "source");
    auto artifact_path = toml::find<std::string>(paths_table, "artifact");

    auto& options_table = toml::find(config, "options");
    auto compiler = toml::find_or<std::string>(options_table, "compiler", {});
    auto compilation_flags = toml::find_or<std::string>(options_table, "compilation-flags", {});
    auto libraries = toml::find_or<std::vector<std::string>>(options_table, "libraries", {});

    std::ofstream output("Makefile");

    if (!compiler.empty()) {
        output << "CXX := " << compiler << std::endl;
    }
    if (!compilation_flags.empty()) {
        output << "CXXFLAGS := " << compilation_flags << std::endl;
    }

    if (!libraries.empty()) {
        output << "LDFLAGS :=";
        for (const auto& library : libraries) {
            output << " -l" << library;
        }
        output << std::endl;
    }

    std::regex regex(R"delimiter(\s*^#include\s*(<(.+)>|"(.+)").*$)delimiter", std::regex::optimize);
    for (std::filesystem::path source_path : source_paths) {
        for (std::filesystem::directory_entry entry :
            std::filesystem::directory_iterator(source_path)) {
            if (entry.is_regular_file() &&
                (entry.path().extension() == ".c" ||
                    entry.path().extension() == ".cpp" ||
                    entry.path().extension() == ".cc" ||
                    entry.path().extension() == ".cxx" ||
                    entry.path().extension() == ".cc")) {
                auto object_path = (std::filesystem::path(artifact_path) / entry.path()).replace_extension(".o");
                output << object_path << ": ";

                std::ifstream source_file(entry.path());
                for (std::string line; std::getline(source_file, line);) {
                    std::smatch matches;
                    if (std::regex_match(line, matches, regex)) {
                        for (std::filesystem::path include_path : include_paths) {
                            if (std::filesystem::is_regular_file(include_path / std::filesystem::path(matches[2]))) {
                            }
                        }
                    }
                }
            }
        }
    }
}
