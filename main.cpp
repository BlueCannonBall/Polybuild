#include "toml.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

void find_dependencies(const std::filesystem::path& path, const std::vector<std::string>& include_paths, std::vector<std::filesystem::path>& ret) {
    const static std::regex angled_include_regex("^\\s*#\\s*include\\s*<(.+)>.*$", std::regex::optimize);
    const static std::regex quoted_include_regex("^\\s*#\\s*include\\s*\"(.+)\".*$", std::regex::optimize);

    std::ifstream source_file(path);
    for (std::string line; std::getline(source_file, line);) {
        std::smatch matches;
        if (std::regex_match(line, matches, angled_include_regex) ||
            std::regex_match(line, matches, quoted_include_regex)) {
            // First, check locally
            auto header_path = path.parent_path() / std::filesystem::path(matches[1]);
            if (std::filesystem::is_regular_file(header_path)) {
                if (std::find(ret.begin(), ret.end(), header_path) == ret.end()) {
                    ret.push_back(header_path);
                    find_dependencies(header_path, include_paths, ret);
                    continue;
                }
            }

            // Then, check the include path
            for (std::filesystem::path include_path : include_paths) {
                auto header_path = include_path / std::filesystem::path(matches[1]);
                if (std::filesystem::is_regular_file(header_path)) {
                    if (std::find(ret.begin(), ret.end(), header_path) == ret.end()) {
                        ret.push_back(header_path);
                        find_dependencies(header_path, include_paths, ret);
                    }
                }
            }
        }
    }
}

std::string generate_echo(const std::string& str) {
    return "@printf '\\033[1m[POLYBUILD]\\033[0m " + str + "\\n'";
}

std::string generate_log(const std::string& str) {
    return "\033[1m[POLYBUILD]\033[0m " + str;
}

int main() {
    std::cout << generate_log("Converting Polybuild.toml to Makefile...") << std::endl;
    auto config = toml::parse("Polybuild.toml");

    auto& paths_table = toml::find(config, "paths");
    auto output_path = toml::find<std::string>(paths_table, "output");
    auto source_paths = toml::find<std::vector<std::string>>(paths_table, "source");
    auto include_paths = toml::find_or<std::vector<std::string>>(paths_table, "include", {});
    auto library_paths = toml::find_or<std::vector<std::string>>(paths_table, "library", {});
    auto artifact_path = toml::find<std::string>(paths_table, "artifact");
    auto install_path = toml::find_or<std::string>(paths_table, "install", {});

    auto& options_table = toml::find(config, "options");
    auto compiler = toml::find_or<std::string>(options_table, "compiler", {});
    auto compilation_flags = toml::find_or<std::string>(options_table, "compilation-flags", {});
    auto libraries = toml::find_or<std::vector<std::string>>(options_table, "libraries", {});

    std::ofstream output("Makefile");
    output << "# This file was auto-generated by Polybuild\n\n";

    if (!compiler.empty()) {
        output << "compiler := " << compiler << '\n';
    } else {
        output << "compiler := $(CXX)\n";
    }
    if (!compilation_flags.empty()) {
        output << "compilation_flags := " << compilation_flags << '\n';
    } else {
        output << "compilation_flags := $(CXXFLAGS)\n";
    }
    if (!include_paths.empty()) {
        output << "compilation_flags +=";
        for (const auto& include_path : include_paths) {
            output << " -I" << include_path;
        }
        output << '\n';
    }
    if (!library_paths.empty()) {
        output << "compilation_flags +=";
        for (const auto& library_path : library_paths) {
            output << " -L" << library_path;
        }
        output << '\n';
    }
    if (!libraries.empty()) {
        output << "libraries :=";
        for (const auto& library : libraries) {
            output << " -l" << library;
        }
        output << '\n';
    } else {
        output << "libraries := $(LDLIBS)\n";
    }

    output << "\ndefault: " << output_path << "\n.PHONY: default\n\n";

    std::vector<std::filesystem::path> object_paths;
    for (std::filesystem::path source_path : source_paths) {
        for (std::filesystem::directory_entry entry :
            std::filesystem::directory_iterator(source_path)) {
            if (entry.is_regular_file() &&
                (entry.path().extension() == ".c" ||
                    entry.path().extension() == ".cpp" ||
                    entry.path().extension() == ".cc" ||
                    entry.path().extension() == ".cxx" ||
                    entry.path().extension() == ".cc")) {
                auto object_path = (std::filesystem::path(artifact_path) / entry.path().filename()).replace_extension(".o");
                output << object_path.string() << ": " << entry.path().string();
                object_paths.push_back(object_path);

                std::vector<std::filesystem::path> dependencies;
                find_dependencies(entry.path(), include_paths, dependencies);
                for (const auto& depdendency : dependencies) {
                    output << ' ' << depdendency.string();
                }

                output << "\n\t" << generate_echo("Compiling $@ from $<...") << '\n';
                output << "\t@mkdir -p " << artifact_path << '\n';
                output << "\t@$(compiler) -c $< $(compilation_flags) -o $@\n";
                output << '\t' << generate_echo("Finished compiling $@ from $<!") << "\n\n";
            }
        }
    }

    output << output_path << ':';
    for (const auto& object_path : object_paths) {
        output << ' ' << object_path.string();
    }
    output << "\n\t" << generate_echo("Building $@...") << '\n';
    {
        auto path = std::filesystem::path(output_path);
        if (path.has_parent_path()) {
            output << "\t@mkdir -p " << path.parent_path().string() << '\n';
        }
    }
    output << "\t@$(compiler) $^ $(compilation_flags) $(libraries) -o $@\n";
    output << '\t' << generate_echo("Finished building $@!") << "\n\n";

    output << "clean:\n";
    output << '\t' << generate_echo("Deleting " + output_path + " and " + artifact_path + "...") << '\n';
    output << "\t@rm -rf " << output_path << ' ' << artifact_path << '\n';
    output << '\t' << generate_echo("Finished deleting " + output_path + " and " + artifact_path + '!') << '\n';
    output << ".PHONY: clean\n\n";

    if (!install_path.empty()) {
        output << "install:\n";
        output << '\t' << generate_echo("Copying " + output_path + " to " + install_path + "...") << '\n';
        output << "\t@cp " << output_path << ' ' << install_path << '\n';
        output << '\t' << generate_echo("Finished copying " + output_path + " to " + install_path + '!') << '\n';
        output << ".PHONY: install\n";
    }

    std::cout << generate_log("Finished converting Polybuild.toml to Makefile!") << std::endl;
    return 0;
}
