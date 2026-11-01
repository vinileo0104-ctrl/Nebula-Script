#ifndef ERROR_HPP
#define ERROR_HPP

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

// Standardized error codes for Nebula Script
enum class ErrorCode {
    NS_00_FileError,
    NS_01_MismatchedTypesError,
    NS_02_UndefinedVariableError,
    NS_03_SyntaxError,
    NS_04_RedeclarationError,
    NS_05_UnexpectedToken,
    NS_06_InvalidFlag,
    NS_07_UnterminatedString,
    NS_08_InvalidCharacter,
    NS_09_MissingClosingBracket,
    NS_10_EmptyExpression,
    NS_11_UnusedVariableError,
    NS_12_ImmutableAssignment,
    NS_13_InvalidTypeCast,
    NS_14_ArgumentCountMismatch,
    NS_15_ControlFlowEndMissing,
    NS_16_IntegerOverflow
};

// Maps error code enum to its string representation and error name (Zero-allocation using string_view)
inline std::pair<std::string_view, std::string_view> getErrorInfo(ErrorCode code) {
    switch (code) {
        case ErrorCode::NS_00_FileError:             return {"NS_00", "FileError"};
        case ErrorCode::NS_01_MismatchedTypesError:   return {"NS_01", "MismatchedTypesError"};
        case ErrorCode::NS_02_UndefinedVariableError: return {"NS_02", "UndefinedVariableError"};
        case ErrorCode::NS_03_SyntaxError:            return {"NS_03", "SyntaxError"};
        case ErrorCode::NS_04_RedeclarationError:     return {"NS_04", "RedeclarationError"};
        case ErrorCode::NS_05_UnexpectedToken:        return {"NS_05", "UnexpectedToken"};
        case ErrorCode::NS_06_InvalidFlag:           return {"NS_06", "InvalidFlag"};
        case ErrorCode::NS_07_UnterminatedString:     return {"NS_07", "UnterminatedString"};
        case ErrorCode::NS_08_InvalidCharacter:       return {"NS_08", "InvalidCharacter"};
        case ErrorCode::NS_09_MissingClosingBracket:  return {"NS_09", "MissingClosingBracket"};
        case ErrorCode::NS_10_EmptyExpression:        return {"NS_10", "EmptyExpression"};
        case ErrorCode::NS_11_UnusedVariableError:    return {"NS_11", "UnusedVariableError"};
        case ErrorCode::NS_12_ImmutableAssignment:    return {"NS_12", "ImmutableAssignment"};
        case ErrorCode::NS_13_InvalidTypeCast:        return {"NS_13", "InvalidTypeCast"};
        case ErrorCode::NS_14_ArgumentCountMismatch:  return {"NS_14", "ArgumentCountMismatch"};
        case ErrorCode::NS_15_ControlFlowEndMissing:  return {"NS_15", "ControlFlowEndMissing"};
        case ErrorCode::NS_16_IntegerOverflow:        return {"NS_16", "IntegerOverflow"};
        default:                                     return {"NS_99", "UnknownError"};
    }
}

// Represents a compiler error
struct CompilerError {
    ErrorCode code;
    std::string message;
    int line; // Line number (1-indexed)
};

// Manages and formats compiler error outputs
class ErrorReporter {
private:
    std::vector<std::string> sourceLines;
    bool hasError = false;

public:
    ErrorReporter() = default;

    // Load source lines to display snippets when an error occurs
    void setSource(const std::vector<std::string>& lines) {
        sourceLines = lines;
    }

    // Report a formatted error message
    void report(ErrorCode code, const std::string& message, int line = 0) {
        hasError = true;
        auto [codeStr, typeStr] = getErrorInfo(code);

        // Print error header in bold red
        std::cerr << "\033[1;31m[" << codeStr << "]: " << typeStr << "\033[0m\n";
        
        if (line > 0) {
            std::cerr << "  --> Line " << line << ": " << message << "\n";
            
            // Display the exact line from source code if available
            if (line <= static_cast<int>(sourceLines.size())) {
                std::cerr << "   |\n";
                std::cerr << " " << line << " | " << sourceLines[line - 1] << "\n";
                std::cerr << "   |\n";
            }
        } else {
            std::cerr << "  --> " << message << "\n";
        }
        
        std::cerr << "\n";
    }

    bool hadError() const { return hasError; }
};

#endif // ERROR_HPP
