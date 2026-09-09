#include "build.hpp"
#include "toml.hpp"
#include "util.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

enum SourceFileType {
    SOURCE_FILE_C,
    SOURCE_FILE_CPP,
    SOURCE_FILE_NONE,
};

struct SourceFile {
    std::filesystem::path path;
    std::filesystem::path object_path;
    SourceFileType type;
    std::vector<std::filesystem::path> dependencies;
};

struct EnvBlock {
    unsigned int line;
    std::string variable;
    std::string value;
    const toml::value* table;
};

void validate_build_path(const std::string& path) {
    if (path.empty() || path.find_first_of(" \t\r\n$#:%*?[];|&<>`\\\"'") != std::string::npos) {
        throw std::runtime_error("Unsupported build path: " + path + ". Use paths without whitespace or Make/shell metacharacters.");
    }
}

void validate_table(const toml::value& table, const std::vector<std::string>& strings, const std::vector<std::string>& arrays, const std::vector<std::string>& booleans) {
    for (const auto& entry : table.as_table()) {
        if (std::find(strings.begin(), strings.end(), entry.first) != strings.end()) {
            entry.second.as_string();
        } else if (std::find(arrays.begin(), arrays.end(), entry.first) != arrays.end()) {
            toml::get<std::vector<std::string>>(entry.second);
        } else if (std::find(booleans.begin(), booleans.end(), entry.first) != booleans.end()) {
            entry.second.as_boolean();
        } else {
            throw std::runtime_error("Unknown configuration key: " + entry.first);
        }
    }
}

void validate_config(const toml::value& config, bool is_override = false) {
    for (const auto& entry : config.as_table()) {
        if (entry.first == "paths") {
            if (is_override) {
                validate_table(entry.second, {"install"}, {"library"}, {});
            } else {
                validate_table(entry.second, {"output", "artifact", "install"}, {"source", "include", "library"}, {});
            }
        } else if (entry.first == "options") {
            std::vector<std::string> strings {"c-compiler", "cpp-compiler", "compiler", "c-compilation-flags", "cpp-compilation-flags", "compilation-flags", "link-time-flags"};
            std::vector<std::string> arrays {"libraries", "static-libraries", "pkg-config-libraries"};
            std::vector<std::string> booleans {"static"};

            if (!is_override) {
                arrays.insert(arrays.end(), {"preludes", "clean-preludes"});
                booleans.push_back("shared");
            }
            validate_table(entry.second, strings, arrays, booleans);
        } else if (entry.first == "env" && !is_override) {
            for (const auto& variable : entry.second.as_table()) {
                if (variable.first.empty() ||
                    (variable.first.front() >= '0' && variable.first.front() <= '9') ||
                    variable.first.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_0123456789") != std::string::npos) {
                    throw std::runtime_error("Invalid environment variable: " + variable.first);
                }

                for (const auto& value : variable.second.as_table()) {
                    if (value.first.find_first_of("\r\n(),$") != std::string::npos) {
                        throw std::runtime_error("Invalid environment condition: " + value.first);
                    }
                    validate_config(value.second, true);
                }
            }
        } else {
            throw std::runtime_error("Unknown configuration table: " + entry.first);
        }
    }
}

SourceFileType get_source_file_type(std::filesystem::path path) {
    if (path.extension() == ".c") {
        return SOURCE_FILE_C;
    } else if (path.extension() == ".cpp" ||
               path.extension() == ".cc" ||
               path.extension() == ".cxx") {
        return SOURCE_FILE_CPP;
    } else {
        return SOURCE_FILE_NONE;
    }
}

void find_dependencies(const std::filesystem::path& path, const std::vector<std::string>& include_paths, std::vector<std::filesystem::path>& ret) {
    const static std::regex angled_include_regex("^\\s*#\\s*include\\s*<([^>]+)>.*$", std::regex::optimize);
    const static std::regex quoted_include_regex("^\\s*#\\s*include\\s*\"([^\"]+)\".*$", std::regex::optimize);

    std::ifstream source_file(path);
    if (!source_file) {
        throw std::runtime_error("Failed to open source file: " + path.generic_string());
    }

    for (std::string line; std::getline(source_file, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::smatch matches;
        if (std::regex_match(line, matches, quoted_include_regex)) {
            // First, check locally
            auto header_path = (path.parent_path() / std::filesystem::path(std::string(matches[1]))).lexically_normal();
            if (std::filesystem::is_regular_file(header_path)) {
                if (std::find(ret.begin(), ret.end(), header_path) == ret.end()) {
                    ret.push_back(header_path);
                    find_dependencies(header_path, include_paths, ret);
                    continue;
                }
            }

            // Then, check the include path
            for (std::filesystem::path include_path : include_paths) {
                auto header_path = (include_path / std::filesystem::path(std::string(matches[1]))).lexically_normal();
                if (std::filesystem::is_regular_file(header_path)) {
                    if (std::find(ret.begin(), ret.end(), header_path) == ret.end()) {
                        ret.push_back(header_path);
                        find_dependencies(header_path, include_paths, ret);
                    }
                }
            }
        } else if (std::regex_match(line, matches, angled_include_regex)) {
            for (std::filesystem::path include_path : include_paths) {
                auto header_path = (include_path / std::filesystem::path(std::string(matches[1]))).lexically_normal();
                if (std::filesystem::is_regular_file(header_path)) {
                    if (std::find(ret.begin(), ret.end(), header_path) == ret.end()) {
                        ret.push_back(header_path);
                        find_dependencies(header_path, include_paths, ret);
                    }
                }
            }
        }
    }
    if (source_file.bad() || !source_file.eof()) {
        throw std::runtime_error("Failed to read source file: " + path.generic_string());
    }
}

std::vector<SourceFile> find_sources(const std::vector<std::string>& source_paths, const std::vector<std::string>& include_paths, const std::string& artifact_path) {
    std::vector<SourceFile> ret;
    std::set<std::filesystem::path> discovered_paths;
    for (const auto& source_path : source_paths) {
        for (const auto& entry : SortedDirectoryIterator(source_path)) {
            auto type = get_source_file_type(entry.path());
            if (!entry.is_regular_file() || type == SOURCE_FILE_NONE) {
                continue;
            }
            if (!discovered_paths.insert(std::filesystem::absolute(entry.path()).lexically_normal()).second) {
                continue;
            }

            validate_build_path(entry.path().generic_string());
            SourceFile source {entry.path(), {}, type, {}};
            for (unsigned int i = 0;; ++i) {
                source.object_path = std::filesystem::path(artifact_path) / (entry.path().stem().string() + '_' + std::to_string(i));
                if (std::none_of(ret.begin(), ret.end(), [&](const SourceFile& other) {
                        return other.object_path == source.object_path;
                    })) {
                    break;
                }
            }

            find_dependencies(source.path, include_paths, source.dependencies);
            for (const auto& dependency : source.dependencies) {
                validate_build_path(dependency.generic_string());
            }
            ret.push_back(std::move(source));
        }
    }

    if (ret.empty()) {
        throw std::runtime_error("No C or C++ source files found in paths.source.");
    }
    return ret;
}

std::string echo(const std::string& str) {
    std::ostringstream ss;
    ss << "@printf \"\\033[1m[POLYBUILD]\\033[0m %s\\n\"" << ' ' << std::quoted(str);
    return ss.str();
}

std::ostream& append_compilation_flags(std::ostream& os, const std::string& variable, const std::vector<std::string>& include_paths, bool is_shared) {
    os << variable << " += $(active_debug_compilation_flag)";
    for (const auto& include_path : include_paths) {
        os << " $(include_path_flag)" << std::quoted(include_path);
    }
    if (is_shared) {
        os << " $(shared_flag)";
    }
    os << " $(active_linkage_flag)\n";
    return os;
}

GeneratedFiles generate(const std::filesystem::path& config_path) {
    auto config = toml::parse(config_path.string());
    validate_config(config);

    auto paths_table = toml::find(config, "paths");
    auto output_path = toml::find<std::string>(paths_table, "output");
    auto source_paths = toml::find<std::vector<std::string>>(paths_table, "source");
    auto include_paths = toml::find_or<std::vector<std::string>>(paths_table, "include", {});
    auto library_paths = toml::find_or<std::vector<std::string>>(paths_table, "library", {});
    auto artifact_path = toml::find_or<std::string>(paths_table, "artifact", "obj");
    auto install_path = toml::find_or<std::string>(paths_table, "install", {});

    toml::value options_table = toml::find_or<toml::table>(config, "options", {});
    auto c_compiler = toml::find_or<std::string>(options_table, "c-compiler", "$(CC)");
    auto cpp_compiler = toml::find_or<std::string>(options_table, "cpp-compiler", toml::find_or<std::string>(options_table, "compiler", "$(CXX)"));

    auto c_compilation_flags = toml::find_or<std::string>(options_table, "c-compilation-flags", "$(CFLAGS)");
    auto cpp_compilation_flags = toml::find_or<std::string>(options_table, "cpp-compilation-flags", toml::find_or<std::string>(options_table, "compilation-flags", "$(CXXFLAGS)"));
    auto link_time_flags = toml::find_or<std::string>(options_table, "link-time-flags", "$(LDFLAGS)");

    auto libraries = toml::find_or<std::vector<std::string>>(options_table, "libraries", {});
    auto static_libraries = toml::find_or<std::vector<std::string>>(options_table, "static-libraries", {});
    auto pkg_config_libraries = toml::find_or<std::vector<std::string>>(options_table, "pkg-config-libraries", {});

    auto preludes = toml::find_or<std::vector<std::string>>(options_table, "preludes", {});
    auto clean_preludes = toml::find_or<std::vector<std::string>>(options_table, "clean-preludes", {});
    auto is_shared = toml::find_or<bool>(options_table, "shared", false);
    auto is_static = toml::find_or<bool>(options_table, "static", false);

    validate_build_path(output_path);
    validate_build_path(artifact_path);

    std::vector<EnvBlock> env_blocks;
    auto env_table = toml::find_or<toml::table>(config, "env", {});
    for (const auto& env_var_table : env_table) {
        for (const auto& env_var_value_table : env_var_table.second.as_table()) {
            env_blocks.push_back({env_var_value_table.second.location().line(), env_var_table.first, env_var_value_table.first, &env_var_value_table.second});
        }
    }

    std::sort(env_blocks.begin(), env_blocks.end(), [](const EnvBlock& a, const EnvBlock& b) {
        return std::tie(a.line, a.variable, a.value) < std::tie(b.line, b.variable, b.value);
    });

    auto sources = find_sources(source_paths, include_paths, artifact_path);

    std::ostringstream makefile;
    makefile << "# This file was auto-generated by Polybuild\n\n";

    makefile << "include_path_flag := -I\n";
    makefile << "library_path_flag := -L\n";
    makefile << "obj_path_flag := -o\n";
    makefile << "out_path_flag := -o\n";
    makefile << "library_flag := -l\n";

    makefile << "release_dynamic_flag :=\n";
    makefile << "release_static_flag := -static\n";
    makefile << "debug_dynamic_flag :=\n";
    makefile << "debug_static_flag := -static\n";
    makefile << "debug_compilation_flag := -g\n";
    makefile << "debug_link_flag :=\n";

    makefile << "shared_flag := -shared -fPIC\n";
    makefile << "compile_only_flag := -c\n";
    makefile << "link_flag :=\n";
    makefile << "pkg_config_syntax :=\n";

    makefile << "obj_ext := .o\n";
    if (is_shared) {
        makefile << "out_ext := .so\n";
    } else {
        makefile << "out_ext :=\n";
    }

    makefile << "ifeq ($(OS),Windows_NT)\n";
    makefile << "\tinclude_path_flag := /I\n";
    makefile << "\tlibrary_path_flag := /LIBPATH:\n";
    makefile << "\tobj_path_flag := /Fo:\n";
    makefile << "\tout_path_flag := /Fe:\n";
    makefile << "\tlibrary_flag :=\n";

    makefile << "\trelease_dynamic_flag := /MD\n";
    makefile << "\trelease_static_flag := /MT\n";
    makefile << "\tdebug_dynamic_flag := /MDd\n";
    makefile << "\tdebug_static_flag := /MTd\n";
    makefile << "\tdebug_compilation_flag := /Zi\n";
    makefile << "\tdebug_link_flag := /DEBUG\n";

    makefile << "\tshared_flag := /LD\n";
    makefile << "\tcompile_only_flag := /c\n";
    makefile << "\tlink_flag := /link\n";
    makefile << "\tpkg_config_syntax := --msvc-syntax\n";

    makefile << "\tobj_ext := .obj\n";
    if (is_shared) {
        makefile << "\tout_ext := .dll\n";
    } else {
        makefile << "\tout_ext := .exe\n";
    }
    makefile << "endif\n\n";

    makefile << "active_dynamic_flag := $(release_dynamic_flag)\n";
    makefile << "active_static_flag := $(release_static_flag)\n";
    makefile << "active_debug_compilation_flag :=\n";
    makefile << "active_debug_link_flag :=\n";
    makefile << "ifeq ($(MODE),debug)\n";
    makefile << "\tactive_debug_compilation_flag := $(debug_compilation_flag)\n";
    makefile << "\tactive_debug_link_flag := $(debug_link_flag)\n";
    makefile << "\tactive_dynamic_flag := $(debug_dynamic_flag)\n";
    makefile << "\tactive_static_flag := $(debug_static_flag)\n";
    makefile << "endif\n\n";

    makefile << "c_compiler := " << std::quoted(c_compiler) << '\n';
    makefile << "cpp_compiler := " << std::quoted(cpp_compiler) << '\n';

    makefile << "c_compilation_flags := " << c_compilation_flags << '\n';
    makefile << "cpp_compilation_flags := " << cpp_compilation_flags << '\n';
    makefile << "link_time_flags := " << link_time_flags << '\n';
    makefile << "is_static := " << (is_static ? "true" : "false") << '\n';

    makefile << "library_paths :=";
    for (const auto& library_path : library_paths) {
        makefile << " $(library_path_flag)" << std::quoted(library_path);
    }
    makefile << '\n';

    makefile << "libraries :=";
    for (const auto& library : libraries) {
        makefile << " $(library_flag)" << std::quoted(library);
    }
    makefile << '\n';

    makefile << "pkg_config_libraries :=";
    for (const auto& pkg_config_library : pkg_config_libraries) {
        makefile << ' ' << std::quoted(pkg_config_library);
    }
    makefile << '\n';

    if (!static_libraries.empty()) {
        makefile << "static_libraries :=";
        for (const auto& static_library : static_libraries) {
            makefile << ' ' << static_library;
        }
        makefile << '\n';
    }

    if (!install_path.empty()) {
        makefile << "prefix := " << std::quoted(install_path) << '\n';
    }

    for (const auto& env_block : env_blocks) {
        auto custom_paths_table = toml::find_or<toml::table>(*env_block.table, "paths", {});
        auto custom_options_table = toml::find_or<toml::table>(*env_block.table, "options", {});

        makefile << "\nifeq ($(" << env_block.variable << ")," << env_block.value << ")\n";

        if (auto it = custom_options_table.find("c-compiler"); it != custom_options_table.end()) {
            makefile << "\tc_compiler := " << std::quoted(toml::get<std::string>(it->second)) << '\n';
        }
        if (auto it = custom_options_table.find("cpp-compiler"); it != custom_options_table.end()) {
            makefile << "\tcpp_compiler := " << std::quoted(toml::get<std::string>(it->second)) << '\n';
        } else if (auto it = custom_options_table.find("compiler"); it != custom_options_table.end()) {
            makefile << "\tcpp_compiler := " << std::quoted(toml::get<std::string>(it->second)) << '\n';
        }

        if (auto it = custom_options_table.find("c-compilation-flags"); it != custom_options_table.end()) {
            makefile << "\tc_compilation_flags := " << toml::get<std::string>(it->second) << '\n';
        }
        if (auto it = custom_options_table.find("cpp-compilation-flags"); it != custom_options_table.end()) {
            makefile << "\tcpp_compilation_flags := " << toml::get<std::string>(it->second) << '\n';
        } else if (auto it = custom_options_table.find("compilation-flags"); it != custom_options_table.end()) {
            makefile << "\tcpp_compilation_flags := " << toml::get<std::string>(it->second) << '\n';
        }
        if (auto it = custom_options_table.find("link-time-flags"); it != custom_options_table.end()) {
            makefile << "\tlink_time_flags := " << toml::get<std::string>(it->second) << '\n';
        }
        if (auto it = custom_options_table.find("static"); it != custom_options_table.end()) {
            makefile << "\tis_static := " << (toml::get<bool>(it->second) ? "true" : "false") << '\n';
        }

        if (auto it = custom_paths_table.find("library"); it != custom_paths_table.end()) {
            auto custom_library_paths = toml::get<std::vector<std::string>>(it->second);
            makefile << "\tlibrary_paths :=";
            for (const auto& library_path : custom_library_paths) {
                makefile << " $(library_path_flag)" << std::quoted(library_path);
            }
            makefile << '\n';
        }
        if (auto it = custom_options_table.find("libraries"); it != custom_options_table.end()) {
            auto custom_libraries = toml::get<std::vector<std::string>>(it->second);
            makefile << "\tlibraries :=";
            for (const auto& library : custom_libraries) {
                makefile << " $(library_flag)" << std::quoted(library);
            }
            makefile << '\n';
        }
        if (auto it = custom_options_table.find("pkg-config-libraries"); it != custom_options_table.end()) {
            auto custom_pkg_config_libraries = toml::get<std::vector<std::string>>(it->second);
            makefile << "\tpkg_config_libraries :=";
            for (const auto& pkg_config_library : custom_pkg_config_libraries) {
                makefile << ' ' << std::quoted(pkg_config_library);
            }
            makefile << '\n';
        }
        if (auto it = custom_options_table.find("static-libraries"); it != custom_options_table.end()) {
            auto custom_static_libraries = toml::get<std::vector<std::string>>(it->second);
            makefile << "\tstatic_libraries :=";
            for (const auto& static_library : custom_static_libraries) {
                makefile << ' ' << static_library;
            }
            makefile << '\n';
        }

        if (auto it = custom_paths_table.find("install"); it != custom_paths_table.end()) {
            makefile << "\tprefix := " << std::quoted(toml::get<std::string>(it->second)) << '\n';
        }
        makefile << "endif\n";
    }

    makefile << "\nactive_linkage_flag := $(active_dynamic_flag)\n";
    makefile << "ifeq ($(is_static),true)\n";
    makefile << "\tactive_linkage_flag := $(active_static_flag)\n";
    makefile << "endif\n\n";

    append_compilation_flags(makefile, "c_compilation_flags", include_paths, is_shared);
    append_compilation_flags(makefile, "cpp_compilation_flags", include_paths, is_shared);
    makefile << "link_time_flags += $(active_debug_link_flag) $(library_paths)\n";

    makefile << "ifneq ($(strip $(pkg_config_libraries)),)\n";
    makefile << "\tc_compilation_flags += `pkg-config $(pkg_config_syntax) --cflags $(pkg_config_libraries)`\n";
    makefile << "\tcpp_compilation_flags += `pkg-config $(pkg_config_syntax) --cflags $(pkg_config_libraries)`\n";
    makefile << "\tlibraries += `pkg-config $(pkg_config_syntax) --libs $(pkg_config_libraries)`\n";
    makefile << "endif\n";

    makefile << "\nall: " << output_path << "$(out_ext)\n";
    makefile << ".PHONY: all\n";

    bool has_cpp = false;
    for (const auto& source : sources) {
        makefile << '\n'
                 << source.object_path.generic_string() << "$(obj_ext): " << source.path.generic_string() << " .polybuild.mk";
        for (const auto& dependency : source.dependencies) {
            makefile << ' ' << dependency.generic_string();
        }
        makefile << '\n';

        makefile << '\t' << echo("Compiling $@ from $<...") << '\n';
        makefile << "\t@mkdir -p " << std::quoted(artifact_path) << '\n';
        if (source.type == SOURCE_FILE_CPP) {
            makefile << "\t@$(cpp_compiler) $(compile_only_flag) \"$<\" $(cpp_compilation_flags) \"$(obj_path_flag)$@\"\n";
            has_cpp = true;
        } else {
            makefile << "\t@$(c_compiler) $(compile_only_flag) \"$<\" $(c_compilation_flags) \"$(obj_path_flag)$@\"\n";
        }
        makefile << '\t' << echo("Finished compiling $@ from $<!") << '\n';
    }

    makefile << "\nobjects := ";
    for (const auto& source : sources) {
        makefile << ' ' << source.object_path.generic_string() << "$(obj_ext)";
    }
    makefile << '\n';

    makefile << output_path << "$(out_ext): .polybuild.mk $(objects) $(static_libraries)\n";
    makefile << "\t" << echo("Building $@...") << '\n';
    {
        auto path = std::filesystem::path(output_path);
        if (path.has_parent_path()) {
            makefile << "\t@mkdir -p " << std::quoted(path.parent_path().generic_string()) << '\n';
        }
    }

    if (has_cpp) {
        makefile << "\t@$(cpp_compiler) $(objects) $(static_libraries) $(cpp_compilation_flags) \"$(out_path_flag)$@\" $(link_flag) $(link_time_flags) $(libraries)\n\t" << echo("Finished building $@!") << '\n';
    } else {
        makefile << "\t@$(c_compiler) $(objects) $(static_libraries) $(c_compilation_flags) \"$(out_path_flag)$@\" $(link_flag) $(link_time_flags) $(libraries)\n\t" << echo("Finished building $@!") << '\n';
    }

    makefile << "\nclean:";
    for (const auto& clean_prelude : clean_preludes) {
        makefile << "\n\t" << echo("Executing clean prelude: " + clean_prelude);
        makefile << "\n\t@" << clean_prelude;
    }

    makefile << "\n\t" << echo("Deleting " + output_path + "$(out_ext) and " + artifact_path + "...") << '\n';
    makefile << "\t@rm -rf " << std::quoted(output_path + "$(out_ext)") << ' ' << std::quoted(artifact_path) << '\n';
    makefile << '\t' << echo("Finished deleting " + output_path + "$(out_ext) and " + artifact_path + '!') << '\n';
    makefile << ".PHONY: clean\n";

    makefile << "\ninstall:\n";
    makefile << '\t' << echo("Copying " + output_path + "$(out_ext) to $(prefix)...") << '\n';
    makefile << "\t@cp " << std::quoted(output_path + "$(out_ext)") << " $(prefix)\n";
    makefile << '\t' << echo("Finished copying " + output_path + "$(out_ext) to $(prefix)!") << '\n';
    makefile << ".PHONY: install\n";

    std::ostringstream wrapper;
    wrapper << "# This file was auto-generated by Polybuild\n\n";

    wrapper << "ifndef MODE\n";
    wrapper << "\tMODE := release\n";
    wrapper << "\texport MODE\n";
    wrapper << "endif\n\n";

    wrapper << "ifndef OS\n";
    wrapper << "\tOS := $(shell uname)\n";
    wrapper << "\texport OS\n";
    wrapper << "endif\n\n";

    wrapper << "ifeq ($(OS),Windows_NT)\n";
    wrapper << "\tCC := cl\n";
    wrapper << "\tCXX := cl\n";
    wrapper << "\tCL := /nologo\n";
    wrapper << "\tLINK := /nologo\n";
    wrapper << "\tMSYS_NO_PATHCONV := 1\n";
    wrapper << "\texport CC CXX CL MSYS_NO_PATHCONV\n";
    wrapper << "endif\n";

    wrapper << "\nall:";
    for (unsigned int i = 0; i < preludes.size(); ++i) {
        wrapper << " prelude" << i;
    }
    wrapper << "\n\t@\"$(MAKE)\" -f .polybuild.mk --no-print-directory\n";
    wrapper << ".PHONY: all\n";

    for (unsigned int i = 0; i < preludes.size(); ++i) {
        wrapper << "\nprelude" << i << ":\n";
        wrapper << '\t' << echo("Executing prelude: " + preludes[i]) << '\n';
        wrapper << "\t@" << preludes[i] << '\n';
        wrapper << ".PHONY: prelude" << i << '\n';
    }

    wrapper << "\nclean:\n";
    wrapper << "\t@\"$(MAKE)\" -f .polybuild.mk --no-print-directory $@\n";
    wrapper << ".PHONY: clean\n";

    wrapper << "\ninstall:\n";
    wrapper << "\t@\"$(MAKE)\" -f .polybuild.mk --no-print-directory $@\n";
    wrapper << ".PHONY: install\n";

    return {wrapper.str(), makefile.str()};
}
