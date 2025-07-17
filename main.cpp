#include "toml.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

enum SourceFileType {
    SOURCE_FILE_C,
    SOURCE_FILE_CPP,
    SOURCE_FILE_NONE,
};

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

std::string echo(const std::string& str) {
    std::ostringstream ss;
    ss << "@printf \"\\033[1m[POLYBUILD]\\033[0m %s\\n\"" << ' ' << std::quoted(str);
    return ss.str();
}

std::string log(const std::string& str) {
    return "\033[1m[POLYBUILD]\033[0m " + str;
}

std::ostream& generate_compilation_flags(std::ostream& os, const std::string& variable, const std::string& flags, const std::vector<std::string>& include_paths, bool is_shared, bool is_static, const std::vector<std::string>& pkg_config_libraries) {
    os << variable << " := " << flags;
    for (const auto& include_path : include_paths) {
        os << " $(include_path_flag)" << include_path;
    }
    if (is_shared) {
        os << " $(shared_flag)";
    }
    if (is_static) {
        os << " $(static_flag)";
    } else {
        os << " $(dynamic_flag)";
    }
    if (!pkg_config_libraries.empty()) {
        os << " `pkg-config $(pkg_config_syntax) --cflags";
        for (const auto& pkg_config_library : pkg_config_libraries) {
            os << ' ' << pkg_config_library;
        }
        os << '`';
    }
    os << '\n';
    return os;
}

int main() {
    std::cout << log("Converting Polybuild.toml to makefile...") << std::endl;
    auto config = toml::parse("Polybuild.toml");

    auto paths_table = toml::find(config, "paths");
    auto output_path = toml::find<std::string>(paths_table, "output");
    auto source_paths = toml::find<std::vector<std::string>>(paths_table, "source");
    auto include_paths = toml::find_or<std::vector<std::string>>(paths_table, "include", {});
    auto library_paths = toml::find_or<std::vector<std::string>>(paths_table, "library", {});
    auto artifact_path = toml::find<std::string>(paths_table, "artifact");
    auto install_path = toml::find_or<std::string>(paths_table, "install", {});

    auto options_table = toml::find(config, "options");
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

    std::ofstream makefile(".polybuild.mk");
    makefile << "# This file was auto-generated by Polybuild\n\n";

    makefile << "include_path_flag := -I\n";
    makefile << "library_path_flag := -L\n";
    makefile << "obj_path_flag := -o\n";
    makefile << "out_path_flag := -o\n";
    makefile << "library_flag := -l\n";
    makefile << "static_flag := -static\n";
    makefile << "shared_flag := -shared -fPIC\n";
    makefile << "compile_only_flag := -c\n";
    makefile << "obj_ext := .o\n";
    if (is_shared) {
        makefile << "out_ext := .so\n";
    }
    makefile << "ifeq ($(OS),Windows_NT)\n";
    makefile << "\tinclude_path_flag := /I\n";
    makefile << "\tlibrary_path_flag := /LIBPATH:\n";
    makefile << "\tobj_path_flag := /Fo:\n";
    makefile << "\tout_path_flag := /Fe:\n";
    makefile << "\tlibrary_flag :=\n";
    makefile << "\tdynamic_flag := /MD\n";
    makefile << "\tstatic_flag := /MT\n";
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

    makefile << "c_compiler := " << c_compiler << '\n';
    makefile << "cpp_compiler := " << cpp_compiler << '\n';

    generate_compilation_flags(makefile, "c_compilation_flags", c_compilation_flags, include_paths, is_shared, is_static, pkg_config_libraries);
    generate_compilation_flags(makefile, "cpp_compilation_flags", cpp_compilation_flags, include_paths, is_shared, is_static, pkg_config_libraries);

    makefile << "link_time_flags := " << link_time_flags;
    for (const auto& library_path : library_paths) {
        makefile << " $(library_path_flag)" << library_path;
    }
    makefile << '\n';

    makefile << "libraries :=";
    for (const auto& library : libraries) {
        makefile << " $(library_flag)" << library;
    }
    if (!pkg_config_libraries.empty()) {
        makefile << " `pkg-config $(pkg_config_syntax) --libs";
        for (const auto& pkg_config_library : pkg_config_libraries) {
            makefile << ' ' << pkg_config_library;
        }
        makefile << '`';
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
        makefile << "prefix := " << install_path << '\n';
    }

    auto env_table = toml::find_or<toml::table>(config, "env", {});
    for (const auto& env_var_table : env_table) {
        for (const auto& env_var_value_table : env_var_table.second.as_table()) {
            auto custom_paths_table = toml::find_or(env_var_value_table.second, "paths", {});
            auto custom_library_paths = toml::find_or<std::vector<std::string>>(custom_paths_table, "library", std::vector<std::string>(library_paths));
            auto custom_install_path = toml::find_or<std::string>(custom_paths_table, "install", install_path);

            auto custom_options_table = toml::find_or(env_var_value_table.second, "options", {});
            auto custom_c_compiler = toml::find_or<std::string>(custom_options_table, "c-compiler", c_compiler);
            auto custom_cpp_compiler = toml::find_or<std::string>(custom_options_table, "cpp-compiler", toml::find_or<std::string>(custom_options_table, "compiler", cpp_compiler));
            auto custom_c_compilation_flags = toml::find_or<std::string>(custom_options_table, "c-compilation-flags", c_compilation_flags);
            auto custom_cpp_compilation_flags = toml::find_or<std::string>(custom_options_table, "cpp-compilation-flags", toml::find_or(custom_options_table, "compilation-flags", cpp_compilation_flags));
            auto custom_link_time_flags = toml::find_or<std::string>(custom_options_table, "link-time-flags", link_time_flags);
            auto custom_libraries = toml::find_or<std::vector<std::string>>(custom_options_table, "libraries", std::vector<std::string>(libraries));
            auto custom_pkg_config_libraries = toml::find_or<std::vector<std::string>>(custom_options_table, "pkg-config-libraries", std::vector<std::string>(pkg_config_libraries));
            auto custom_is_static = toml::find_or<bool>(custom_options_table, "static", is_static);

            makefile << "\nifeq ($(" << env_var_table.first << ")," << env_var_value_table.first << ")\n";

            makefile << "\tc_compiler := " << custom_c_compiler << '\n';
            makefile << "\tcpp_compiler := " << custom_cpp_compiler << '\n';

            generate_compilation_flags(makefile << '\t', "c_compilation_flags", custom_c_compilation_flags, include_paths, is_shared, custom_is_static, custom_pkg_config_libraries);
            generate_compilation_flags(makefile << '\t', "cpp_compilation_flags", custom_cpp_compilation_flags, include_paths, is_shared, custom_is_static, custom_pkg_config_libraries);

            makefile << "\tlink_time_flags := " << custom_link_time_flags;
            for (const auto& library_path : custom_library_paths) {
                makefile << " $(library_path_flag)" << library_path;
            }
            makefile << '\n';

            makefile << "\tlibraries :=";
            for (const auto& library : custom_libraries) {
                makefile << " $(library_flag)" << library;
            }
            if (!custom_pkg_config_libraries.empty()) {
                makefile << " `pkg-config $(pkg_config_syntax) --libs";
                for (const auto& pkg_config_library : custom_pkg_config_libraries) {
                    makefile << ' ' << pkg_config_library;
                }
                makefile << '`';
            }
            makefile << '\n';

            if (custom_options_table.contains("static-libraries")) {
                auto custom_static_libraries = toml::find<std::vector<std::string>>(custom_options_table, "static-libraries");
                makefile << "\tstatic_libraries :=";
                for (const auto& static_library : custom_static_libraries) {
                    makefile << ' ' << static_library;
                }
                makefile << '\n';
            }

            if (!custom_install_path.empty()) {
                makefile << "\tprefix := " << custom_install_path << '\n';
            }

            makefile << "endif\n";
        }
    }

    makefile << "\nall: " << output_path << "$(out_ext)\n";
    makefile << ".PHONY: all\n";

    std::vector<std::filesystem::path> object_paths;
    bool has_cpp = false;
    for (std::filesystem::path source_path : source_paths) {
        for (std::filesystem::directory_entry entry :
            std::filesystem::directory_iterator(source_path)) {
            if (SourceFileType file_type; entry.is_regular_file() && (file_type = get_source_file_type(entry.path())) != SOURCE_FILE_NONE) {
                std::filesystem::path object_path;
                for (unsigned int i = 0;; ++i) {
                    object_path = (std::filesystem::path(artifact_path) / (entry.path().stem().string() + '_' + std::to_string(i)));
                    if (std::find(object_paths.begin(), object_paths.end(), object_path) != object_paths.end()) {
                        continue;
                    } else {
                        object_paths.push_back(object_path);
                        break;
                    }
                }
                makefile << '\n'
                         << object_path.generic_string() << "$(obj_ext): " << entry.path().generic_string();

                std::vector<std::filesystem::path> dependencies;
                find_dependencies(entry.path(), include_paths, dependencies);
                for (const auto& depdendency : dependencies) {
                    makefile << ' ' << depdendency.generic_string();
                }

                makefile << "\n\t" << echo("Compiling $@ from $<...") << '\n';
                makefile << "\t@mkdir -p " << artifact_path << '\n';
                if (file_type == SOURCE_FILE_CPP) {
                    makefile << "\t@\"$(cpp_compiler)\" $(compile_only_flag) $< $(cpp_compilation_flags) $(obj_path_flag)$@\n";
                    has_cpp = true;
                } else {
                    makefile << "\t@\"$(c_compiler)\" $(compile_only_flag) $< $(c_compilation_flags) $(obj_path_flag)$@\n";
                }
                makefile << '\t' << echo("Finished compiling $@ from $<!") << '\n';
            }
        }
    }

    makefile << '\n'
             << output_path << "$(out_ext):";
    for (const auto& object_path : object_paths) {
        makefile << ' ' << object_path.generic_string() << "$(obj_ext)";
    }
    makefile << " $(static_libraries)";
    makefile << "\n\t" << echo("Building $@...");
    {
        auto path = std::filesystem::path(output_path);
        if (path.has_parent_path()) {
            makefile << "\n\t@mkdir -p " << path.parent_path().generic_string();
        }
    }
    if (has_cpp) {
        makefile << "\n\t@\"$(cpp_compiler)\" $^ $(cpp_compilation_flags) $(out_path_flag)$@ $(link_flag) $(link_time_flags) $(libraries)\n\t" << echo("Finished building $@!") << '\n';
    } else {
        makefile << "\n\t@\"$(c_compiler)\" $^ $(c_compilation_flags) $(out_path_flag)$@ $(link_flag) $(link_time_flags) $(libraries)\n\t" << echo("Finished building $@!") << '\n';
    }

    makefile << "\nclean:";
    for (const auto& clean_prelude : clean_preludes) {
        makefile << "\n\t" << echo("Executing clean prelude: " + clean_prelude);
        makefile << "\n\t@" << clean_prelude;
    }
    makefile << "\n\t" << echo("Deleting " + output_path + "$(out_ext) and " + artifact_path + "...") << '\n';
    makefile << "\t@rm -rf " << output_path << "$(out_ext) " << artifact_path << '\n';
    makefile << '\t' << echo("Finished deleting " + output_path + "$(out_ext) and " + artifact_path + '!') << '\n';
    makefile << ".PHONY: clean\n";

    makefile << "\ninstall:\n";
    makefile << '\t' << echo("Copying " + output_path + "$(out_ext) to $(prefix)...") << '\n';
    makefile << "\t@cp " << output_path << "$(out_ext) $(prefix)\n";
    makefile << '\t' << echo("Finished copying " + output_path + "$(out_ext) to $(prefix)!") << '\n';
    makefile << ".PHONY: install\n";

    makefile.close();
    std::cout << log("Finished converting Polybuild.toml to makefile!") << std::endl;

    std::cout << log("Producing makefile wrapper...") << std::endl;
    std::ofstream wrapper("Makefile");
    wrapper << "# This file was auto-generated by Polybuild\n\n";

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

    wrapper.close();
    std::cout << log("Finished producing makefile wrapper!") << std::endl;

    return 0;
}
