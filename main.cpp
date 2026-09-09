#include "build.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

struct Arguments {
    std::string command = "generate";
    std::filesystem::path directory;
    std::filesystem::path config = "Polybuild.toml";
    bool help = false;
    bool version = false;
};

void print_help() {
    std::cout << "Usage: polybuild [command] [options]\n\n"
                 "Commands:\n"
                 "  generate       Generate Makefile and .polybuild.mk (default)\n"
                 "  init           Create a minimal Polybuild.toml\n"
                 "  check          Validate configuration and discover sources without writing\n\n"
                 "Options:\n"
                 "  -C <directory> Change directory before running the command\n"
                 "  --config <file> Use a different configuration file\n"
                 "  -h, --help     Show this help\n"
                 "  --version      Show the version\n\n"
                 "Build with Make: make MODE=debug\n";
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments ret;
    bool has_command = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (++i == argc || std::string(argv[i]).empty()) {
                throw std::invalid_argument("Missing value for " + arg);
            }
            return argv[i];
        };

        if (arg == "-h" || arg == "--help") {
            ret.help = true;
        } else if (arg == "--version") {
            ret.version = true;
        } else if (arg == "-C") {
            auto directory = std::filesystem::path(value());
            ret.directory = ret.directory / directory;
        } else if (arg == "--config") {
            ret.config = value();
        } else if (!arg.empty() && arg[0] == '-') {
            throw std::invalid_argument("Unknown option: " + arg);
        } else if (!has_command) {
            ret.command = arg;
            has_command = true;
        } else {
            throw std::invalid_argument("Unexpected argument: " + arg);
        }
    }

    if (ret.command != "generate" && ret.command != "init" && ret.command != "check") {
        throw std::invalid_argument("Unknown command: " + ret.command);
    }
    return ret;
}

bool write_changed(const std::filesystem::path& path, const std::string& contents) {
    std::ifstream existing(path);
    if (existing && std::string(std::istreambuf_iterator<char>(existing), {}) == contents) {
        return false;
    }
    existing.close();

    auto temporary_directory = path;
    temporary_directory += ".tmp";
    auto temporary_path = temporary_directory / path.filename();
    if (!std::filesystem::create_directory(temporary_directory)) {
        throw std::runtime_error("Temporary path already exists: " + temporary_directory.string());
    }

    try {
        std::ofstream file;
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.open(temporary_path, std::ios::out | std::ios::trunc);
        file << contents;
        file.close();
        std::filesystem::rename(temporary_path, path);
        std::filesystem::remove(temporary_directory);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary_path, error);
        std::filesystem::remove(temporary_directory, error);
        throw;
    }
    return true;
}

int main(int argc, char** argv) {
    try {
        auto args = parse_arguments(argc, argv);
        if (args.help) {
            print_help();
            return 0;
        }
        if (args.version) {
            std::cout << "Polybuild\n";
            return 0;
        }

        if (!args.directory.empty()) {
            std::filesystem::current_path(args.directory);
        }

        if (args.command == "init") {
            if (std::filesystem::exists(args.config)) {
                throw std::runtime_error("Refusing to overwrite " + args.config.string());
            }

            write_changed(args.config, "# Paths are relative to the project directory.\n[paths]\noutput = \"app\"\nsource = [\".\"]\n# Object files are stored in this directory.\nartifact = \"obj\"\n\n[options]\n# Uses CC/CXX and CFLAGS/CXXFLAGS from Make by default.\n");
            std::cout << "Created " << args.config.string() << '\n';
            return 0;
        }

        auto files = generate(args.config);
        if (args.command == "check") {
            std::cout << args.config.string() << ": configuration and source discovery passed.\n";
            return 0;
        }

        bool makefile_changed = write_changed(".polybuild.mk", files.makefile);
        bool wrapper_changed = write_changed("Makefile", files.wrapper);
        std::cout << (makefile_changed || wrapper_changed ? "Generated Makefile and .polybuild.mk.\n" : "Makefiles are unchanged.\n") << std::flush;
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "polybuild: " << error.what() << "\nRun 'polybuild --help' for usage.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "polybuild: " << error.what() << '\n';
        return 1;
    }
}
