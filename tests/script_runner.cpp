// Runs every tests/scripts/*.sed file and checks its output against the expectations
// written in the script itself:
//   # expect: <text>          the next output line must equal <text>
//   # expect-error: <text>    the script must fail with an error message containing <text>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "interpreter.h"

namespace fs = std::filesystem;

namespace {

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) lines.push_back(trimRight(line));
    return lines;
}

bool runScript(const fs::path& path, std::string& report) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream buf;
    buf << f.rdbuf();
    std::string source = buf.str();
    if (source.rfind("\xEF\xBB\xBF", 0) == 0) source.erase(0, 3);

    std::vector<std::string> expected, expectedErrors;
    for (const std::string& line : splitLines(source)) {
        size_t p = line.find("# expect: ");
        if (p != std::string::npos) {
            expected.push_back(line.substr(p + 10));
            continue;
        }
        p = line.find("# expect-error: ");
        if (p != std::string::npos) expectedErrors.push_back(line.substr(p + 16));
    }

    std::ostringstream out, err;
    Interpreter interp;
    interp.setOutput(out);
    interp.setErrorStream(err);
    RunStatus status = interp.run(source, path.u8string());

    std::ostringstream r;
    bool ok = true;
    std::vector<std::string> actual = splitLines(out.str());
    size_t n = std::max(actual.size(), expected.size());
    for (size_t i = 0; i < n; i++) {
        std::string a = i < actual.size() ? actual[i] : "<missing>";
        std::string e = i < expected.size() ? expected[i] : "<no expectation>";
        if (a != e) {
            r << "    line " << (i + 1) << ": expected '" << e << "'\n"
              << "             got      '" << a << "'\n";
            ok = false;
        }
    }
    std::string errText = err.str();
    if (expectedErrors.empty()) {
        if (status != RunStatus::Ok) {
            r << "    unexpected error:\n" << errText;
            ok = false;
        }
    } else {
        if (status == RunStatus::Ok) {
            r << "    expected an error, but the script succeeded\n";
            ok = false;
        }
        for (const auto& e : expectedErrors) {
            if (errText.find(e) == std::string::npos) {
                r << "    expected error containing '" << e << "', got:\n" << errText;
                ok = false;
            }
        }
    }
    report = r.str();
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::path("tests/scripts");
    std::string filter = argc > 2 ? argv[2] : "";
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().extension() == ".sed") files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    int passed = 0, failed = 0;
    for (const auto& file : files) {
        if (!filter.empty() && file.filename().string().find(filter) == std::string::npos) continue;
        std::string report;
        bool ok = false;
        try {
            ok = runScript(file, report);
        } catch (const std::exception& e) {
            report = std::string("    crashed: ") + e.what() + "\n";
        }
        std::cout << (ok ? "PASS " : "FAIL ") << file.filename().string() << "\n";
        if (!ok) std::cout << report;
        (ok ? passed : failed)++;
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 && passed > 0 ? 0 : 1;
}
