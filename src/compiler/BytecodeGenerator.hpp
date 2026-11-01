#ifndef BYTECODE_GENERATOR_HPP
#define BYTECODE_GENERATOR_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <memory>
#include <unordered_map>
#include "Token.hpp"
#include "Error.hpp"
#include "Ast.hpp"

enum class OpCode : uint8_t {
    OP_CONSTANT,
    OP_DEFINE_VAR,
    OP_LOAD_VAR,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_PRINT,
    OP_RETURN,
    OP_FUNC_DECL,
    OP_CALL,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LESS,
    OP_GREATER,
    OP_EQUAL,
    OP_MOD,
    OP_LOAD_DLL,
    OP_CALL_DLL,
    OP_DECLARE_EXTERNAL,
    OP_POP,
    OP_ARRAY,
    OP_GET_INDEX,
    OP_SET_INDEX,
    OP_NEGATE,
    OP_NOT,
    OP_NOT_EQUAL,
    OP_LESS_EQUAL,
    OP_GREATER_EQUAL,
    OP_AND,
    OP_OR,
    OP_JUMP_IF_TRUE,
    OP_JUMP_LONG,
    OP_JUMP_IF_FALSE_LONG,
    OP_JUMP_IF_TRUE_LONG,
    OP_FUNC_DECL_LONG,
    OP_CONSTANT_LONG,
    OP_DEFINE_VAR_LONG,
    OP_LOAD_VAR_LONG,
    OP_CALL_LONG,
    OP_LOAD_DLL_LONG,
    OP_ARRAY_LONG,
    OP_DECLARE_EXTERNAL_LONG,
    OP_NEW_STRUCT_LONG,
    OP_GET_FIELD_LONG,
    OP_SET_FIELD_LONG
};

struct StructInstance;

struct Value {
    enum Type {
        TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_BOOL, TYPE_HANDLE, TYPE_STRUCT
    } type;
    std::string asString;
    std::shared_ptr<std::vector<Value>> array;
    std::shared_ptr<StructInstance> structure;
};

struct StructInstance {
    std::string typeName;
    std::unordered_map<std::string, Value> fields;
};

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<int> lines;

    void write(uint8_t byte, int line) {
        code.push_back(byte);
        lines.push_back(line);
    }

    int addConstant(Value value) {
        constants.push_back(value);
        return static_cast<int>(constants.size() - 1);
    }

    bool saveToFile(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[ERRO] Não foi possível criar o ficheiro binário: " << filename << "\n";
            return false;
        }

        file.write("NS02", 4);

        uint32_t constantCount = static_cast<uint32_t>(constants.size());
        file.write(reinterpret_cast<const char*>(&constantCount), sizeof(constantCount));
        
        for (const auto& val : constants) {
            uint8_t typeVal = static_cast<uint8_t>(val.type);
            file.write(reinterpret_cast<const char*>(&typeVal), sizeof(typeVal));
            
            uint32_t strLen = static_cast<uint32_t>(val.asString.size());
            file.write(reinterpret_cast<const char*>(&strLen), sizeof(strLen));
            file.write(val.asString.data(), strLen);
        }

        uint32_t codeSize = static_cast<uint32_t>(code.size());
        file.write(reinterpret_cast<const char*>(&codeSize), sizeof(codeSize));
        file.write(reinterpret_cast<const char*>(code.data()), codeSize);

        file.close();
        if (!file) {
            std::cerr << "[ERRO] Falha ao gravar o ficheiro binário: " << filename << "\n";
            return false;
        }
        std::cout << "[Nebula] Ficheiro compilado guardado com sucesso: " << filename << "\n";
        return true;
    }
};

class BytecodeGenerator {
private:
    std::vector<std::unique_ptr<Stmt>> statements;
    ErrorReporter& errorReporter;
    Chunk currentChunk;
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::string>>> structDefinitions;

    void writeU32(uint32_t value, int line) {
        for (int i = 0; i < 4; ++i) {
            currentChunk.write(static_cast<uint8_t>((value >> (i * 8)) & 0xFF), line);
        }
    }

    void writeConstantIndex(int index, int line) {
        if (index < 0) {
            errorReporter.report(ErrorCode::NS_16_IntegerOverflow,
                                 "Índice de constante inválido.", line);
            return;
        }
        writeU32(static_cast<uint32_t>(index), line);
    }

    size_t emitJump(OpCode instruction, int line) {
        currentChunk.write(static_cast<uint8_t>(instruction), line);
        size_t operandOffset = currentChunk.code.size();
        for (int i = 0; i < 4; ++i) currentChunk.write(0, line);
        return operandOffset;
    }

    void patchJump(size_t operandOffset, size_t target, int line) {
        if (target > UINT32_MAX) {
            errorReporter.report(ErrorCode::NS_16_IntegerOverflow,
                                 "O destino do salto excede o limite de 32 bits.",
                                 line);
            return;
        }
        const uint32_t targetOffset = static_cast<uint32_t>(target);
        for (int i = 0; i < 4; ++i) {
            currentChunk.code[operandOffset + static_cast<size_t>(i)] =
                static_cast<uint8_t>((targetOffset >> (i * 8)) & 0xFF);
        }
    }

    void emitBool(bool value, int line) {
        int index = currentChunk.addConstant(
            Value{Value::TYPE_BOOL, value ? "true" : "false"});
        currentChunk.write(static_cast<uint8_t>(OpCode::OP_CONSTANT_LONG), line);
        writeConstantIndex(index, line);
    }

    void generateStmt(Stmt* stmt) {
        if (auto varDecl = dynamic_cast<VarDeclStmt*>(stmt)) {
            if (varDecl->initializer) {
                generateExpr(varDecl->initializer.get());
            }
            Value nameVal;
            nameVal.type = Value::TYPE_STRING;
            nameVal.asString = varDecl->name.lexeme;
            int nameIndex = currentChunk.addConstant(nameVal);

            currentChunk.write(static_cast<uint8_t>(OpCode::OP_DEFINE_VAR_LONG), 0);
            writeConstantIndex(nameIndex, 0);
        }
        else if (auto printStmt = dynamic_cast<PrintStmt*>(stmt)) {
            for (const auto& expr : printStmt->interpolations) {
                generateExpr(expr.get());
            }

            if (!printStmt->message.empty()) {
                Value val;
                val.type = Value::TYPE_STRING;
                val.asString = printStmt->message;
                int constantIndex = currentChunk.addConstant(val);

                currentChunk.write(static_cast<uint8_t>(OpCode::OP_CONSTANT_LONG), 0);
                writeConstantIndex(constantIndex, 0);
            }
            
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_PRINT), 0);
        }
        else if (auto dllStmt = dynamic_cast<DllImportStmt*>(stmt)) { 
            int dllConstIdx = currentChunk.addConstant(
                Value{Value::TYPE_STRING, dllStmt->dllPath});
            int noFunctionConstIdx = currentChunk.addConstant(
                Value{Value::TYPE_STRING, ""});

            currentChunk.write(static_cast<uint8_t>(OpCode::OP_LOAD_DLL_LONG), 0);
            writeConstantIndex(dllConstIdx, 0);
            writeConstantIndex(noFunctionConstIdx, 0);
        }
        else if (auto external = dynamic_cast<ExternFunctionDeclStmt*>(stmt)) {
            if (external->parameters.size() > UINT32_MAX) {
                errorReporter.report(ErrorCode::NS_16_IntegerOverflow,
                                     "A assinatura externa excede o limite de 32 bits.",
                                     external->name.line);
                return;
            }

            int nameIdx = currentChunk.addConstant(
                Value{Value::TYPE_STRING, external->name.lexeme});
            int returnTypeIdx = currentChunk.addConstant(
                Value{Value::TYPE_STRING, external->returnType});

            currentChunk.write(static_cast<uint8_t>(OpCode::OP_DECLARE_EXTERNAL_LONG),
                               external->name.line);
            writeConstantIndex(nameIdx, external->name.line);
            writeConstantIndex(returnTypeIdx, external->name.line);
            writeU32(static_cast<uint32_t>(external->parameters.size()),
                     external->name.line);
            for (const auto& parameter : external->parameters) {
                int typeIdx = currentChunk.addConstant(
                    Value{Value::TYPE_STRING, parameter.first});
                writeConstantIndex(typeIdx, parameter.second.line);
            }
        }
        else if (auto expression = dynamic_cast<ExpressionStmt*>(stmt)) {
            generateExpr(expression->expression.get());
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_POP), 0);
        }
        else if (auto assignment = dynamic_cast<ArraySetStmt*>(stmt)) {
            Value nameVal{Value::TYPE_STRING, assignment->name.lexeme};
            int nameIndex = currentChunk.addConstant(nameVal);
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_LOAD_VAR_LONG),
                               assignment->name.line);
            writeConstantIndex(nameIndex, assignment->name.line);
            generateExpr(assignment->index.get());
            generateExpr(assignment->value.get());
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_SET_INDEX),
                               assignment->name.line);
        }
        else if (auto assignment = dynamic_cast<FieldSetStmt*>(stmt)) {
            int nameIndex = currentChunk.addConstant(
                Value{Value::TYPE_STRING, assignment->name.lexeme});
            int fieldIndex = currentChunk.addConstant(
                Value{Value::TYPE_STRING, assignment->field.lexeme});
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_LOAD_VAR_LONG),
                               assignment->name.line);
            writeConstantIndex(nameIndex, assignment->name.line);
            generateExpr(assignment->value.get());
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_SET_FIELD_LONG),
                               assignment->field.line);
            writeConstantIndex(fieldIndex, assignment->field.line);
        }
        else if (auto sleepStmt = dynamic_cast<SleepStmt*>(stmt)) {
            generateExpr(sleepStmt->duration.get());

            int functionNameIdx = currentChunk.addConstant(
                Value{Value::TYPE_STRING, "sleep"});
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_CALL_LONG),
                               sleepStmt->keyword.line);
            writeConstantIndex(functionNameIdx, sleepStmt->keyword.line);
            writeU32(1, sleepStmt->keyword.line);
        }

        else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            // 1. Avalia a condição
            generateExpr(ifStmt->condition.get());

            // 2. Salta para o else (ou fim) se a condição for falsa
            size_t jumpElseOffset = emitJump(OpCode::OP_JUMP_IF_FALSE_LONG, 0);

            // 3. Compila o corpo do 'then'
            for (const auto& s : ifStmt->thenBranch) {
                generateStmt(s.get());
            }

            size_t jumpEndOffset = 0;
            bool hasElse = !ifStmt->elseBranch.empty();

            if (hasElse) {
                // Salta por cima do bloco else no final do bloco then
                jumpEndOffset = emitJump(OpCode::OP_JUMP_LONG, 0);
            }

            // Define o destino do salto do if falso
            size_t elseStart = currentChunk.code.size();
            patchJump(jumpElseOffset, elseStart, 0);

            // 4. Compila o corpo do 'else' (se existir)
            if (hasElse) {
                for (const auto& s : ifStmt->elseBranch) {
                    generateStmt(s.get());
                }
                size_t endPos = currentChunk.code.size();
                patchJump(jumpEndOffset, endPos, 0);
            }
        }
        else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            // Guarda o endereço onde o loop começa (para voltarmos cá no fim)
            size_t loopStart = currentChunk.code.size();

            // 1. Avalia a expressão de condição
            generateExpr(whileStmt->condition.get());

            // 2. Emite o salto condicional (se a condição for falsa, salta para fora do loop)
            size_t jumpOutOffset = emitJump(OpCode::OP_JUMP_IF_FALSE_LONG, 0);

            // 3. Compila o corpo do while
            for (const auto& s : whileStmt->body) {
                generateStmt(s.get());
            }

            // 4. Salta incondicionalmente de volta para o início do loop (para reavaliar a condição)
            size_t jumpBackOffset = emitJump(OpCode::OP_JUMP_LONG, 0);
            patchJump(jumpBackOffset, loopStart, 0);

            // 5. Preenche o placeholder do salto de saída com o endereço atual
            size_t loopEnd = currentChunk.code.size();
            patchJump(jumpOutOffset, loopEnd, 0);
        }
        else if (auto forStmt = dynamic_cast<ForStmt*>(stmt)) {
            generateStmt(forStmt->initializer.get());
            const size_t loopStart = currentChunk.code.size();
            generateExpr(forStmt->condition.get());
            const size_t jumpOutOffset = emitJump(
                OpCode::OP_JUMP_IF_FALSE_LONG, 0);
            for (const auto& statement : forStmt->body) {
                generateStmt(statement.get());
            }
            generateStmt(forStmt->increment.get());
            const size_t jumpBackOffset = emitJump(OpCode::OP_JUMP_LONG, 0);
            patchJump(jumpBackOffset, loopStart, 0);
            patchJump(jumpOutOffset, currentChunk.code.size(), 0);
        }
        else if (auto fnDecl = dynamic_cast<FunctionDeclStmt*>(stmt)) {
            Value fnNameVal{Value::TYPE_STRING, fnDecl->name.lexeme};
            int fnNameIdx = currentChunk.addConstant(fnNameVal);

            if (fnDecl->parameters.size() > UINT32_MAX) {
                errorReporter.report(ErrorCode::NS_16_IntegerOverflow,
                                     "A função excede o limite de parâmetros de 32 bits.",
                                     fnDecl->name.line);
                return;
            }
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_FUNC_DECL_LONG), 0);
            writeConstantIndex(fnNameIdx, fnDecl->name.line);
            writeU32(static_cast<uint32_t>(fnDecl->parameters.size()),
                     fnDecl->name.line);
            
            for (const auto& param : fnDecl->parameters) {
                Value paramVal{Value::TYPE_STRING, param.second.lexeme};
                int pIdx = currentChunk.addConstant(paramVal);
                writeConstantIndex(pIdx, param.second.line);
            }

            // Reserva espaço para o tamanho do corpo da função (4 bytes)
            size_t bodyLenOffset = currentChunk.code.size();
            for (int i = 0; i < 4; ++i) currentChunk.write(0, 0);

            size_t bodyStart = currentChunk.code.size();

            // Compila o corpo da função
            for (const auto& s : fnDecl->body) {
                generateStmt(s.get());
            }
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_RETURN), 0);

            size_t bodyEnd = currentChunk.code.size();
            size_t bodyLength = bodyEnd - bodyStart;

            // Grava o tamanho real do corpo no placeholder
            if (bodyLength > UINT32_MAX) {
                errorReporter.report(ErrorCode::NS_16_IntegerOverflow,
                                     "O corpo da função excede o limite de 32 bits.",
                                     fnDecl->name.line);
            } else {
                const uint32_t bodySize = static_cast<uint32_t>(bodyLength);
                for (int i = 0; i < 4; ++i) {
                    currentChunk.code[bodyLenOffset + static_cast<size_t>(i)] =
                        static_cast<uint8_t>((bodySize >> (i * 8)) & 0xFF);
                }
            }
        }
        else if (auto retStmt = dynamic_cast<ReturnStmt*>(stmt)) {
            if (retStmt->value) {
                generateExpr(retStmt->value.get());
            } else {
                Value val{Value::TYPE_INT, "0"};
                int idx = currentChunk.addConstant(val);
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_CONSTANT_LONG), 0);
                writeConstantIndex(idx, 0);
            }
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_RETURN), 0);
        }
    }

    void generateExpr(Expr* expr) {
        if (auto lit = dynamic_cast<LiteralExpr*>(expr)) {
            Value val;
            if (lit->tokenType == NsTokenType::INT_LITERAL) val.type = Value::TYPE_INT;
            else if (lit->tokenType == NsTokenType::FLOAT_LITERAL) val.type = Value::TYPE_FLOAT;
            else if (lit->tokenType == NsTokenType::STRING_LITERAL) val.type = Value::TYPE_STRING;
            else val.type = Value::TYPE_BOOL;

            val.asString = lit->value;
            int constantIndex = currentChunk.addConstant(val);

            currentChunk.write(static_cast<uint8_t>(OpCode::OP_CONSTANT_LONG), 0);
            writeConstantIndex(constantIndex, 0);
        }
        else if (auto varExpr = dynamic_cast<VariableExpr*>(expr)) {
            Value nameVal;
            nameVal.type = Value::TYPE_STRING;
            nameVal.asString = varExpr->name.lexeme;
            int nameIndex = currentChunk.addConstant(nameVal);

            currentChunk.write(static_cast<uint8_t>(OpCode::OP_LOAD_VAR_LONG), 0);
            writeConstantIndex(nameIndex, 0);
        }
        else if (auto array = dynamic_cast<ArrayLiteralExpr*>(expr)) {
            if (array->elements.size() > UINT32_MAX) {
                errorReporter.report(ErrorCode::NS_16_IntegerOverflow,
                                     "O array literal excede o limite de elementos de 32 bits.",
                                     0);
                return;
            }
            for (const auto& element : array->elements) {
                generateExpr(element.get());
            }
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_ARRAY_LONG), 0);
            writeU32(static_cast<uint32_t>(array->elements.size()), 0);
        }
        else if (auto index = dynamic_cast<IndexExpr*>(expr)) {
            generateExpr(index->object.get());
            generateExpr(index->index.get());
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_GET_INDEX), 0);
        }
        else if (auto field = dynamic_cast<FieldExpr*>(expr)) {
            generateExpr(field->object.get());
            int fieldIndex = currentChunk.addConstant(
                Value{Value::TYPE_STRING, field->field.lexeme});
            currentChunk.write(static_cast<uint8_t>(OpCode::OP_GET_FIELD_LONG),
                               field->field.line);
            writeConstantIndex(fieldIndex, field->field.line);
        }
        else if (auto unary = dynamic_cast<UnaryExpr*>(expr)) {
            generateExpr(unary->right.get());
            OpCode opcode = unary->op.tokenType == NsTokenType::BANG
                ? OpCode::OP_NOT : OpCode::OP_NEGATE;
            currentChunk.write(static_cast<uint8_t>(opcode), unary->op.line);
        }
        else if (auto callExpr = dynamic_cast<CallExpr*>(expr)) {
            auto structDefinition = structDefinitions.find(callExpr->callee.lexeme);
            if (structDefinition != structDefinitions.end()) {
                if (callExpr->arguments.size() > UINT32_MAX) {
                    errorReporter.report(ErrorCode::NS_14_ArgumentCountMismatch,
                                         "A struct excede o limite de campos de 32 bits.",
                                         callExpr->callee.line);
                    return;
                }
                for (const auto& argument : callExpr->arguments) {
                    generateExpr(argument.get());
                }
                const int typeIndex = currentChunk.addConstant(
                    Value{Value::TYPE_STRING, callExpr->callee.lexeme});
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_NEW_STRUCT_LONG),
                                   callExpr->callee.line);
                writeConstantIndex(typeIndex, callExpr->callee.line);
                writeU32(static_cast<uint32_t>(structDefinition->second.size()),
                         callExpr->callee.line);
                for (const auto& field : structDefinition->second) {
                    const int fieldIndex = currentChunk.addConstant(
                        Value{Value::TYPE_STRING, field.second});
                    writeConstantIndex(fieldIndex, callExpr->callee.line);
                }
                return;
            }
            if (callExpr->arguments.size() > UINT32_MAX) {
                errorReporter.report(ErrorCode::NS_14_ArgumentCountMismatch,
                                     "Uma chamada excede o limite de argumentos de 32 bits.",
                                     callExpr->callee.line);
                return;
            }
            for (const auto& arg : callExpr->arguments) {
                generateExpr(arg.get());
            }

            Value nameVal{Value::TYPE_STRING, callExpr->callee.lexeme};
            int nameIndex = currentChunk.addConstant(nameVal);

            currentChunk.write(static_cast<uint8_t>(OpCode::OP_CALL_LONG), 0);
            writeConstantIndex(nameIndex, callExpr->callee.line);
            writeU32(static_cast<uint32_t>(callExpr->arguments.size()),
                     callExpr->callee.line);
        }
        else if (auto bin = dynamic_cast<BinaryExpr*>(expr)) {
            if (bin->op.tokenType == NsTokenType::AMPERSAND_AMPERSAND ||
                bin->op.tokenType == NsTokenType::PIPE_PIPE) {
                const bool isAnd = bin->op.tokenType == NsTokenType::AMPERSAND_AMPERSAND;
                generateExpr(bin->left.get());
                size_t shortCircuitOffset = emitJump(
                    isAnd ? OpCode::OP_JUMP_IF_FALSE_LONG : OpCode::OP_JUMP_IF_TRUE_LONG,
                    bin->op.line);
                generateExpr(bin->right.get());
                size_t normalizeOffset = emitJump(
                    isAnd ? OpCode::OP_JUMP_IF_FALSE_LONG : OpCode::OP_JUMP_IF_TRUE_LONG,
                    bin->op.line);
                emitBool(isAnd, bin->op.line);
                size_t endOffset = emitJump(OpCode::OP_JUMP_LONG, bin->op.line);
                size_t shortCircuitTarget = currentChunk.code.size();
                emitBool(!isAnd, bin->op.line);
                size_t endTarget = currentChunk.code.size();
                patchJump(shortCircuitOffset, shortCircuitTarget, bin->op.line);
                patchJump(normalizeOffset, shortCircuitTarget, bin->op.line);
                patchJump(endOffset, endTarget, bin->op.line);
                return;
            }

            generateExpr(bin->left.get());
            generateExpr(bin->right.get());

            if (bin->op.tokenType == NsTokenType::PLUS) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_ADD), 0);
            } else if (bin->op.tokenType == NsTokenType::MINUS) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_SUB), 0);
            } else if (bin->op.tokenType == NsTokenType::STAR) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_MUL), 0);
            } else if (bin->op.tokenType == NsTokenType::SLASH) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_DIV), 0);
            } else if (bin->op.tokenType == NsTokenType::LESS) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_LESS), 0);
            } else if (bin->op.tokenType == NsTokenType::GREATER) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_GREATER), 0);
            } else if (bin->op.tokenType == NsTokenType::GREATER_EQUAL) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_GREATER_EQUAL), 0);
            } else if (bin->op.tokenType == NsTokenType::LESS_EQUAL) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_LESS_EQUAL), 0);
            } else if (bin->op.tokenType == NsTokenType::EQUAL_EQUAL) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_EQUAL), 0);
            } else if (bin->op.tokenType == NsTokenType::BANG_EQUAL) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_NOT_EQUAL), 0);
            } else if (bin->op.tokenType == NsTokenType::PERCENT) {
                currentChunk.write(static_cast<uint8_t>(OpCode::OP_MOD), 0);
            }
 else {
    errorReporter.report(ErrorCode::NS_05_UnexpectedToken,
        "Operador binário não suportado: " + bin->op.lexeme, bin->op.line);
}
        }
    }

public:
    BytecodeGenerator(std::vector<std::unique_ptr<Stmt>> stmts, ErrorReporter& reporter)
        : statements(std::move(stmts)), errorReporter(reporter) {}

    Chunk generate() {
        for (const auto& stmt : statements) {
            if (auto declaration = dynamic_cast<StructDeclStmt*>(stmt.get())) {
                auto& fields = structDefinitions[declaration->name.lexeme];
                for (const auto& field : declaration->fields) {
                    fields.push_back({field.first, field.second.lexeme});
                }
            }
        }
        for (const auto& stmt : statements) {
            if (dynamic_cast<DllImportStmt*>(stmt.get())) {
                generateStmt(stmt.get());
            }
        }
        for (const auto& stmt : statements) {
            if (dynamic_cast<ExternFunctionDeclStmt*>(stmt.get())) {
                generateStmt(stmt.get());
            }
        }
        for (const auto& stmt : statements) {
            if (!dynamic_cast<DllImportStmt*>(stmt.get()) &&
                !dynamic_cast<ExternFunctionDeclStmt*>(stmt.get()) &&
                !dynamic_cast<StructDeclStmt*>(stmt.get())) {
                generateStmt(stmt.get());
            }
        }

        currentChunk.write(static_cast<uint8_t>(OpCode::OP_RETURN), 0);
        return currentChunk;
    }
};

#endif // BYTECODE_GENERATOR_HPP