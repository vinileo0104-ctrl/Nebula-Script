#ifndef MAIN_CPP
#define MAIN_CPP

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../compiler/Error.hpp"
#include "../compiler/Token.hpp"
#include "../compiler/Lexer.hpp"
#include "../compiler/Parser.hpp"
#include "../compiler/BytecodeGenerator.hpp"
#include "../runtime/Vm.hpp"
#include "../common/Version.hpp"

#if __has_include("../compiler/TypeChecker.hpp")
    #include "../compiler/TypeChecker.hpp"
    #define NEBULA_HAS_TYPECHECKER 1
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// ==========================================
// Utilitários
// ==========================================

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string stripExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("\\/");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path;
    return path.substr(0, dot);
}

[[maybe_unused]] static std::string getExecutableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (len == 0) return "";
    std::string full(buffer, len);
    size_t pos = full.find_last_of("\\/");
    return (pos == std::string::npos) ? "" : full.substr(0, pos);
#else
    return "";
#endif
}

static std::vector<std::string> readFileLines(const std::string& filepath) {
    std::vector<std::string> lines;
    std::ifstream file(filepath);
    if (!file.is_open()) return lines;

    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
    return lines;
}

[[maybe_unused]] static bool readBinaryFile(const std::string& path, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    if (size < 0) return false;

    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0 && !file.read(out.data(), size)) return false;
    return true;
}

[[maybe_unused]] static bool fileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.is_open();
}

struct TempFileGuard {
    std::string path;
    explicit TempFileGuard(std::string p) : path(std::move(p)) {}
    ~TempFileGuard() { std::remove(path.c_str()); }
};

static bool loadSource(const std::string& path, ErrorReporter& rep, std::vector<std::string>& lines) {
    lines = readFileLines(path);
    if (lines.empty()) {
        rep.report(ErrorCode::NS_00_FileError,
                   "Não foi possível abrir ou o ficheiro está vazio: " + path);
        return false;
    }
    rep.setSource(lines);
    return true;
}

[[maybe_unused]] static bool validateExtension(const std::string& path, const std::string& ext, ErrorReporter& rep) {
    if (endsWith(path, ext)) return true;
    rep.report(ErrorCode::NS_00_FileError, "Esperado um ficheiro " + ext + ", recebido: " + path);
    return false;
}

// ==========================================
// Compilação de Múltiplos Ficheiros
// ==========================================
static bool compileMultipleSources(const std::vector<std::string>& sourcePaths,
                                   ErrorReporter& rep, Chunk& outChunk) {
    std::vector<std::unique_ptr<Stmt>> combinedAst;

    for (const std::string& path : sourcePaths) {
        if (!validateExtension(path, ".ns", rep)) return false;

        std::vector<std::string> src;
        if (!loadSource(path, rep, src)) return false;

        Lexer lexer(src, rep);
        std::vector<Token> tokens = lexer.scanTokens();
        if (rep.hadError()) return false;

        Parser parser(tokens, rep);
        auto ast = parser.parse();
        if (rep.hadError()) return false;

        for (auto& stmt : ast) {
            combinedAst.push_back(std::move(stmt));
        }
    }

#ifdef NEBULA_HAS_TYPECHECKER
    TypeChecker checker(rep);
    checker.check(combinedAst);
    if (rep.hadError()) return false;
#endif

    BytecodeGenerator generator(std::move(combinedAst), rep);
    outChunk = generator.generate();
    return !rep.hadError();
}

// ==========================================
// CLI
// ==========================================

static void printHelp(const char* programName) {
    std::cout << "Nebula Script Compiler (v0.1.0)\n\n";
    std::cout << "Uso:\n";
    std::cout << "  " << programName << " -check <ficheiro.ns>                          Verifica a sintaxe e erros\n";
    std::cout << "  " << programName << " -nbc <f1.ns> [f2.ns ...] [-o <saida.cns>]   Compila para bytecode\n";
    std::cout << "  " << programName << " -run <ficheiro.cns>                           Executa o ficheiro binário compilado\n";
    std::cout << "  " << programName << " -build <f1.ns> [f2.ns ...] [-o <saida.exe>] [stub.exe]\n";
    std::cout << "                                                                      Gera um .exe autónomo\n";
    std::cout << "  " << programName << " -version                                      Mostra a versão atual\n";
}

static int cmdCheck(const std::string& path, ErrorReporter& rep) {
    std::cout << "[Nebula] A analisar sintaxe de: " << path << "...\n";
    std::vector<std::string> src;
    if (!validateExtension(path, ".ns", rep) || !loadSource(path, rep, src)) return 1;
    
    Lexer lexer(src, rep);
    std::vector<Token> tokens = lexer.scanTokens();
    if (rep.hadError()) return 1;

    Parser parser(tokens, rep);
    auto ast = parser.parse();
    if (rep.hadError()) return 1;

#ifdef NEBULA_HAS_TYPECHECKER
    TypeChecker checker(rep);
    checker.check(ast);
    if (rep.hadError()) return 1;
#endif

    std::cout << "[Nebula] Sintaxe verificada com sucesso! Nenhum erro encontrado.\n";
    return 0;
}

struct CompileOptions {
    std::vector<std::string> sourcePaths;
    std::string outputPath;
    std::string stubPath;
};

static bool parseCompileOptions(int argc, char* argv[], bool build,
                                CompileOptions& options, ErrorReporter& rep) {
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-o") {
            if (i + 1 >= argc || std::string(argv[i + 1]).empty()) {
                rep.report(ErrorCode::NS_06_InvalidFlag,
                           "A flag -o exige um caminho de saída.");
                return false;
            }
            if (!options.outputPath.empty()) {
                rep.report(ErrorCode::NS_06_InvalidFlag,
                           "A flag -o só pode ser usada uma vez.");
                return false;
            }
            options.outputPath = argv[++i];
        } else if (build && endsWith(argument, ".exe")) {
            if (!options.stubPath.empty()) {
                rep.report(ErrorCode::NS_06_InvalidFlag,
                           "Só é possível especificar um ficheiro stub.");
                return false;
            }
            options.stubPath = argument;
        } else if (!argument.empty() && argument[0] == '-') {
            rep.report(ErrorCode::NS_06_InvalidFlag,
                       "Opção desconhecida: " + argument);
            return false;
        } else {
            options.sourcePaths.push_back(argument);
        }
    }

    if (options.sourcePaths.empty()) {
        rep.report(ErrorCode::NS_00_FileError,
                   "Nenhum ficheiro .ns especificado para compilação.");
        return false;
    }
    return true;
}

static int cmdNbc(int argc, char* argv[], ErrorReporter& rep) {
    CompileOptions options;
    if (!parseCompileOptions(argc, argv, false, options, rep)) {
        return 1;
    }

    std::cout << "[Nebula] A compilar ficheiros para bytecode...\n";
    Chunk chunk;
    if (!compileMultipleSources(options.sourcePaths, rep, chunk)) return 1;

    const std::string outName = options.outputPath.empty()
        ? stripExtension(options.sourcePaths.front()) + ".cns"
        : options.outputPath;
    if (!chunk.saveToFile(outName)) return 1;
    std::cout << "[Nebula] Compilação concluída com sucesso! Total de instruções: "
              << chunk.code.size() << "\n";
    return 0;
}

static int cmdRun(const std::string& path, ErrorReporter& rep) {
    if (!validateExtension(path, ".cns", rep)) return 1;

    std::cout << "[Nebula] A executar o ficheiro binário: " << path << "...\n";
    VM vm;
    if (!vm.loadFromFile(path)) return 1;
    return vm.run() ? 0 : 1;
}

static long readStubBuild(const std::vector<char>& stub) {
    const std::string needle = "NSBUILD:";
    auto it = std::search(stub.begin(), stub.end(), needle.begin(), needle.end());
    if (it == stub.end()) return -1;

    long value = 0;
    bool anyDigit = false;
    for (it += static_cast<std::ptrdiff_t>(needle.size());
         it != stub.end() && *it >= '0' && *it <= '9'; ++it) {
        value = value * 10 + (*it - '0');
        anyDigit = true;
    }
    return anyDigit ? value : -1;
}

static int cmdBuild(int argc, char* argv[], ErrorReporter& rep) {
    CompileOptions options;
    if (!parseCompileOptions(argc, argv, true, options, rep)) {
        return 1;
    }

    std::cout << "[Nebula] A gerar executável autónomo unificado...\n";

    Chunk chunk;
    if (!compileMultipleSources(options.sourcePaths, rep, chunk)) return 1;

    const std::string baseName = stripExtension(options.sourcePaths.front());
    const std::string outputExe = options.outputPath.empty()
        ? baseName + ".exe" : options.outputPath;
    const std::string tempCns = outputExe + ".tmp.cns";

    TempFileGuard tempGuard(tempCns);
    if (!chunk.saveToFile(tempCns)) return 1;

    std::vector<char> cnsBuffer;
    if (!readBinaryFile(tempCns, cnsBuffer) || cnsBuffer.empty()) {
        rep.report(ErrorCode::NS_00_FileError, "Falha ao gerar o bytecode temporário: " + tempCns);
        return 1;
    }

    std::string stubPath = options.stubPath;
    if (stubPath.empty()) {
        stubPath = "stub.exe";
        if (!fileExists(stubPath)) {
            std::string dir = getExecutableDir();
            if (!dir.empty() && fileExists(dir + "/stub.exe")) {
                stubPath = dir + "/stub.exe";
            }
        }
    }

    std::vector<char> stubBuffer;
    if (!readBinaryFile(stubPath, stubBuffer) || stubBuffer.empty()) {
        rep.report(ErrorCode::NS_00_FileError,
                   "Ficheiro stub não encontrado: " + stubPath +
                   ". Certifica-te de que o stub.exe está disponível!");
        return 1;
    }

    const long stubBuild = readStubBuild(stubBuffer);
    if (stubBuild != NEBULA_BUILD) {
        std::string found = (stubBuild < 0) ? "sem número de build" : "build " + std::to_string(stubBuild);
        rep.report(ErrorCode::NS_00_FileError,
                   "O stub (" + found + ") não corresponde ao compilador (build " NEBULA_BUILD_STR ").");
        return 1;
    }

    std::ofstream finalExe(outputExe, std::ios::binary);
    if (!finalExe.is_open()) {
        rep.report(ErrorCode::NS_00_FileError, "Não foi possível criar o executável: " + outputExe);
        return 1;
    }

    const uint32_t sizeVal = static_cast<uint32_t>(cnsBuffer.size());
    finalExe.write(stubBuffer.data(), static_cast<std::streamsize>(stubBuffer.size()));
    finalExe.write(cnsBuffer.data(), static_cast<std::streamsize>(cnsBuffer.size()));
    finalExe.write(reinterpret_cast<const char*>(&sizeVal), sizeof(sizeVal));
    finalExe.write("NSBK", 4);
    finalExe.close();
    if (!finalExe) {
        rep.report(ErrorCode::NS_00_FileError, "Falha ao gravar o executável: " + outputExe);
        return 1;
    }

    std::cout << "[Nebula] Sucesso! Executável autónomo gerado: " << outputExe << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc == 2) {
        std::string arg = argv[1];
        if (arg == "-version" || arg == "-v") {
            std::cout << "Nebula Script Compiler v" NEBULA_VERSION " (build " NEBULA_BUILD_STR ")\n";
            return 0;
        }
    }

    if (argc < 3) {
        printHelp(argv[0]);
        return 1;
    }

    const std::string flag = argv[1];
    ErrorReporter rep;

    if (flag == "-check") {
        if (argc != 3) {
            std::cerr << "[Erro] O comando -check aceita apenas 1 ficheiro de cada vez.\n";
            return 1;
        }
        return cmdCheck(argv[2], rep);
    }
    if (flag == "-nbc")   return cmdNbc(argc, argv, rep);
    if (flag == "-run") {
        if (argc != 3) {
            std::cerr << "[Erro] O comando -run aceita apenas 1 ficheiro .cns.\n";
            return 1;
        }
        return cmdRun(argv[2], rep);
    }
    if (flag == "-build") return cmdBuild(argc, argv, rep);

    rep.report(ErrorCode::NS_06_InvalidFlag, "Flag desconhecida: " + flag);
    printHelp(argv[0]);
    return 1;
}

#endif // MAIN_CPP