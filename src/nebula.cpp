#include "common/NebulaApi.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "compiler/BytecodeGenerator.hpp"
#include "compiler/Error.hpp"
#include "compiler/Lexer.hpp"
#include "compiler/Parser.hpp"
#include "common/Version.hpp"
#include "runtime/Vm.hpp"

#if __has_include("compiler/TypeChecker.hpp")
#include "compiler/TypeChecker.hpp"
#define NEBULA_HAS_TYPECHECKER 1
#endif

namespace {

bool readSource(const std::string& path, ErrorReporter& reporter,
                std::vector<std::string>& lines) {
    std::ifstream file(path);
    if (!file.is_open()) {
        reporter.report(ErrorCode::NS_00_FileError,
                        "Não foi possível abrir o ficheiro: " + path);
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    if (file.bad()) {
        reporter.report(ErrorCode::NS_00_FileError,
                        "Falha ao ler o ficheiro: " + path);
        return false;
    }
    if (lines.empty()) {
        reporter.report(ErrorCode::NS_00_FileError,
                        "O ficheiro está vazio: " + path);
        return false;
    }

    reporter.setSource(lines);
    return true;
}

bool parseAndCheck(const std::string& sourcePath, ErrorReporter& reporter,
                   std::vector<std::unique_ptr<Stmt>>& statements) {
    std::vector<std::string> lines;
    if (!readSource(sourcePath, reporter, lines)) return false;

    Lexer lexer(lines, reporter);
    std::vector<Token> tokens = lexer.scanTokens();
    if (reporter.hadError()) return false;

    Parser parser(std::move(tokens), reporter);
    statements = parser.parse();
    if (reporter.hadError()) return false;

#ifdef NEBULA_HAS_TYPECHECKER
    TypeChecker checker(reporter);
    checker.check(statements);
    if (reporter.hadError()) return false;
#endif

    return true;
}

std::string defaultBytecodePath(const std::string& sourcePath) {
    const size_t separator = sourcePath.find_last_of("\\/");
    const size_t extension = sourcePath.find_last_of('.');
    const size_t baseEnd =
        extension != std::string::npos &&
        (separator == std::string::npos || extension > separator)
            ? extension : sourcePath.size();
    return sourcePath.substr(0, baseEnd) + ".cns";
}

int reportException(const char* functionName, const std::exception& exception) {
    std::cerr << "[Nebula DLL] " << functionName << " falhou: "
              << exception.what() << "\n";
    return 1;
}

}

int nebula_check_syntax(const char* sourcePath) {
    if (!sourcePath || sourcePath[0] == '\0') {
        std::cerr << "[Nebula DLL] nebula_check_syntax requer um caminho de fonte.\n";
        return 1;
    }

    try {
        ErrorReporter reporter;
        std::vector<std::unique_ptr<Stmt>> statements;
        if (!parseAndCheck(sourcePath, reporter, statements)) return 1;
        return 0;
    } catch (const std::exception& exception) {
        return reportException("nebula_check_syntax", exception);
    }
}

int nebula_compile(const char* sourcePath, const char* outputPath) {
    if (!sourcePath || sourcePath[0] == '\0') {
        std::cerr << "[Nebula DLL] nebula_compile requer um caminho de fonte.\n";
        return 1;
    }

    try {
        ErrorReporter reporter;
        std::vector<std::unique_ptr<Stmt>> statements;
        if (!parseAndCheck(sourcePath, reporter, statements)) return 1;

        BytecodeGenerator generator(std::move(statements), reporter);
        Chunk chunk = generator.generate();
        if (reporter.hadError()) return 1;

        const std::string output =
            outputPath && outputPath[0] != '\0'
                ? outputPath : defaultBytecodePath(sourcePath);
        return chunk.saveToFile(output) ? 0 : 1;
    } catch (const std::exception& exception) {
        return reportException("nebula_compile", exception);
    }
}

int nebula_run_bytecode(const char* bytecodePath) {
    if (!bytecodePath || bytecodePath[0] == '\0') {
        std::cerr << "[Nebula DLL] nebula_run_bytecode requer um caminho de bytecode.\n";
        return 1;
    }

    try {
        VM vm;
        if (!vm.loadFromFile(bytecodePath)) return 1;
        return vm.run() ? 0 : 1;
    } catch (const std::exception& exception) {
        return reportException("nebula_run_bytecode", exception);
    }
}
