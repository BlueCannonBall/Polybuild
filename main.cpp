#include "toml.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

std::string path_to_string(const std::filesystem::path& path) {
#ifdef _WIN32
    std::string ret = path.string();
    std::replace(ret.begin(), ret.end(), '\\', '/');
    return ret;
#else
    return path.string();
#endif
}

void find_dependencies(const std::filesystem::path& path, const std::vector<std::string>& include_paths, std::vector<std::filesystem::path>& ret) {
    const static std::regex angled_include_regex("^\\s*#\\s*include\\s*<(.+)>.*$", std::regex::optimize);
    const static std::regex quoted_include_regex("^\\s*#\\s*include\\s*\"(.+)\".*$", std::regex::optimize);

    std::ifstream source_file(path);
    for (std::string line; std::getline(source_file, line);) {
        std::smatch matches;
        if (std::regex_match(line, matches, angled_include_regex) ||
            std::regex_match(line, matches, quoted_include_regex)) {
            // First, check locally
            auto header_path = path.parent_path() / std::filesystem::path(std::string(matches[1]));
            if (std::filesystem::is_regular_file(header_path)) {
                if (std::find(ret.begin(), ret.end(), header_path) == ret.end()) {
                    ret.push_back(header_path);
                    find_dependencies(header_path, include_paths, ret);
                    continue;
                }
            }

            // Then, check the include path
            for (std::filesystem::path include_path : include_paths) {
                auto header_path = include_path / std::filesystem::path(std::string(matches[1]));
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

    auto paths_table = toml::find(config, "paths");
    auto output_path = toml::find<std::string>(paths_table, "output");
    auto source_paths = toml::find<std::vector<std::string>>(paths_table, "source");
    auto include_paths = toml::find_or<std::vector<std::string>>(paths_table, "include", {});
    auto library_paths = toml::find_or<std::vector<std::string>>(paths_table, "library", {});
    auto artifact_path = toml::find<std::string>(paths_table, "artifact");
    auto install_path = toml::find_or<std::string>(paths_table, "install", {});

    auto options_table = toml::find(config, "options");
    auto compiler = toml::find_or<std::string>(options_table, "compiler", "$(CXX)");
    auto compilation_flags = toml::find_or<std::string>(options_table, "compilation-flags", "$(CXXFLAGS)");
    auto libraries = toml::find_or<std::vector<std::string>>(options_table, "libraries", {});
    auto static_libraries = toml::find_or<std::vector<std::string>>(options_table, "static-libraries", {});
    auto preludes = toml::find_or<std::vector<std::string>>(options_table, "preludes", {});
    auto clean_preludes = toml::find_or<std::vector<std::string>>(options_table, "clean-preludes", {});
    auto is_shared = toml::find_or<bool>(options_table, "shared", false);
    auto is_static = toml::find_or<bool>(options_table, "static", false);

    std::ofstream output("Makefile");
    output << "# This file was auto-generated by Polybuild\n\n";

    output << "compiler := " << compiler << '\n';

    output << "compilation_flags := " << compilation_flags;
    for (const auto& include_path : include_paths) {
        output << " -I" << include_path;
    }
    for (const auto& library_path : library_paths) {
        output << " -L" << library_path;
    }
    if (is_shared) {
        output << " -fPIC";
        output << " -shared";
    }
    if (is_static) {
        output << " -static";
    }
    output << '\n';

    if (!libraries.empty()) {
        output << "libraries :=";
        for (const auto& library : libraries) {
            output << " -l" << library;
        }
        output << '\n';
    } else {
        output << "libraries := $(LDLIBS)\n";
    }

    if (!static_libraries.empty()) {
        output << "static_libraries :=";
        for (const auto& static_library : static_libraries) {
            output << ' ' << static_library;
        }
        output << '\n';
    }

    auto env_table = toml::find_or<toml::table>(config, "env", {});
    for (const auto& env_var_table : env_table) {
        for (const auto& env_var_value_table : env_var_table.second.as_table()) {
            auto custom_paths_table = toml::find_or(env_var_value_table.second, "paths", {});
            auto custom_library_paths = toml::find_or<std::vector<std::string>>(custom_paths_table, "library", std::vector<std::string>(library_paths));

            auto custom_options_table = toml::find_or(env_var_value_table.second, "options", {});
            auto custom_compilation_flags = toml::find_or<std::string>(custom_options_table, "compilation-flags", compilation_flags);
            auto custom_is_static = toml::find_or<bool>(custom_options_table, "static", is_static);

            output << "\nifeq ($(" << env_var_table.first << ")," << env_var_value_table.first << ")\n";

            if (custom_options_table.contains("compiler")) {
                output << "\tcompiler := " << custom_options_table.at("compiler") << '\n';
            }

            output << "\tcompilation_flags := " << custom_compilation_flags;
            for (const auto& include_path : include_paths) {
                output << " -I" << include_path;
            }
            for (const auto& library_path : custom_library_paths) {
                output << " -L" << library_path;
            }
            if (is_shared) {
                output << " -fPIC";
                output << " -shared";
            }
            if (custom_is_static) {
                output << " -static";
            }
            output << '\n';

            if (custom_options_table.contains("libraries")) {
                auto custom_libraries = toml::find<std::vector<std::string>>(custom_options_table, "libraries");
                output << "\tlibraries :=";
                for (const auto& library : custom_libraries) {
                    output << " -l" << library;
                }
                output << '\n';
            }

            if (custom_options_table.contains("static-libraries")) {
                auto custom_static_libraries = toml::find<std::vector<std::string>>(custom_options_table, "static-libraries");
                output << "\tstatic_libraries :=";
                for (const auto& static_library : custom_static_libraries) {
                    output << ' ' << static_library;
                }
                output << '\n';
            }

            output << "endif\n";
        }
    }

    output << "\ndefault: " << output_path << "\n.PHONY: default\n";

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
                std::filesystem::path object_path;
                for (int i = 0;; ++i) {
                    object_path = (std::filesystem::path(artifact_path) / (path_to_string(entry.path().stem()) + '_' + std::to_string(i))).replace_extension(".o");
                    if (std::find(object_paths.begin(), object_paths.end(), object_path) != object_paths.end()) {
                        continue;
                    } else {
                        object_paths.push_back(object_path);
                        break;
                    }
                }
                output << '\n'
                       << path_to_string(object_path) << ": " << path_to_string(entry.path());

                std::vector<std::filesystem::path> dependencies;
                find_dependencies(entry.path(), include_paths, dependencies);
                for (const auto& depdendency : dependencies) {
                    output << ' ' << path_to_string(depdendency);
                }

                output << "\n\t" << generate_echo("Compiling $@ from $<...") << '\n';
                output << "\t@mkdir -p " << artifact_path << '\n';
                output << "\t@$(compiler) -c $< $(compilation_flags) -o $@\n";
                output << '\t' << generate_echo("Finished compiling $@ from $<!") << '\n';
            }
        }
    }

    output << '\n'
           << output_path << ':';
    for (const auto& object_path : object_paths) {
        output << ' ' << path_to_string(object_path);
    }
    output << "\n\t" << generate_echo("Building $@...");
    {
        auto path = std::filesystem::path(output_path);
        if (path.has_parent_path()) {
            output << "\n\t@mkdir -p " << path_to_string(path.parent_path());
        }
    }
    for (const auto& prelude : preludes) {
        output << "\n\t" << generate_echo("Executing prelude: " + prelude);
        output << "\n\t@" << prelude;
    }
    output << "\n\t@$(compiler) $^ $(compilation_flags) $(libraries) $(static_libraries) -o $@\n\t" << generate_echo("Finished building $@!") << '\n';

    output << "\nclean:";
    for (const auto& clean_prelude : clean_preludes) {
        output << "\n\t" << generate_echo("Executing clean prelude: " + clean_prelude);
        output << "\n\t@" << clean_prelude;
    }
    output << "\n\t" << generate_echo("Deleting " + output_path + " and " + artifact_path + "...") << '\n';
    output << "\t@rm -rf " << output_path << ' ' << artifact_path << '\n';
    output << '\t' << generate_echo("Finished deleting " + output_path + " and " + artifact_path + '!') << '\n';
    output << ".PHONY: clean\n";

    if (!install_path.empty()) {
        output << "\ninstall:\n";
        output << '\t' << generate_echo("Copying " + output_path + " to " + install_path + "...") << '\n';
        output << "\t@cp " << output_path << ' ' << install_path << '\n';
        output << '\t' << generate_echo("Finished copying " + output_path + " to " + install_path + '!') << '\n';
        output << ".PHONY: install\n";
    }

    std::cout << generate_log("Finished converting Polybuild.toml to Makefile!") << std::endl;
    return 0;
}
