#ifndef VM_HPP
#define VM_HPP

#ifdef _WIN32
#include <windows.h>
#include <ffi.h>
#endif

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <memory>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include "../compiler/BytecodeGenerator.hpp"
#include <thread>
#include <chrono>

struct FunctionInfo {
    std::string name;
    size_t startAddr;
    std::vector<std::string> paramNames;
};

struct CallFrame {
    size_t returnIp;
    std::unordered_map<std::string, Value> locals;
};

class VM {
private:
    Chunk chunk;
    size_t ip = 0;
    std::vector<Value> stack;
    std::unordered_map<std::string, Value> globals; 
    std::unordered_map<std::string, FunctionInfo> functions;
    std::vector<CallFrame> frames;
    std::filesystem::path sourceDirectory = std::filesystem::current_path();
    bool runtimeFailed = false;

    struct ExternalFunction {
        std::vector<std::string> parameterTypes;
        std::string returnType;
    };

    #ifdef _WIN32
        struct NativeArgument {
            int32_t integer = 0;
            float single = 0.0f;
            double floating = 0.0;
            uint8_t boolean = 0;
            const char* string = nullptr;
            void* handle = nullptr;
        };

        std::vector<std::pair<std::string, HMODULE>> loadedDlls;
        std::unordered_map<std::string, FARPROC> externalFunctions;
        std::unordered_map<std::string, ExternalFunction> externalDeclarations;

        FARPROC findExternalFunction(const std::string& name) {
            auto cached = externalFunctions.find(name);
            if (cached != externalFunctions.end()) return cached->second;

            for (auto dll = loadedDlls.rbegin(); dll != loadedDlls.rend(); ++dll) {
                FARPROC proc = GetProcAddress(dll->second, name.c_str());
                if (proc) {
                    externalFunctions[name] = proc;
                    return proc;
                }
            }
            return nullptr;
        }

        bool parseIntArgument(const Value& value, int32_t& result) {
            if (value.type != Value::TYPE_INT) return false;
            try {
                size_t parsed = 0;
                long long number = std::stoll(value.asString, &parsed);
                if (parsed != value.asString.size() ||
                    number < std::numeric_limits<int32_t>::min() ||
                    number > std::numeric_limits<int32_t>::max()) {
                    return false;
                }
                result = static_cast<int32_t>(number);
                return true;
            } catch (const std::invalid_argument&) {
                return false;
            } catch (const std::out_of_range&) {
                return false;
            }
        }

        bool loadDll(const std::string& dllPath) {
            std::filesystem::path path(dllPath);
            if (path.is_relative()) path = sourceDirectory / path;

            std::error_code pathError;
            path = std::filesystem::absolute(path, pathError).lexically_normal();
            if (pathError) {
                std::cerr << "[ERRO VM] Não foi possível resolver o caminho da DLL: "
                          << dllPath << " (" << pathError.message() << ")\n";
                runtimeFailed = true;
                return false;
            }

            const std::string pathKey = path.string();
            for (const auto& dll : loadedDlls) {
                if (dll.first == pathKey) return true;
            }

            HMODULE module = LoadLibraryW(path.c_str());
            if (!module) {
                DWORD error = GetLastError();
                std::cerr << "[ERRO VM] Não foi possível carregar a DLL: "
                          << pathKey << " (erro Windows " << error << ")\n";
                runtimeFailed = true;
                return false;
            }

            loadedDlls.emplace_back(pathKey, module);
            externalFunctions.clear();
            std::cout << "[Nebula FFI] DLL carregada com sucesso: " << pathKey << "\n";
            return true;
        }

        bool parseFloatArgument(const Value& value, double& result) {
            if (value.type != Value::TYPE_FLOAT && value.type != Value::TYPE_INT) return false;
            try {
                size_t parsed = 0;
                result = std::stod(value.asString, &parsed);
                return parsed == value.asString.size();
            } catch (const std::invalid_argument&) {
                return false;
            } catch (const std::out_of_range&) {
                return false;
            }
        }

        bool parseHandleArgument(const Value& value, void*& result) {
            if (value.type != Value::TYPE_HANDLE) return false;
            try {
                size_t parsed = 0;
                unsigned long long address = std::stoull(value.asString, &parsed);
                if (parsed != value.asString.size() ||
                    address > std::numeric_limits<uintptr_t>::max()) {
                    return false;
                }
                result = reinterpret_cast<void*>(static_cast<uintptr_t>(address));
                return true;
            } catch (const std::invalid_argument&) {
                return false;
            } catch (const std::out_of_range&) {
                return false;
            }
        }

        bool invokeExternal(const std::string& name, size_t argCount) {
            auto declaration = externalDeclarations.find(name);
            if (declaration == externalDeclarations.end()) return false;

            const ExternalFunction& signature = declaration->second;
            if (signature.parameterTypes.size() != argCount) {
                std::cerr << "[ERRO VM] A função externa '" << name << "' espera "
                          << signature.parameterTypes.size() << " argumento(s), mas recebeu "
                          << argCount << ".\n";
                runtimeFailed = true;
                return true;
            }

            FARPROC proc = findExternalFunction(name);
            if (!proc) {
                std::cerr << "[ERRO VM] Símbolo '" << name
                          << "' não encontrado nas DLLs carregadas.\n";
                runtimeFailed = true;
                return true;
            }

            std::vector<Value> args(argCount);
            for (size_t i = argCount; i > 0; --i) {
                args[i - 1] = pop();
            }

            std::vector<NativeArgument> nativeArgs(argCount);
            std::vector<ffi_type*> ffiArgTypes(argCount);
            std::vector<void*> ffiArgValues(argCount);
            for (size_t i = 0; i < argCount; ++i) {
                const std::string& type = signature.parameterTypes[i];
                NativeArgument& native = nativeArgs[i];
                if (type == "number") {
                    ffiArgTypes[i] = &ffi_type_sint32;
                    if (!parseIntArgument(args[i], native.integer)) {
                        runtimeFailed = true;
                    } else {
                        ffiArgValues[i] = &native.integer;
                    }
                } else if (type == "float") {
                    ffiArgTypes[i] = &ffi_type_float;
                    double parsed = 0.0;
                    if (!parseFloatArgument(args[i], parsed)) {
                        runtimeFailed = true;
                    } else {
                        native.single = static_cast<float>(parsed);
                        ffiArgValues[i] = &native.single;
                    }
                } else if (type == "double") {
                    ffiArgTypes[i] = &ffi_type_double;
                    if (!parseFloatArgument(args[i], native.floating)) {
                        runtimeFailed = true;
                    } else {
                        ffiArgValues[i] = &native.floating;
                    }
                } else if (type == "bool") {
                    ffiArgTypes[i] = &ffi_type_uint8;
                    if (args[i].type != Value::TYPE_BOOL ||
                        (args[i].asString != "true" && args[i].asString != "false")) {
                        runtimeFailed = true;
                    } else {
                        native.boolean = args[i].asString == "true" ? 1 : 0;
                        ffiArgValues[i] = &native.boolean;
                    }
                } else if (type == "string") {
                    ffiArgTypes[i] = &ffi_type_pointer;
                    if (args[i].type != Value::TYPE_STRING) {
                        runtimeFailed = true;
                    } else {
                        native.string = args[i].asString.c_str();
                        ffiArgValues[i] = &native.string;
                    }
                } else if (type == "handle") {
                    ffiArgTypes[i] = &ffi_type_pointer;
                    if (!parseHandleArgument(args[i], native.handle)) {
                        runtimeFailed = true;
                    } else {
                        ffiArgValues[i] = &native.handle;
                    }
                }

                if (runtimeFailed) {
                    std::cerr << "[ERRO VM] Tipo de argumento incompatível na posição "
                              << (i + 1) << " da função externa '" << name << "'.\n";
                    return true;
                }
            }

            ffi_type* ffiReturnType = nullptr;
            if (signature.returnType == "number") ffiReturnType = &ffi_type_sint32;
            else if (signature.returnType == "float") ffiReturnType = &ffi_type_float;
            else if (signature.returnType == "double") ffiReturnType = &ffi_type_double;
            else if (signature.returnType == "bool") ffiReturnType = &ffi_type_uint8;
            else if (signature.returnType == "string" ||
                     signature.returnType == "handle") ffiReturnType = &ffi_type_pointer;
            else if (signature.returnType == "void") ffiReturnType = &ffi_type_void;
            else {
                std::cerr << "[ERRO VM] Tipo de retorno externo inválido: "
                          << signature.returnType << ".\n";
                runtimeFailed = true;
                return true;
            }

            ffi_cif callInterface;
            if (ffi_prep_cif(&callInterface, FFI_DEFAULT_ABI,
                             static_cast<unsigned int>(argCount),
                             ffiReturnType, ffiArgTypes.data()) != FFI_OK) {
                std::cerr << "[ERRO VM] Não foi possível preparar a chamada FFI de '"
                          << name << "'.\n";
                runtimeFailed = true;
                return true;
            }

            union ReturnValue {
                int32_t integer;
                float single;
                double floating;
                uint8_t boolean;
                void* pointer;
            } result{};
            void* resultStorage = signature.returnType == "void" ? nullptr : &result;
            ffi_call(&callInterface, FFI_FN(proc), resultStorage, ffiArgValues.data());

            if (signature.returnType == "number") {
                push(Value{Value::TYPE_INT, std::to_string(result.integer)});
            } else if (signature.returnType == "float") {
                push(Value{Value::TYPE_FLOAT, std::to_string(result.single)});
            } else if (signature.returnType == "double") {
                push(Value{Value::TYPE_FLOAT, std::to_string(result.floating)});
            } else if (signature.returnType == "bool") {
                push(Value{Value::TYPE_BOOL, result.boolean ? "true" : "false"});
            } else if (signature.returnType == "string") {
                const char* stringResult = static_cast<const char*>(result.pointer);
                push(Value{Value::TYPE_STRING, stringResult ? stringResult : ""});
            } else if (signature.returnType == "handle") {
                push(Value{Value::TYPE_HANDLE, std::to_string(
                    reinterpret_cast<uintptr_t>(result.pointer))});
            } else {
                push(Value{Value::TYPE_INT, "0"});
            }
            return true;
        }
    #endif

    void push(Value value) {
        stack.push_back(value);
    }

    Value pop() {
        if (stack.empty()) {
            return Value{Value::TYPE_INT, "0"};
        }
        Value val = stack.back();
        stack.pop_back();
        return val;
    }

    bool parseNumericValue(const Value& value, double& result) {
        if (value.type != Value::TYPE_INT && value.type != Value::TYPE_FLOAT) {
            return false;
        }
        try {
            size_t parsed = 0;
            result = std::stod(value.asString, &parsed);
            return parsed == value.asString.size();
        } catch (const std::invalid_argument&) {
            return false;
        } catch (const std::out_of_range&) {
            return false;
        }
    }

    bool parseIntegerValue(const Value& value, long long& result) {
        if (value.type != Value::TYPE_INT) return false;
        try {
            size_t parsed = 0;
            result = std::stoll(value.asString, &parsed);
            return parsed == value.asString.size();
        } catch (const std::invalid_argument&) {
            return false;
        } catch (const std::out_of_range&) {
            return false;
        }
    }

    bool checkedIntegerOperation(OpCode op, long long left, long long right,
                                 long long& result) {
        const long long min = std::numeric_limits<long long>::min();
        const long long max = std::numeric_limits<long long>::max();
        if (op == OpCode::OP_ADD) {
            if ((right > 0 && left > max - right) ||
                (right < 0 && left < min - right)) return false;
            result = left + right;
            return true;
        }
        if (op == OpCode::OP_SUB) {
            if ((right < 0 && left > max + right) ||
                (right > 0 && left < min + right)) return false;
            result = left - right;
            return true;
        }
        if (op == OpCode::OP_MUL) {
            if (left == 0 || right == 0) {
                result = 0;
                return true;
            }
            if ((left == -1 && right == min) || (right == -1 && left == min)) {
                return false;
            }
            if (left > 0) {
                if ((right > 0 && left > max / right) ||
                    (right < 0 && right < min / left)) return false;
            } else {
                if ((right > 0 && left < min / right) ||
                    (right < 0 && left < max / right)) return false;
            }
            result = left * right;
            return true;
        }
        return false;
    }

    bool isFalse(const Value& value) {
        if (value.type == Value::TYPE_BOOL) return value.asString != "true";
        double numeric = 0.0;
        return parseNumericValue(value, numeric) && numeric == 0.0;
    }

    std::string valueToString(const Value& value) {
        if (value.type == Value::TYPE_HANDLE) return value.asString;
        if (value.structure) {
            std::string result = value.structure->typeName + "{";
            bool first = true;
            for (const auto& field : value.structure->fields) {
                if (!first) result += ", ";
                first = false;
                result += field.first + ": " + valueToString(field.second);
            }
            return result + "}";
        }
        if (!value.array) return value.asString;

        std::string result = "[";
        for (size_t i = 0; i < value.array->size(); ++i) {
            if (i > 0) result += ", ";
            result += valueToString((*value.array)[i]);
        }
        result += "]";
        return result;
    }

    bool parseArrayIndex(const Value& value, size_t& result) {
        if (value.type != Value::TYPE_INT) return false;
        try {
            size_t parsed = 0;
            long long index = std::stoll(value.asString, &parsed);
            if (parsed != value.asString.size() || index < 0) return false;
            result = static_cast<size_t>(index);
            return true;
        } catch (const std::invalid_argument&) {
            return false;
        } catch (const std::out_of_range&) {
            return false;
        }
    }

    uint32_t readU32() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value |= static_cast<uint32_t>(chunk.code[ip++]) << (i * 8);
        }
        return value;
    }

    void setVariable(const std::string& name, Value val) {
        if (!frames.empty()) {
            frames.back().locals[name] = val;
        } else {
            globals[name] = val;
        }
    }

    Value getVariable(const std::string& name) {
        if (!frames.empty()) {
            auto& locals = frames.back().locals;
            if (locals.find(name) != locals.end()) {
                return locals[name];
            }
        }
        if (globals.find(name) != globals.end()) {
            return globals[name];
        }
        std::cerr << "[ERRO VM] Variável não declarada: " << name << "\n";
        return Value{Value::TYPE_INT, "0"};
    }

public:
    VM() = default;
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    ~VM() {
        #ifdef _WIN32
        for (const auto& dll : loadedDlls) {
            FreeLibrary(dll.second);
        }
        #endif
    }

    bool loadFromFile(const std::string& filename,
                      const std::filesystem::path& libraryDirectory = {}) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[ERRO VM] Não foi possível abrir o ficheiro binário: " << filename << "\n";
            return false;
        }

        std::error_code pathError;
        std::filesystem::path absolutePath = std::filesystem::absolute(filename, pathError);
        if (pathError) {
            std::cerr << "[ERRO VM] Não foi possível resolver o caminho do bytecode: "
                      << filename << " (" << pathError.message() << ")\n";
            return false;
        }
        sourceDirectory = libraryDirectory.empty() ? absolutePath.parent_path() : libraryDirectory;
        if (sourceDirectory.is_relative()) {
            sourceDirectory = std::filesystem::absolute(sourceDirectory, pathError);
            if (pathError) {
                std::cerr << "[ERRO VM] Não foi possível resolver a pasta das DLLs: "
                          << libraryDirectory.string() << " (" << pathError.message() << ")\n";
                return false;
            }
        }

        char magic[4];
        file.read(magic, 4);
        const std::string bytecodeMagic(magic, 4);
        if (bytecodeMagic != "NS01" && bytecodeMagic != "NS02") {
            std::cerr << "[ERRO VM] Formato de ficheiro .cns inválido ou corrompido.\n";
            return false;
        }

        uint32_t constantCount;
        file.read(reinterpret_cast<char*>(&constantCount), sizeof(constantCount));
        chunk.constants.clear();
        
        for (uint32_t i = 0; i < constantCount; ++i) {
            uint8_t typeVal;
            file.read(reinterpret_cast<char*>(&typeVal), sizeof(typeVal));
            
            uint32_t strLen;
            file.read(reinterpret_cast<char*>(&strLen), sizeof(strLen));
            
            std::string strData(strLen, '\0');
            file.read(&strData[0], strLen);

            Value val;
            val.type = static_cast<Value::Type>(typeVal);
            val.asString = strData;
            chunk.constants.push_back(val); 
        }

        uint32_t codeSize;
        file.read(reinterpret_cast<char*>(&codeSize), sizeof(codeSize));
        chunk.code.resize(codeSize);
        file.read(reinterpret_cast<char*>(chunk.code.data()), codeSize);

        file.close();
        return true;
    }

    bool run() {
        ip = 0;
        frames.clear();
        stack.clear();
        runtimeFailed = false;

        while (ip < chunk.code.size()) {
            OpCode instruction = static_cast<OpCode>(chunk.code[ip++]);
            
            switch (instruction) {
                case OpCode::OP_CONSTANT:
                case OpCode::OP_CONSTANT_LONG: {
                    size_t constantIndex = instruction == OpCode::OP_CONSTANT
                        ? chunk.code[ip++] : readU32();
                    push(chunk.constants[constantIndex]);
                    break;
                }
                case OpCode::OP_POP: {
                    if (!stack.empty()) pop();
                    break;
                }
                case OpCode::OP_ARRAY:
                case OpCode::OP_ARRAY_LONG: {
                    size_t count = instruction == OpCode::OP_ARRAY
                        ? chunk.code[ip++] : readU32();
                    std::vector<Value> elements(count);
                    for (size_t i = count; i > 0; --i) {
                        elements[i - 1] = pop();
                    }
                    Value array{Value::TYPE_INT, ""};
                    array.array = std::make_shared<std::vector<Value>>(std::move(elements));
                    push(std::move(array));
                    break;
                }
                case OpCode::OP_GET_INDEX: {
                    Value indexValue = pop();
                    Value array = pop();
                    size_t index = 0;
                    if (!array.array || !parseArrayIndex(indexValue, index) ||
                        index >= array.array->size()) {
                        std::cerr << "[ERRO VM] Índice de array inválido ou fora dos limites.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    push((*array.array)[index]);
                    break;
                }
                case OpCode::OP_SET_INDEX: {
                    Value value = pop();
                    Value indexValue = pop();
                    Value array = pop();
                    size_t index = 0;
                    if (!array.array || !parseArrayIndex(indexValue, index) ||
                        index >= array.array->size()) {
                        std::cerr << "[ERRO VM] Índice de array inválido ou fora dos limites.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    (*array.array)[index] = std::move(value);
                    break;
                }
                case OpCode::OP_NEW_STRUCT_LONG: {
                    const size_t typeIndex = readU32();
                    const size_t fieldCount = readU32();
                    std::vector<std::string> fieldNames(fieldCount);
                    for (size_t i = 0; i < fieldCount; ++i) {
                        fieldNames[i] = chunk.constants[readU32()].asString;
                    }
                    auto instance = std::make_shared<StructInstance>();
                    instance->typeName = chunk.constants[typeIndex].asString;
                    for (size_t i = fieldCount; i > 0; --i) {
                        instance->fields[fieldNames[i - 1]] = pop();
                    }
                    Value value{Value::TYPE_STRUCT, ""};
                    value.structure = std::move(instance);
                    push(std::move(value));
                    break;
                }
                case OpCode::OP_GET_FIELD_LONG: {
                    const size_t fieldIndex = readU32();
                    Value object = pop();
                    const std::string& fieldName = chunk.constants[fieldIndex].asString;
                    if (!object.structure ||
                        object.structure->fields.find(fieldName) ==
                            object.structure->fields.end()) {
                        std::cerr << "[ERRO VM] Campo de struct inválido: "
                                  << fieldName << ".\n";
                        runtimeFailed = true;
                        return false;
                    }
                    push(object.structure->fields.at(fieldName));
                    break;
                }
                case OpCode::OP_SET_FIELD_LONG: {
                    const size_t fieldIndex = readU32();
                    Value value = pop();
                    Value object = pop();
                    const std::string& fieldName = chunk.constants[fieldIndex].asString;
                    if (!object.structure ||
                        object.structure->fields.find(fieldName) ==
                            object.structure->fields.end()) {
                        std::cerr << "[ERRO VM] Campo de struct inválido: "
                                  << fieldName << ".\n";
                        runtimeFailed = true;
                        return false;
                    }
                    object.structure->fields[fieldName] = std::move(value);
                    break;
                }
                case OpCode::OP_DEFINE_VAR:
                case OpCode::OP_DEFINE_VAR_LONG: {
                    size_t nameIndex = instruction == OpCode::OP_DEFINE_VAR
                        ? chunk.code[ip++] : readU32();
                    std::string varName = chunk.constants[nameIndex].asString;
                    Value val = pop(); 
                    setVariable(varName, val);
                    break;
                }
                case OpCode::OP_LOAD_VAR:
                case OpCode::OP_LOAD_VAR_LONG: {
                    size_t nameIndex = instruction == OpCode::OP_LOAD_VAR
                        ? chunk.code[ip++] : readU32();
                    std::string varName = chunk.constants[nameIndex].asString;
                    push(getVariable(varName));
                    break;
                }
                case OpCode::OP_ADD: {
                    Value b = pop();
                    Value a = pop();
                    if (a.type == Value::TYPE_STRING && b.type == Value::TYPE_STRING) {
                        push(Value{Value::TYPE_STRING, a.asString + b.asString});
                        break;
                    }
                    double left = 0.0;
                    double right = 0.0;
                    if (!parseNumericValue(a, left) || !parseNumericValue(b, right)) {
                        std::cerr << "[ERRO VM] '+' requer dois números ou duas strings.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (a.type == Value::TYPE_INT && b.type == Value::TYPE_INT) {
                        long long leftInteger = 0, rightInteger = 0, result = 0;
                        if (!parseIntegerValue(a, leftInteger) ||
                            !parseIntegerValue(b, rightInteger) ||
                            !checkedIntegerOperation(OpCode::OP_ADD, leftInteger,
                                                     rightInteger, result)) {
                            std::cerr << "[ERRO VM] Overflow ou valor inteiro inválido em '+'.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        push(Value{Value::TYPE_INT, std::to_string(result)});
                    } else {
                        push(Value{Value::TYPE_FLOAT, std::to_string(left + right)});
                    }
                    break;
                }
                case OpCode::OP_SUB: {
                    Value b = pop();
                    Value a = pop();
                    double left = 0.0;
                    double right = 0.0;
                    if (!parseNumericValue(a, left) || !parseNumericValue(b, right)) {
                        std::cerr << "[ERRO VM] '-' requer operandos numéricos.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (a.type == Value::TYPE_INT && b.type == Value::TYPE_INT) {
                        long long leftInteger = 0, rightInteger = 0, result = 0;
                        if (!parseIntegerValue(a, leftInteger) ||
                            !parseIntegerValue(b, rightInteger) ||
                            !checkedIntegerOperation(OpCode::OP_SUB, leftInteger,
                                                     rightInteger, result)) {
                            std::cerr << "[ERRO VM] Overflow ou valor inteiro inválido em '-'.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        push(Value{Value::TYPE_INT, std::to_string(result)});
                    } else {
                        push(Value{Value::TYPE_FLOAT, std::to_string(left - right)});
                    }
                    break;
                }
                case OpCode::OP_MUL: {
                    Value b = pop();
                    Value a = pop();
                    double left = 0.0;
                    double right = 0.0;
                    if (!parseNumericValue(a, left) || !parseNumericValue(b, right)) {
                        std::cerr << "[ERRO VM] '*' requer operandos numéricos.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (a.type == Value::TYPE_INT && b.type == Value::TYPE_INT) {
                        long long leftInteger = 0, rightInteger = 0, result = 0;
                        if (!parseIntegerValue(a, leftInteger) ||
                            !parseIntegerValue(b, rightInteger) ||
                            !checkedIntegerOperation(OpCode::OP_MUL, leftInteger,
                                                     rightInteger, result)) {
                            std::cerr << "[ERRO VM] Overflow ou valor inteiro inválido em '*'.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        push(Value{Value::TYPE_INT, std::to_string(result)});
                    } else {
                        push(Value{Value::TYPE_FLOAT, std::to_string(left * right)});
                    }
                    break;
                }
                case OpCode::OP_DIV: {
                    Value b = pop();
                    Value a = pop();
                    double left = 0.0;
                    double right = 0.0;
                    if (!parseNumericValue(a, left) || !parseNumericValue(b, right)) {
                        std::cerr << "[ERRO VM] '/' requer operandos numéricos.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (right == 0.0) {
                        std::cerr << "[ERRO VM] Divisão por zero!\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (a.type == Value::TYPE_INT && b.type == Value::TYPE_INT) {
                        long long leftInteger = 0, rightInteger = 0;
                        if (!parseIntegerValue(a, leftInteger) ||
                            !parseIntegerValue(b, rightInteger) ||
                            (leftInteger == std::numeric_limits<long long>::min() &&
                             rightInteger == -1)) {
                            std::cerr << "[ERRO VM] Overflow ou valor inteiro inválido em '/'.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        push(Value{Value::TYPE_INT,
                                   std::to_string(leftInteger / rightInteger)});
                    } else {
                        push(Value{Value::TYPE_FLOAT, std::to_string(left / right)});
                    }
                    break;
                }
                case OpCode::OP_PRINT: {
                    std::string textFixed = "";
                    std::string varValue = "";

                    if (!stack.empty()) {
                        textFixed = valueToString(pop());
                    }
                    if (!stack.empty()) {
                        varValue = valueToString(pop());
                        std::cout << "[Nebula Output] " << textFixed << varValue << "\n";
                    } else {
                        std::cout << "[Nebula Output] " << textFixed << "\n";
                    }
                    break;
                }
                case OpCode::OP_JUMP: {
                    uint8_t target = chunk.code[ip++];
                    ip = target;
                    break;
                }
                case OpCode::OP_JUMP_LONG: {
                    uint32_t target = readU32();
                    if (target > chunk.code.size()) {
                        std::cerr << "[ERRO VM] Destino de salto inválido.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    ip = target;
                    break;
                }
                case OpCode::OP_FUNC_DECL: {
                    uint8_t fnNameIdx = chunk.code[ip++];
                    uint8_t paramCount = chunk.code[ip++];
                    
                    FunctionInfo fnInfo;
                    fnInfo.name = chunk.constants[fnNameIdx].asString;

                    for (uint8_t i = 0; i < paramCount; ++i) {
                        uint8_t pIdx = chunk.code[ip++];
                        fnInfo.paramNames.push_back(chunk.constants[pIdx].asString);
                    }

                    uint8_t bodyLength = chunk.code[ip++];
                    fnInfo.startAddr = ip; // O IP fica exatamente no início do corpo da função

                    functions[fnInfo.name] = fnInfo;

                    // Salta o corpo da função durante a execução global linear
                    ip += bodyLength;
                    break;
                }
                case OpCode::OP_FUNC_DECL_LONG: {
                    size_t fnNameIdx = readU32();
                    uint32_t paramCount = readU32();

                    FunctionInfo fnInfo;
                    fnInfo.name = chunk.constants[fnNameIdx].asString;
                    for (uint32_t i = 0; i < paramCount; ++i) {
                        size_t paramIdx = readU32();
                        fnInfo.paramNames.push_back(
                            chunk.constants[paramIdx].asString);
                    }

                    uint32_t bodyLength = readU32();
                    fnInfo.startAddr = ip;
                    if (bodyLength > chunk.code.size() - ip) {
                        std::cerr << "[ERRO VM] Corpo de função inválido no bytecode.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    functions[fnInfo.name] = std::move(fnInfo);
                    ip += bodyLength;
                    break;
                }
                case OpCode::OP_CALL:
                case OpCode::OP_CALL_LONG: {
                    size_t fnNameIdx = instruction == OpCode::OP_CALL
                        ? chunk.code[ip++] : readU32();
                    size_t argCount = instruction == OpCode::OP_CALL
                        ? chunk.code[ip++] : readU32();
                    std::string fnName = chunk.constants[fnNameIdx].asString;

                    if (fnName == "sleep") {
                        if (argCount != 1) {
                            std::cerr << "[ERRO VM] sleep espera exatamente um argumento.\n";
                            runtimeFailed = true;
                            return false;
                        }

                        Value duration = pop();
                        try {
                            size_t parsed = 0;
                            long long milliseconds = std::stoll(duration.asString, &parsed);
                            if (duration.type != Value::TYPE_INT ||
                                parsed != duration.asString.size() || milliseconds < 0) {
                                throw std::invalid_argument("duração inválida");
                            }
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(milliseconds));
                        } catch (const std::invalid_argument&) {
                            std::cerr << "[ERRO VM] sleep requer uma duração inteira não negativa em milissegundos.\n";
                            runtimeFailed = true;
                            return false;
                        } catch (const std::out_of_range&) {
                            std::cerr << "[ERRO VM] sleep requer uma duração inteira não negativa em milissegundos.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        break;
                    }

                    if (fnName == "read") {
                        if (argCount != 0) {
                            std::cerr << "[ERRO VM] read não aceita argumentos.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        std::string inputLine;
                        std::getline(std::cin, inputLine);
                        push(Value{Value::TYPE_STRING, inputLine});
                        break;
                    }

                    if (fnName == "length") {
                        if (argCount != 1) {
                            std::cerr << "[ERRO VM] length espera exatamente um array.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        Value value = pop();
                        if (!value.array) {
                            std::cerr << "[ERRO VM] length só aceita arrays.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        push(Value{Value::TYPE_INT,
                                   std::to_string(value.array->size())});
                        break;
                    }

                    #ifdef _WIN32
                    if (invokeExternal(fnName, argCount)) {
                        if (runtimeFailed) return false;
                        break;
                    }
                    #endif

                    if (functions.find(fnName) == functions.end()) {
                        std::cerr << "[ERRO VM] Função não definida: " << fnName << "\n";
                        runtimeFailed = true;
                        return false;
                    }

                    FunctionInfo fnInfo = functions[fnName];
                    CallFrame frame;
                    frame.returnIp = ip;

                    // Extrai os argumentos transmitidos e associa aos nomes dos parâmetros
                    std::vector<Value> args(argCount);
                    for (size_t i = argCount; i > 0; --i) {
                        args[i - 1] = pop();
                    }

                    for (size_t i = 0; i < fnInfo.paramNames.size() && i < args.size(); ++i) {
                        frame.locals[fnInfo.paramNames[i]] = args[i];
                    }

                    frames.push_back(frame);
                    ip = fnInfo.startAddr;
                    break;
                }
                case OpCode::OP_RETURN: {
                    if (frames.empty()) {
                        // Fim da execução global
                        return !runtimeFailed;
                    }
                    CallFrame currentFrame = frames.back();
                    frames.pop_back();
                    ip = currentFrame.returnIp;
                    break;
                }
                case OpCode::OP_JUMP_IF_FALSE: {
                    uint8_t target = chunk.code[ip++];
                    Value conditionVal = pop();
                    if (isFalse(conditionVal)) ip = target;
                    break;
                }
                case OpCode::OP_JUMP_IF_FALSE_LONG: {
                    uint32_t target = readU32();
                    Value conditionVal = pop();
                    if (target > chunk.code.size()) {
                        std::cerr << "[ERRO VM] Destino de salto inválido.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (isFalse(conditionVal)) ip = target;
                    break;
                }
                case OpCode::OP_JUMP_IF_TRUE_LONG: {
                    uint32_t target = readU32();
                    Value conditionVal = pop();
                    if (target > chunk.code.size()) {
                        std::cerr << "[ERRO VM] Destino de salto inválido.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (!isFalse(conditionVal)) ip = target;
                    break;
                }
                case OpCode::OP_JUMP_IF_TRUE: {
                    uint8_t target = chunk.code[ip++];
                    Value conditionVal = pop();
                    if (!isFalse(conditionVal)) ip = target;
                    break;
                }
                case OpCode::OP_LESS: {
                    Value b = pop();
                    Value a = pop();
                    double left = 0.0, right = 0.0;
                    if (!parseNumericValue(a, left) || !parseNumericValue(b, right)) {
                        std::cerr << "[ERRO VM] Comparação requer operandos numéricos.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    push(Value{Value::TYPE_BOOL, left < right ? "true" : "false"});
                    break;
                }
                case OpCode::OP_GREATER:
                case OpCode::OP_LESS_EQUAL:
                case OpCode::OP_GREATER_EQUAL: {
                    Value b = pop();
                    Value a = pop();
                    double left = 0.0, right = 0.0;
                    if (!parseNumericValue(a, left) || !parseNumericValue(b, right)) {
                        std::cerr << "[ERRO VM] Comparação requer operandos numéricos.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    bool result = instruction == OpCode::OP_GREATER
                        ? left > right
                        : instruction == OpCode::OP_LESS_EQUAL
                            ? left <= right : left >= right;
                    push(Value{Value::TYPE_BOOL, result ? "true" : "false"});
                    break;
                }
                case OpCode::OP_EQUAL: {
                    Value b = pop();
                    Value a = pop();
                    bool result = false;
                    double left = 0.0, right = 0.0;
                    if (parseNumericValue(a, left) && parseNumericValue(b, right)) {
                        result = left == right;
                    } else if (a.type == b.type) {
                        result = a.structure || b.structure
                            ? a.structure == b.structure
                            : a.array || b.array
                                ? a.array == b.array : a.asString == b.asString;
                    }
                    push(Value{Value::TYPE_BOOL, result ? "true" : "false"});
                    break;
                }
                case OpCode::OP_NOT_EQUAL: {
                    Value b = pop();
                    Value a = pop();
                    bool result = false;
                    double left = 0.0, right = 0.0;
                    if (parseNumericValue(a, left) && parseNumericValue(b, right)) {
                        result = left != right;
                    } else if (a.type == b.type) {
                        result = a.structure || b.structure
                            ? a.structure != b.structure
                            : a.array || b.array
                                ? a.array != b.array : a.asString != b.asString;
                    } else {
                        result = true;
                    }
                    push(Value{Value::TYPE_BOOL, result ? "true" : "false"});
                    break;
                }
                case OpCode::OP_NOT: {
                    Value value = pop();
                    if (value.type != Value::TYPE_BOOL) {
                        std::cerr << "[ERRO VM] '!' requer um valor bool.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    push(Value{Value::TYPE_BOOL,
                               value.asString == "true" ? "false" : "true"});
                    break;
                }
                case OpCode::OP_NEGATE: {
                    Value value = pop();
                    double numeric = 0.0;
                    if (!parseNumericValue(value, numeric)) {
                        std::cerr << "[ERRO VM] O operador unário '-' requer um número.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (value.type == Value::TYPE_INT) {
                        long long integer = 0;
                        if (!parseIntegerValue(value, integer) ||
                            integer == std::numeric_limits<long long>::min()) {
                            std::cerr << "[ERRO VM] Overflow ou valor inteiro inválido em '-'.\n";
                            runtimeFailed = true;
                            return false;
                        }
                        push(Value{Value::TYPE_INT, std::to_string(-integer)});
                    } else {
                        push(Value{Value::TYPE_FLOAT, std::to_string(-numeric)});
                    }
                    break;
                }
                case OpCode::OP_MOD: {
                    Value b = pop();
                    Value a = pop();
                    long long leftInteger = 0, divisor = 0;
                    if (!parseIntegerValue(a, leftInteger) ||
                        !parseIntegerValue(b, divisor)) {
                        std::cerr << "[ERRO VM] '%' requer operandos inteiros.\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (divisor == 0) {
                        std::cerr << "[ERRO VM] Módulo por zero!\n";
                        runtimeFailed = true;
                        return false;
                    }
                    long long remainder =
                        leftInteger == std::numeric_limits<long long>::min() && divisor == -1
                            ? 0 : leftInteger % divisor;
                    push(Value{Value::TYPE_INT, std::to_string(remainder)});
                    break; 
                }
                case OpCode::OP_LOAD_DLL:
                case OpCode::OP_LOAD_DLL_LONG: {
                    size_t dllIdx = instruction == OpCode::OP_LOAD_DLL
                        ? chunk.code[ip++] : readU32();
                    size_t funcIdx = instruction == OpCode::OP_LOAD_DLL
                        ? chunk.code[ip++] : readU32();

                    std::string dllPath = chunk.constants[dllIdx].asString;
                    std::string funcName = chunk.constants[funcIdx].asString;

                #ifdef _WIN32
                    if (!loadDll(dllPath)) return false;
                    if (!funcName.empty()) {
                        FARPROC proc = findExternalFunction(funcName);
                        if (!proc) {
                            std::cerr << "[ERRO VM] Função '" << funcName
                                      << "' não foi encontrada nas DLLs carregadas.\n";
                            runtimeFailed = true;
                            return false;
                        }
                    }
                #else
                    std::cerr << "[ERRO VM] FFI (DLL) só é suportado em ambientes Windows.\n";
                    runtimeFailed = true;
                    return false;
                #endif
                    break;
                }
                case OpCode::OP_DECLARE_EXTERNAL:
                case OpCode::OP_DECLARE_EXTERNAL_LONG: {
                    size_t nameIdx = instruction == OpCode::OP_DECLARE_EXTERNAL
                        ? chunk.code[ip++] : readU32();
                    size_t returnTypeIdx = instruction == OpCode::OP_DECLARE_EXTERNAL
                        ? chunk.code[ip++] : readU32();
                    size_t argCount = instruction == OpCode::OP_DECLARE_EXTERNAL
                        ? chunk.code[ip++] : readU32();

                    ExternalFunction declaration;
                    declaration.returnType = chunk.constants[returnTypeIdx].asString;
                    for (size_t i = 0; i < argCount; ++i) {
                        size_t typeIdx = instruction == OpCode::OP_DECLARE_EXTERNAL
                            ? chunk.code[ip++] : readU32();
                        declaration.parameterTypes.push_back(
                            chunk.constants[typeIdx].asString);
                    }

                    #ifdef _WIN32
                    const std::string name = chunk.constants[nameIdx].asString;
                    if (externalDeclarations.count(name)) {
                        std::cerr << "[ERRO VM] Declaração externa duplicada: "
                                  << name << "\n";
                        runtimeFailed = true;
                        return false;
                    }
                    externalDeclarations.emplace(name, std::move(declaration));
                    #endif
                    break;
                }
                case OpCode::OP_CALL_DLL: {
                    uint8_t funcIdx = chunk.code[ip++];
                    std::string funcName = chunk.constants[funcIdx].asString;

                    #ifdef _WIN32
                    auto declaration = externalDeclarations.find(funcName);
                    if (declaration == externalDeclarations.end() ||
                        declaration->second.parameterTypes.size() > 255 ||
                        !invokeExternal(funcName, static_cast<uint8_t>(
                            declaration->second.parameterTypes.size()))) {
                        std::cerr << "[ERRO VM] Função externa não declarada: "
                                  << funcName << "\n";
                        runtimeFailed = true;
                        return false;
                    }
                    if (runtimeFailed) return false;
                    #else
                    std::cerr << "[ERRO VM] FFI (DLL) só é suportado em ambientes Windows.\n";
                    runtimeFailed = true;
                    return false;
                    #endif
                    break;
                }
                
                default:
                    std::cerr << "[ERRO VM] Opcode desconhecido: "
                              << static_cast<int>(instruction) << " em ip=" << (ip - 1) << "\n";
                    runtimeFailed = true;
                    return false;
            }
        }
        return !runtimeFailed;
    }

private:
    int std_stoi_safe(const std::string& str) {
        try {
            return std::stoi(str);
        } catch (...) {
            return 0;
        }
    }
};

#endif // VM_HPP