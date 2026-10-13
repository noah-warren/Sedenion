#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "interpreter.h"

namespace {

const char* kVersion = "0.1.0";

#ifdef _WIN32
// Reads one line of console input as UTF-8 (std::getline would see the console code page).
bool readLine(std::string& line) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(in, &mode)) return static_cast<bool>(std::getline(std::cin, line));
    std::wstring w;
    wchar_t buf[512];
    for (;;) {
        DWORD n = 0;
        if (!ReadConsoleW(in, buf, 512, &n, nullptr) || n == 0) return !w.empty();
        w.append(buf, n);
        if (!w.empty() && w.back() == L'\n') break;
    }
    while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) w.pop_back();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    line.assign(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &line[0], len, nullptr, nullptr);
    return true;
}
#else
bool readLine(std::string& line) { return static_cast<bool>(std::getline(std::cin, line)); }
#endif

int repl(Interpreter& interp) {
    std::cout << "Sedenion " << kVersion << " - a language for mathematics. Type help() or quit.\n";
    std::string buffer, line;
    for (;;) {
        std::cout << (buffer.empty() ? "> " : ". ") << std::flush;
        if (!readLine(line)) break;
        if (buffer.empty() && (line == "quit" || line == "exit")) break;
        if (!buffer.empty() && line.empty()) {
            // An empty line ends an unfinished block so its error is shown.
            interp.run(buffer, "<repl>");
            buffer.clear();
            continue;
        }
        buffer += line + "\n";
        RunStatus st = interp.run(buffer, "<repl>", true);
        if (st == RunStatus::Incomplete) continue;
        buffer.clear();
    }
    std::cout << "\n";
    return 0;
}

int usage() {
    std::cerr << "usage: sedenion [file.sed] [-e code] [--disasm file.sed] [--version]\n";
    return 2;
}

int finish(RunStatus st) { return st == RunStatus::Ok ? 0 : (st == RunStatus::RuntimeError ? 70 : 65); }

}  // namespace

std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (w) {
        for (int i = 0; i < n; i++) {
            int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
            if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
            args.push_back(s);
        }
        LocalFree(w);
        return args;
    }
#endif
    for (int i = 0; i < argc; i++) args.push_back(argv[i]);
    return args;
}

int main(int rawArgc, char** rawArgv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::ios::sync_with_stdio(false);
    std::vector<std::string> argv = utf8Args(rawArgc, rawArgv);
    int argc = static_cast<int>(argv.size());
    Interpreter interp;
    auto setArgv = [&](int from) {
        std::vector<Value> items;
        for (int i = from; i < argc; i++) items.push_back(newString(argv[static_cast<size_t>(i)]));
        interp.vm().setGlobal("argv", newList(std::move(items)));
    };
    if (argc == 1) return repl(interp);

    std::string a1 = argv[1];
    if (a1 == "--version" || a1 == "-v") {
        std::cout << "Sedenion " << kVersion << "\n";
        return 0;
    }
    if (a1 == "-h" || a1 == "--help") {
        usage();
        return 0;
    }
    if (a1 == "-e") {
        if (argc < 3) return usage();
        setArgv(3);
        return finish(interp.run(argv[2], "<-e>"));
    }
    if (a1 == "--disasm") {
        if (argc < 3) return usage();
        interp.disassembleOnly = true;
        a1 = argv[2];
    }
    setArgv(1);
    std::string source;
    if (!readSourceFile(a1, source)) {
        std::cerr << "error: cannot open '" << a1 << "'\n";
        return 66;
    }
    return finish(interp.run(source, a1));
}
