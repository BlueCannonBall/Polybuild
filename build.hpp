#pragma once

#include <filesystem>
#include <string>

struct GeneratedFiles {
    std::string wrapper;
    std::string makefile;
};

GeneratedFiles generate(const std::filesystem::path& config_path);
