#include "toolchain.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>

#include "subprocess.h"
#include "sha256.h"
#include "texts.h"

namespace {

enum class Packaging { Archive, Zip, SelfExtracting, Single };

struct Package {
    const char* program;
    const char* name;
    const char* url;
    const char* sha256;
    const char* file;
    Packaging packaging;
    const char* folder;
    const char* bin;
    const char* mirror = nullptr;
    bool merge = false;
    const char* home = nullptr;
};

#ifdef _WIN32
constexpr Package kPackages[] = {
    {"nodtool", "nodtool 1.4.4", "https://github.com/encounter/nod/releases/download/v1.4.4/nodtool-windows-x86_64.exe",
     "fb3203a68a59fa19ba9de0aa2f8c339396b6e4c5f9274d0858d302074cdedf8b", "nodtool.exe", Packaging::Single, "nodtool", "nodtool"},
    {"python", "Python 3.12.10", "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip",
     "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3", "python-3.12.10-embed-amd64.zip", Packaging::Archive, "python", "python"},
    {"cmake", "CMake 4.4.3", "https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-windows-x86_64.zip",
     "4d52ebab7193a698651639ed80d8d04fd903358843572cf44c7fd234cb7c26ab", "cmake-4.4.3-windows-x86_64.zip", Packaging::Archive, "cmake",
     "cmake/cmake-4.4.3-windows-x86_64/bin"},
    {"ninja", "Ninja 1.13.2", "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip",
     "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65", "ninja-win.zip", Packaging::Archive, "ninja", "ninja"},
    {"g++", "MinGW-w64 GCC 15.2 (nuwen.net 20.0)", "https://nuwen.net/files/mingw/mingw-20.0.exe",
     "bce8bdaa095848561488d34fd80371a1f36eb0a590202eb534f32e026e5cd8ef", "mingw-20.0.exe", Packaging::SelfExtracting, "mingw", "mingw/MinGW/bin"},
    {"java", "Eclipse Temurin JDK 21.0.13+9", "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_x64_windows_hotspot_21.0.13_9.zip",
     "a4dbaa05fca4bd89d84584e58c459bd732efebb1722af80ab13c6516832004f9", "OpenJDK21U-jdk_x64_windows_hotspot_21.0.13_9.zip",
     Packaging::Zip, "java", "java/jdk-21.0.13+9/bin", nullptr, false, "java/jdk-21.0.13+9"},
    {"ghidra", "Ghidra 12.1.4", "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip",
     "ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db", "ghidra_12.1.4_PUBLIC_20260921.zip",
     Packaging::Zip, "ghidra", nullptr},
    {"ghidra-loader", "GameCube Loader 1.3.1", "https://github.com/Cuyler36/Ghidra-GameCube-Loader/releases/download/1.3.1/GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     "9892f28fc1e7f19bb3fcec7cfb4ad2ab7504503b571d93de8b142a6f7bec8ed0", "GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     Packaging::Zip, "ghidra/ghidra_12.1.4_PUBLIC/Ghidra/Extensions", nullptr, nullptr, true},
};
constexpr const char* kPython = "python";
constexpr const char* kCompiler = "g++";
#elif defined(__linux__) && defined(__x86_64__)
constexpr Package kPackages[] = {
    {"nodtool", "nodtool 1.4.4", "https://github.com/encounter/nod/releases/download/v1.4.4/nodtool-linux-x86_64",
     "cf20828d1a437ca14ed7acfd5545e1a0e62c082f5a77240a399aa4f172119fc4", "nodtool-linux-x86_64", Packaging::Single, "nodtool", "nodtool"},
    {"java", "Eclipse Temurin JDK 21.0.13+9", "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz",
     "d7f5d5cf865a2902b70ff549b97586a7d3f234e016f041ab447ea740c7d66d1c", "OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz",
     Packaging::Archive, "java", "java/jdk-21.0.13+9/bin", nullptr, false, "java/jdk-21.0.13+9"},
    {"ghidra", "Ghidra 12.1.4", "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip",
     "ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db", "ghidra_12.1.4_PUBLIC_20260921.zip",
     Packaging::Zip, "ghidra", nullptr},
    {"ghidra-loader", "GameCube Loader 1.3.1", "https://github.com/Cuyler36/Ghidra-GameCube-Loader/releases/download/1.3.1/GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     "9892f28fc1e7f19bb3fcec7cfb4ad2ab7504503b571d93de8b142a6f7bec8ed0", "GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     Packaging::Zip, "ghidra/ghidra_12.1.4_PUBLIC/Ghidra/Extensions", nullptr, nullptr, true},
};
constexpr const char* kPython = "python3";
constexpr const char* kCompiler = "c++";
#elif defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
constexpr Package kPackages[] = {
    {"nodtool", "nodtool 2.0.0-alpha.12", "https://github.com/encounter/nod/releases/download/v2.0.0-alpha.12/nodtool-macos-arm64",
     "524c0b27c0c4289e435df4474d288ea7e63d6a34814cc7c650231688dd1e53b4", "nodtool-macos-arm64", Packaging::Single, "nodtool", "nodtool"},
    {"python3", "Python 3.11.17", "https://github.com/astral-sh/python-build-standalone/releases/download/20261021/cpython-3.11.17%2B20261021-aarch64-apple-darwin-install_only.tar.gz",
     "b5199d66966141fb6d5447512d2b2fe0398d417da37b1949f58db3792d314eac", "cpython-3.11.17+20261021-aarch64-apple-darwin-install_only.tar.gz",
     Packaging::Archive, "python", "python/python/bin"},
    {"cmake", "CMake 4.4.3", "https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-macos-universal.tar.gz",
     "0c5d65251c14cc884bfa16bdbed3c263ce5bffe2e21c0d0d00962cb0610464fa", "cmake-4.4.3-macos-universal.tar.gz", Packaging::Archive,
     "cmake", "cmake/cmake-4.4.3-macos-universal/CMake.app/Contents/bin"},
    {"ninja", "Ninja 1.13.2", "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip",
     "c99048673aa765960a99cf10c6ddb9f1fad506099ff0a0e137ad8960a88f321b", "ninja-mac.zip", Packaging::Archive, "ninja", "ninja"},
    {"java", "Eclipse Temurin JDK 21.0.13+9", "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.13_9.tar.gz",
     "bf036b35b071bfb2f54f2c0af8ee8d88afa95ee6eb3dce02c0f94cfc2524d04a", "OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.13_9.tar.gz",
     Packaging::Archive, "java", "java/jdk-21.0.13+9/Contents/Home/bin", nullptr, false, "java/jdk-21.0.13+9/Contents/Home"},
    {"ghidra", "Ghidra 12.1.4", "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip",
     "ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db", "ghidra_12.1.4_PUBLIC_20260921.zip",
     Packaging::Zip, "ghidra", nullptr},
    {"ghidra-loader", "GameCube Loader 1.3.1", "https://github.com/Cuyler36/Ghidra-GameCube-Loader/releases/download/1.3.1/GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     "9892f28fc1e7f19bb3fcec7cfb4ad2ab7504503b571d93de8b142a6f7bec8ed0", "GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     Packaging::Zip, "ghidra/ghidra_12.1.4_PUBLIC/Ghidra/Extensions", nullptr, nullptr, true},
};
constexpr const char* kPython = "python3";
constexpr const char* kCompiler = "c++";
#elif defined(__APPLE__) && defined(__x86_64__)
constexpr Package kPackages[] = {
    {"nodtool", "nodtool 2.0.0-alpha.12", "https://github.com/encounter/nod/releases/download/v2.0.0-alpha.12/nodtool-macos-x86_64",
     "d1e7ba904c2e557fafbd3bf2d9b75a7901a2bf1568c0fb3a505fca0f30b0a833", "nodtool-macos-x86_64", Packaging::Single, "nodtool", "nodtool"},
    {"python3", "Python 3.11.17", "https://github.com/astral-sh/python-build-standalone/releases/download/20261021/cpython-3.11.17%2B20261021-x86_64-apple-darwin-install_only.tar.gz",
     "c647c272312c03301e399f0e0fdb2b876fed213ee6985cd2d232d588a316483f", "cpython-3.11.17+20261021-x86_64-apple-darwin-install_only.tar.gz",
     Packaging::Archive, "python", "python/python/bin"},
    {"cmake", "CMake 4.4.3", "https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-macos-universal.tar.gz",
     "0c5d65251c14cc884bfa16bdbed3c263ce5bffe2e21c0d0d00962cb0610464fa", "cmake-4.4.3-macos-universal.tar.gz", Packaging::Archive,
     "cmake", "cmake/cmake-4.4.3-macos-universal/CMake.app/Contents/bin"},
    {"ninja", "Ninja 1.13.2", "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip",
     "c99048673aa765960a99cf10c6ddb9f1fad506099ff0a0e137ad8960a88f321b", "ninja-mac.zip", Packaging::Archive, "ninja", "ninja"},
    {"java", "Eclipse Temurin JDK 21.0.13+9", "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_x64_mac_hotspot_21.0.13_9.tar.gz",
     "9aa1d1c17ae5467a022679e7e0af5c00797d94ac462ed1a46f4db07a8a34a6dd", "OpenJDK21U-jdk_x64_mac_hotspot_21.0.13_9.tar.gz",
     Packaging::Archive, "java", "java/jdk-21.0.13+9/Contents/Home/bin", nullptr, false, "java/jdk-21.0.13+9/Contents/Home"},
    {"ghidra", "Ghidra 12.1.4", "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip",
     "ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db", "ghidra_12.1.4_PUBLIC_20260921.zip",
     Packaging::Zip, "ghidra", nullptr},
    {"ghidra-loader", "GameCube Loader 1.3.1", "https://github.com/Cuyler36/Ghidra-GameCube-Loader/releases/download/1.3.1/GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     "9892f28fc1e7f19bb3fcec7cfb4ad2ab7504503b571d93de8b142a6f7bec8ed0", "GameCubeLoader-1.3.0-921504c-Ghidra_12.1.zip",
     Packaging::Zip, "ghidra/ghidra_12.1.4_PUBLIC/Ghidra/Extensions", nullptr, nullptr, true},
};
constexpr const char* kPython = "python3";
constexpr const char* kCompiler = "c++";
#else
constexpr Package kPackages[] = {{nullptr, nullptr, nullptr, nullptr, nullptr, Packaging::Single, nullptr, nullptr}};
constexpr const char* kPython = "python3";
constexpr const char* kCompiler = "c++";
#endif

struct Requirement {
    const char* program;
    const char* argument;
    const char* pattern;
    int major;
    int minor;
};

struct Requirement {
    const char* program;
    const char* argument;
    const char* pattern;
    int major;
    int minor;
};

const Package* package_for(const std::string& program) {
    for (const Package& package : kPackages) {
        if (package.program && program == package.program) {
            return &package;
        }
    }
    return nullptr;
}

std::string system_program(const char* name) {
#ifdef _WIN32
    std::string root = environment_value("SystemRoot");
    return utf8_of(path_from(root.empty() ? "C:/Windows" : root) / "System32" / (std::string(name) + ".exe"));
#else
    return name;
#endif
}

}

Toolchain::Toolchain(std::filesystem::path folder, std::filesystem::path bundled) : folder_(std::move(folder)), bundled_(std::move(bundled)) {}

void Toolchain::add_to_path() const {
    std::vector<std::filesystem::path> folders;
    for (const std::filesystem::path& base : {bundled_, folder_}) {
        for (const Package& package : kPackages) {
            if (package.bin) {
                folders.push_back(base / path_from(package.bin));
            }
        }
    }

    std::filesystem::path java_home;
    for (const std::filesystem::path& base : {folder_, bundled_}) {
        for (const Package& package : kPackages) {
            if (package.program && std::strcmp(package.program, "java") == 0 && package.home) {
                std::filesystem::path candidate = base / path_from(package.home);
#ifdef _WIN32
                candidate /= "bin";
                candidate /= "java.exe";
#else
                candidate /= "bin";
                candidate /= "java";
#endif
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate, error)) {
                    java_home = base / path_from(package.home);
                    break;
                }
            }
        }
        if (!java_home.empty()) break;
    }
    if (!java_home.empty()) {
#ifdef _WIN32
        _putenv_s("JAVA_HOME", utf8_of(java_home).c_str());
#else
        setenv("JAVA_HOME", utf8_of(java_home).c_str(), 1);
#endif
    } else {
        std::string external_java = environment_value("JAVA_HOME");
        if (!external_java.empty()) folders.push_back(path_from(external_java) / "bin");
    }

    std::filesystem::path ghidra_home;
    for (const std::filesystem::path& base : {folder_, bundled_}) {
        std::filesystem::path candidate = base / "ghidra" / "ghidra_12.1.4_PUBLIC";
#ifdef _WIN32
        std::filesystem::path headless = candidate / "support" / "analyzeHeadless.bat";
#else
        std::filesystem::path headless = candidate / "support" / "analyzeHeadless";
#endif
        std::error_code error;
        if (std::filesystem::is_regular_file(headless, error)) {
            ghidra_home = candidate;
            break;
        }
    }
    if (!ghidra_home.empty()) {
#ifdef _WIN32
        _putenv_s("GHIDRA_HOME", utf8_of(ghidra_home).c_str());
#else
        setenv("GHIDRA_HOME", utf8_of(ghidra_home).c_str(), 1);
#endif
    }
    prepend_path(folders);
}

std::string Toolchain::python() const {
    return kPython;
}

#ifdef _WIN32
constexpr int kCompilerMajor = 13;
#else
constexpr int kCompilerMajor = 10;
#endif

std::vector<ToolStatus> Toolchain::inspect() const {
    const Requirement requirements[] = {
        {"nodtool", "--version", R"(nodtool)", 0, 0},
        {kPython, "--version", R"(Python (\d+)\.(\d+))", 3, 11},
        {"cmake", "--version", R"(cmake version (\d+)\.(\d+))", 3, 20},
        {"ninja", "--version", R"((\d+)\.(\d+))", 1, 10},
#ifdef __APPLE__
        // Apple Clang rejects -dumpfullversion when invoked without an input file.
        // Parse the installed compiler's version banner instead.
        {kCompiler, "--version", R"(clang version (\d+)\.(\d+))", kCompilerMajor, 0},
#else
        {kCompiler, "-dumpfullversion", R"((\d+)\.(\d+))", kCompilerMajor, 0},
#endif
        {"java", "-version", R"((?:openjdk|java) version "?(\d+)\.(\d+))", 21, 0},
        {"ghidra", "", "", 0, 0},
        {"ghidra-loader", "", "", 0, 0},
    };
    std::vector<ToolStatus> tools;
    for (const Requirement& requirement : requirements) {
        ToolStatus status;
        status.program = requirement.program;
        if (status.program == "ghidra" || status.program == "ghidra-loader") {
            std::vector<std::filesystem::path> homes = {
                folder_ / "ghidra" / "ghidra_12.1.4_PUBLIC",
                bundled_ / "ghidra" / "ghidra_12.1.4_PUBLIC"
            };
            std::string configured = environment_value("GHIDRA_HOME");
            if (!configured.empty()) homes.push_back(path_from(configured));
            for (const std::filesystem::path& home : homes) {
#ifdef _WIN32
                std::filesystem::path headless = home / "support" / "analyzeHeadless.bat";
#else
                std::filesystem::path headless = home / "support" / "analyzeHeadless";
#endif
                std::error_code error;
                if (!std::filesystem::is_regular_file(headless, error)) continue;
                bool found = status.program == "ghidra";
                if (found) {
                    status.version = "Ghidra 12.1.4";
                } else {
                    std::filesystem::path extensions = home / "Ghidra" / "Extensions";
                    for (std::filesystem::recursive_directory_iterator it(extensions, error), end;
                         !error && it != end; it.increment(error)) {
                        if (!it->is_regular_file(error) || it->path().filename() != "extension.properties") continue;
                        std::ifstream properties(it->path());
                        std::string line;
                        while (std::getline(properties, line)) {
                            if (line.find("Nintendo GameCube Binary Loader for Ghidra") != std::string::npos) {
                                found = true;
                                status.version = "GameCube Loader 1.3.1";
                                break;
                            }
                        }
                        if (found) break;
                    }
                }
                if (found) {
                    status.usable = true;
                    status.bundled = (home.string().rfind(bundled_.string(), 0) == 0);
                    break;
                }
            }
            tools.push_back(status);
            continue;
        }
        std::string path = find_program(status.program);
        std::error_code outside;
        std::filesystem::path relative = std::filesystem::relative(path_from(path), bundled_, outside);
        status.bundled = !path.empty() && !outside && !relative.empty() && relative.native()[0] != '.';
        std::smatch match;
        std::string text = path.empty() ? std::string() : capture(path, {requirement.argument});
        if (!text.empty() && std::regex_search(text, match, std::regex(requirement.pattern))) {
            status.version = match.str(0);
            int major = match.size() > 2 ? std::atoi(match.str(1).c_str()) : 0;
            int minor = match.size() > 2 ? std::atoi(match.str(2).c_str()) : 0;
            status.usable = major > requirement.major || (major == requirement.major && minor >= requirement.minor);
#ifdef _WIN32
            if (status.usable && std::strcmp(requirement.program, kCompiler) == 0) {
                status.usable = capture(path, {"-dumpmachine"}).find("mingw32") != std::string::npos;
            }
#endif
        }
        tools.push_back(status);
    }
    return tools;
}

std::vector<Step> Toolchain::preparation(const std::vector<ToolStatus>& tools, bool offline) const {
    const Texts& t = texts();
    std::vector<Step> steps;
    std::filesystem::path downloads = folder_ / "downloads";
    for (const ToolStatus& tool : tools) {
        if (tool.usable) {
            continue;
        }
        const Package* package = package_for(tool.program);
        if (!package || offline) {
            std::string program = tool.program;
            Step step;
            step.title = format(t.step_tool, program);
            step.action = [program, offline](std::string& message) {
                message = format(offline ? texts().tool_offline : texts().tool_unavailable, program);
                return false;
            };
            steps.push_back(step);
            continue;
        }
        std::filesystem::path archive = downloads / package->file;
        std::filesystem::path destination = folder_ / package->folder;
        std::string name = package->name;
        std::string expected = package->sha256;
        Step download;
        download.title = format(t.step_download, name);
        download.weight = 2.0f;
        download.action = [archive, program = system_program("curl"), url = std::string(package->url),
                           mirror = std::string(package->mirror ? package->mirror : ""), name](std::string& message) {
            std::error_code error;
            std::filesystem::create_directories(archive.parent_path(), error);
            std::vector<std::string> sources;
            if (!mirror.empty()) {
                sources.push_back(mirror);
            }
            sources.push_back(url);
            std::string last;
            for (const std::string& source : sources) {
                Process curl;
                std::string failure;
                if (!curl.start(program, {"-L", "--fail", "--silent", "--show-error", "--connect-timeout", "20", "--speed-limit", "50000", "--speed-time", "30",
                                          "--retry", "2", "-o", utf8_of(archive), source},
                                {}, false, failure)) {
                    last = failure;
                    continue;
                }
                std::string output;
                char buffer[512];
                for (size_t count; (count = curl.read(buffer, sizeof(buffer))) > 0;) {
                    output.append(buffer, count);
                }
                if (curl.wait() == 0) {
                    return true;
                }
                last = output;
                std::filesystem::remove(archive, error);
            }
            message = format(texts().step_download, name) + ": " + last;
            return false;
        };
        steps.push_back(download);
        Step verify;
        verify.title = format(t.step_verify, name);
        verify.action = [archive, expected, destination, merge = package->merge](std::string& message) {
            std::error_code error;
            if (sha256_of(archive) != expected) {
                std::filesystem::remove(archive, error);
                message = format(texts().hash_mismatch, utf8_of(archive.filename()));
                return false;
            }
            if (!merge) std::filesystem::remove_all(destination, error);
            std::filesystem::create_directories(destination, error);
            return !error;
        };
        steps.push_back(verify);
        Step unpack;
        unpack.title = format(t.step_install, name);
        switch (package->packaging) {
        case Packaging::Archive:
            unpack.program = system_program("tar");
            unpack.arguments = {"-xf", utf8_of(archive), "-C", utf8_of(destination)};
            break;
        case Packaging::Zip:
            unpack.program = kPython;
            unpack.arguments = {"-c", "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])",
                                utf8_of(archive), utf8_of(destination)};
            break;
        case Packaging::SelfExtracting:
            unpack.program = utf8_of(archive);
            unpack.arguments = {"-y", "-o" + utf8_of(destination.lexically_normal().make_preferred())};
            break;
        case Packaging::Single:
            unpack.action = [archive, destination, program = tool.program](std::string&) {
                std::error_code error;
                std::filesystem::path target = destination / program;
#ifdef _WIN32
                target += ".exe";
                return std::filesystem::copy_file(archive, target, std::filesystem::copy_options::overwrite_existing, error);
#else
                if (!std::filesystem::copy_file(archive, target, std::filesystem::copy_options::overwrite_existing, error)) {
                    return false;
                }
                std::filesystem::permissions(target, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                                             std::filesystem::perm_options::add, error);
                return !error;
#endif
            };
            break;
        }
        steps.push_back(unpack);
        Step finish;
        finish.title = format(t.step_finish, name);
        finish.action = [archive, destination](std::string&) {
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(destination, error)) {
                if (entry.path().extension() == "._pth") {
                    std::filesystem::remove(entry.path(), error);
                }
            }
            std::filesystem::remove(archive, error);
            return true;
        };
        steps.push_back(finish);
    }
    Step activate;
    activate.title = "Activate managed Java and Ghidra";
    activate.action = [this](std::string&) {
        add_to_path();
        return true;
    };
    steps.push_back(std::move(activate));
    return steps;
}
