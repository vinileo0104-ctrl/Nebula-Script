#ifndef TYPE_CHECKER_HPP
#define TYPE_CHECKER_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Ast.hpp"
#include "Error.hpp"
#include "Token.hpp"

class TypeChecker {
private:
    struct FnSig {
        std::vector<std::string> params;
        std::string ret;
    };

    struct StructInfo {
        std::vector<std::pair<std::string, std::string>> fields;
    };

    ErrorReporter& rep;

    std::vector<std::unordered_map<std::string, std::string>> scopes;
    std::unordered_map<std::string, FnSig> fns;
    std::unordered_map<std::string, StructInfo> structs;

    bool inFn = false;
    bool sawReturn = false;
    std::string currentRet = "void";

    static bool isNumeric(const std::string& t) {
        return t == "number" || t == "float" || t == "double";
    }

    static bool compatible(const std::string& declared, const std::string& actual) {
        if (declared.empty() || actual.empty()) return true;
        if (declared == actual) return true;
        if (actual == "array[]" && isArrayType(declared)) return true;
        if (declared == "array[]" && actual.size() > 2 &&
            actual.compare(actual.size() - 2, 2, "[]") == 0) return true;
        return (declared == "float" || declared == "double") &&
               (actual == "number" || actual == "float" || actual == "double");
    }

    static bool isArrayType(const std::string& type) {
        return type.size() >= 2 &&
               type.compare(type.size() - 2, 2, "[]") == 0;
    }

    static std::string elementType(const std::string& type) {
        return isArrayType(type) ? type.substr(0, type.size() - 2) : "";
    }

    static bool isFfiType(const std::string& type) {
        return type == "number" || type == "float" || type == "double" ||
               type == "bool" || type == "string" || type == "handle";
    }

    bool isKnownType(const std::string& type) const {
        const std::string baseType = isArrayType(type)
            ? elementType(type) : type;
        return isNumeric(baseType) || baseType == "string" || baseType == "bool" ||
               baseType == "handle" || structs.count(baseType) != 0;
    }

    const std::string* lookup(const std::string& name) const {
        for (size_t i = scopes.size(); i > 1; --i) {
            auto it = scopes[i - 1].find(name);
            if (it != scopes[i - 1].end()) return &it->second;
        }
        auto it = scopes.front().find(name);
        return it != scopes.front().end() ? &it->second : nullptr;
    }

    static int lineOf(const Expr* e) {
        if (!e) return 0;
        if (auto b = dynamic_cast<const BinaryExpr*>(e)) return b->op.line;
        if (auto u = dynamic_cast<const UnaryExpr*>(e)) return u->op.line;
        if (auto v = dynamic_cast<const VariableExpr*>(e)) return v->name.line;
        if (auto c = dynamic_cast<const CallExpr*>(e)) return c->callee.line;
        if (auto f = dynamic_cast<const FieldExpr*>(e)) return f->field.line;
        return 0;
    }

    std::string typeOfRequired(Expr* e, int fallbackLine, const std::string& context) {
        if (!e) {
            rep.report(ErrorCode::NS_10_EmptyExpression, "Expressão em falta: " + context + ".", fallbackLine);
            return "";
        }
        return typeOf(e);
    }

    std::string typeOf(Expr* e) {
        if (!e) return "";

        if (auto lit = dynamic_cast<LiteralExpr*>(e)) {
            switch (lit->tokenType) {
                case NsTokenType::INT_LITERAL:    return "number";
                case NsTokenType::FLOAT_LITERAL:  return "float";
                case NsTokenType::STRING_LITERAL: return "string";
                default:                        return "bool";
            }
        }

        if (auto array = dynamic_cast<ArrayLiteralExpr*>(e)) {
            if (array->elements.empty()) return "array[]";
            std::string itemType = typeOfRequired(
                array->elements.front().get(), 0, "elemento do array");
            for (size_t i = 1; i < array->elements.size(); ++i) {
                std::string currentType = typeOfRequired(
                    array->elements[i].get(), 0, "elemento do array");
                if (itemType != currentType) {
                    rep.report(ErrorCode::NS_01_MismatchedTypesError,
                               "Arrays precisam conter elementos do mesmo tipo; encontrado '" +
                               itemType + "' e '" + currentType + "'.",
                               lineOf(array->elements[i].get()));
                }
            }
            return itemType + "[]";
        }

        if (auto unary = dynamic_cast<UnaryExpr*>(e)) {
            std::string operand = typeOfRequired(
                unary->right.get(), unary->op.line, "operando de '" + unary->op.lexeme + "'");
            if (unary->op.tokenType == NsTokenType::BANG) {
                if (operand != "bool" && !operand.empty()) {
                    rep.report(ErrorCode::NS_01_MismatchedTypesError,
                               "O operador '!' requer um valor bool.",
                               unary->op.line);
                }
                return "bool";
            }
            if (!isNumeric(operand) && !operand.empty()) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "O operador unário '-' requer um número.",
                           unary->op.line);
            }
            return operand;
        }

        if (auto index = dynamic_cast<IndexExpr*>(e)) {
            std::string arrayType = typeOfRequired(
                index->object.get(), 0, "array antes do índice");
            std::string indexType = typeOfRequired(
                index->index.get(), 0, "índice do array");
            if (indexType != "number" && !indexType.empty()) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "O índice de um array deve ser number.",
                           lineOf(index->index.get()));
            }
            if (!isArrayType(arrayType)) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "Só é possível indexar uma variável do tipo array.",
                           lineOf(index->object.get()));
                return "";
            }
            return elementType(arrayType);
        }

        if (auto field = dynamic_cast<FieldExpr*>(e)) {
            std::string objectType = typeOfRequired(
                field->object.get(), field->field.line, "objeto antes do campo");
            auto structInfo = structs.find(objectType);
            if (structInfo == structs.end()) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "O tipo '" + objectType + "' não possui campos.",
                           field->field.line);
                return "";
            }
            for (const auto& member : structInfo->second.fields) {
                if (member.second == field->field.lexeme) return member.first;
            }
            rep.report(ErrorCode::NS_05_UnexpectedToken,
                       "A struct '" + objectType + "' não possui o campo '" +
                       field->field.lexeme + "'.",
                       field->field.line);
            return "";
        }

        if (auto v = dynamic_cast<VariableExpr*>(e)) {
            if (const std::string* t = lookup(v->name.lexeme)) return *t;
            // Se a variável não foi declarada mas é usada, assume "number" por segurança dinâmica
            return "number";
        }

        if (auto c = dynamic_cast<CallExpr*>(e)) {
            return typeOfCall(c);
        }

        if (auto b = dynamic_cast<BinaryExpr*>(e)) {
            return typeOfBinary(b);
        }

        return "";
    }

    std::string typeOfCall(CallExpr* c) {
        const std::string& name = c->callee.lexeme;
        auto structInfo = structs.find(name);
        if (structInfo != structs.end()) {
            const auto& fields = structInfo->second.fields;
            if (fields.size() != c->arguments.size()) {
                rep.report(ErrorCode::NS_14_ArgumentCountMismatch,
                           "A struct '" + name + "' espera " +
                           std::to_string(fields.size()) + " campo(s), mas recebeu " +
                           std::to_string(c->arguments.size()) + ".",
                           c->callee.line);
            }
            for (size_t i = 0; i < c->arguments.size(); ++i) {
                const std::string actual = typeOfRequired(
                    c->arguments[i].get(), c->callee.line,
                    "valor do campo " + std::to_string(i + 1) + " de '" + name + "'");
                if (i < fields.size() && !compatible(fields[i].first, actual)) {
                    rep.report(ErrorCode::NS_01_MismatchedTypesError,
                               "Campo '" + fields[i].second + "' da struct '" + name +
                               "' espera '" + fields[i].first + "', recebeu '" +
                               actual + "'.",
                               c->callee.line);
                }
            }
            return name;
        }
        if (name == "length") {
            if (c->arguments.size() != 1) {
                rep.report(ErrorCode::NS_14_ArgumentCountMismatch,
                           "'length' espera exatamente 1 argumento.",
                           c->callee.line);
            }
            for (const auto& argument : c->arguments) {
                std::string argumentType = typeOfRequired(
                    argument.get(), c->callee.line, "argumento de 'length'");
                if (!isArrayType(argumentType) && !argumentType.empty()) {
                    rep.report(ErrorCode::NS_01_MismatchedTypesError,
                               "'length' só aceita arrays.",
                               c->callee.line);
                }
            }
            return "number";
        }
        auto it = fns.find(name);
        
        if (it == fns.end()) {
            for (const auto& a : c->arguments) {
                typeOf(a.get());
            }
            rep.report(ErrorCode::NS_05_UnexpectedToken,
                       "Função '" + name +
                       "' não declarada. Funções de DLL precisam de uma declaração 'extern fn'.",
                       c->callee.line);
            return "";
        }

        const FnSig& sig = it->second;
        if (sig.params.size() != c->arguments.size()) {
            rep.report(ErrorCode::NS_14_ArgumentCountMismatch,
                       "'" + name + "' espera " + std::to_string(sig.params.size()) +
                       " argumento(s), mas recebeu " + std::to_string(c->arguments.size()) + ".",
                       c->callee.line);
        }

        for (size_t i = 0; i < c->arguments.size(); ++i) {
            std::string actual = typeOfRequired(c->arguments[i].get(), c->callee.line,
                                                "argumento " + std::to_string(i + 1) + " de '" + name + "'");
            if (i < sig.params.size() && !compatible(sig.params[i], actual)) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "Argumento " + std::to_string(i + 1) + " de '" + name + "': esperado '" +
                           sig.params[i] + "', recebido '" + actual + "'.",
                           c->callee.line);
            }
        }
        return sig.ret;
    }

    std::string typeOfBinary(BinaryExpr* b) {
        const int line = b->op.line;
        std::string l = typeOfRequired(b->left.get(), line, "lado esquerdo de '" + b->op.lexeme + "'");
        std::string r = typeOfRequired(b->right.get(), line, "lado direito de '" + b->op.lexeme + "'");
        
        const auto opType = b->op.tokenType;

        if (opType == NsTokenType::EQUAL_EQUAL ||
            opType == NsTokenType::BANG_EQUAL) {
            if (!compatible(l, r) && !compatible(r, l) && !l.empty() && !r.empty()) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "Não é possível comparar '" + l + "' com '" + r + "'.", line);
            }
            return "bool";
        }

        if (opType == NsTokenType::AMPERSAND_AMPERSAND ||
            opType == NsTokenType::PIPE_PIPE) {
            if ((l != "bool" && !l.empty()) || (r != "bool" && !r.empty())) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "Os operadores lógicos requerem operandos bool.", line);
            }
            return "bool";
        }

        if (opType == NsTokenType::PLUS && l == "string" && r == "string") {
            return "string";
        }

        const bool comparison = (opType == NsTokenType::LESS ||
            opType == NsTokenType::LESS_EQUAL || opType == NsTokenType::GREATER ||
            opType == NsTokenType::GREATER_EQUAL);
        if (comparison) {
            if ((!isNumeric(l) || !isNumeric(r)) && !l.empty() && !r.empty()) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "Comparações de ordem requerem operandos numéricos.", line);
            }
            return "bool";
        }
        
        if (opType == NsTokenType::PERCENT) {
            if ((l != "number" || r != "number") && !l.empty() && !r.empty()) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "O operador '%' requer operandos number.", line);
            }
            return "number";
        }

        if (!isNumeric(l) || !isNumeric(r)) {
            if (!l.empty() && !r.empty()) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "Operações aritméticas requerem operandos numéricos.", line);
            }
            return "";
        }
        if (l == "double" || r == "double") return "double";
        return (l == "float" || r == "float") ? "float" : "number";
    }

    void checkBlock(const std::vector<std::unique_ptr<Stmt>>& body) {
        for (const auto& s : body) checkStmt(s.get());
    }

    void checkCondition(Expr* cond, const std::string& keyword) {
        std::string conditionType = typeOfRequired(
            cond, 0, "condição do '" + keyword + "'");
        if (conditionType != "bool" && !conditionType.empty()) {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       "A condição do '" + keyword + "' deve ser bool.",
                       lineOf(cond));
        }
    }

    void checkVarDecl(VarDeclStmt* d) {
        const std::string& name = d->name.lexeme;
        const int line = d->name.line;

        std::string actual = typeOfRequired(d->initializer.get(), line, "valor de '" + name + "'");
        if (!d->dataType.empty() && !isKnownType(d->dataType)) {
            rep.report(ErrorCode::NS_05_UnexpectedToken,
                       "Tipo não declarado: '" + d->dataType + "'.", line);
        }
        if (actual == "void") {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       "Uma função sem retorno não pode inicializar a variável '" + name + "'.",
                       line);
            return;
        }

        // Atribuições sem tipo explícito inferem o tipo do valor inicial.
        if (d->dataType.empty()) {
            const std::string* declared = lookup(name);
            if (!declared) {
                if (actual == "array[]") {
                    rep.report(ErrorCode::NS_01_MismatchedTypesError,
                               "Um array vazio precisa de um tipo explícito, por exemplo 'number[] " +
                               name + " = []'.",
                               line);
                    return;
                }
                // Declaração implícita automática no escopo atual
                scopes.back()[name] = actual.empty() ? "number" : actual;
            } else if (!compatible(*declared, actual)) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "Não é possível atribuir '" + actual + "' à variável '" +
                           name + "' do tipo '" + *declared + "'.", line);
            }
            return;
        }

        if (scopes.back().count(name)) {
            rep.report(ErrorCode::NS_04_RedeclarationError,
                       "'" + name + "' já foi declarada neste escopo.", line);
        }
        
        if (!compatible(d->dataType, actual)) {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       "A variável '" + name + "' é do tipo '" + d->dataType +
                       "', mas recebeu '" + actual + "'.", line);
        }
        scopes.back()[name] = d->dataType;
    }

    void checkArraySet(ArraySetStmt* assignment) {
        const std::string* arrayType = lookup(assignment->name.lexeme);
        if (!arrayType) {
            rep.report(ErrorCode::NS_02_UndefinedVariableError,
                       "Array não declarado: '" + assignment->name.lexeme + "'.",
                       assignment->name.line);
            typeOf(assignment->index.get());
            typeOf(assignment->value.get());
            return;
        }
        if (!isArrayType(*arrayType)) {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       "A variável '" + assignment->name.lexeme + "' não é um array.",
                       assignment->name.line);
        }
        std::string indexType = typeOfRequired(
            assignment->index.get(), assignment->name.line, "índice do array");
        if (indexType != "number" && !indexType.empty()) {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       "O índice de um array deve ser number.",
                       assignment->name.line);
        }
        std::string valueType = typeOfRequired(
            assignment->value.get(), assignment->name.line, "valor atribuído ao array");
        std::string expectedType = elementType(*arrayType);
        if (!compatible(expectedType, valueType)) {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       "O elemento do array espera '" + expectedType +
                       "', recebeu '" + valueType + "'.",
                       assignment->name.line);
        }
    }

    void checkFieldSet(FieldSetStmt* assignment) {
        const std::string* objectType = lookup(assignment->name.lexeme);
        if (!objectType) {
            rep.report(ErrorCode::NS_02_UndefinedVariableError,
                       "Variável não declarada: '" + assignment->name.lexeme + "'.",
                       assignment->name.line);
            typeOf(assignment->value.get());
            return;
        }
        auto structInfo = structs.find(*objectType);
        if (structInfo == structs.end()) {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       "A variável '" + assignment->name.lexeme +
                       "' não é uma struct.", assignment->name.line);
            typeOf(assignment->value.get());
            return;
        }
        for (const auto& member : structInfo->second.fields) {
            if (member.second != assignment->field.lexeme) continue;
            const std::string valueType = typeOfRequired(
                assignment->value.get(), assignment->field.line,
                "valor do campo '" + assignment->field.lexeme + "'");
            if (!compatible(member.first, valueType)) {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "O campo '" + assignment->field.lexeme + "' espera '" +
                           member.first + "', recebeu '" + valueType + "'.",
                           assignment->field.line);
            }
            return;
        }
        rep.report(ErrorCode::NS_05_UnexpectedToken,
                   "A struct '" + *objectType + "' não possui o campo '" +
                   assignment->field.lexeme + "'.",
                   assignment->field.line);
    }

    void checkReturn(ReturnStmt* r) {
        const int line = r->keyword.line;
        if (!inFn) {
            rep.report(ErrorCode::NS_03_SyntaxError, "'return' fora de uma função.", line);
            if (r->value) typeOf(r->value.get());
            return;
        }
        sawReturn = true;
        if (!r->value) {
            if (currentRet != "void") {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "A função deve retornar '" + currentRet + "'.", line);
            }
            return;
        }
        const std::string actual = typeOf(r->value.get());
        if (currentRet == "void" || !compatible(currentRet, actual)) {
            rep.report(ErrorCode::NS_01_MismatchedTypesError,
                       currentRet == "void"
                           ? "Uma função void não pode retornar um valor."
                           : "A função deve retornar '" + currentRet +
                             "', mas retornou '" + actual + "'.",
                       line);
        }
    }

    void registerFunction(FunctionDeclStmt* f) {
        const std::string& name = f->name.lexeme;
        FnSig sig;
        sig.ret = f->returnType;
        for (const auto& p : f->parameters) sig.params.push_back(p.first);
        fns[name] = sig;
    }

    void checkFunction(FunctionDeclStmt* f) {
        if (inFn) registerFunction(f);
        if (f->returnType != "void" && !isKnownType(f->returnType)) {
            rep.report(ErrorCode::NS_05_UnexpectedToken,
                       "Tipo de retorno não declarado: '" + f->returnType + "'.",
                       f->name.line);
        }
        for (const auto& parameter : f->parameters) {
            if (!parameter.first.empty() && !isKnownType(parameter.first)) {
                rep.report(ErrorCode::NS_05_UnexpectedToken,
                           "Tipo de parâmetro não declarado: '" + parameter.first + "'.",
                           parameter.second.line);
            }
        }

        const bool prevInFn = inFn;
        const bool prevSaw = sawReturn;
        const std::string prevRet = currentRet;

        scopes.push_back({});
        for (const auto& p : f->parameters) {
            scopes.back()[p.second.lexeme] = p.first;
        }

        inFn = true;
        sawReturn = false;
        currentRet = f->returnType;

        checkBlock(f->body);

        scopes.pop_back();
        inFn = prevInFn;
        sawReturn = prevSaw;
        currentRet = prevRet;
    }

    void checkStmt(Stmt* s) {
        if (!s) return;

        if (auto d = dynamic_cast<VarDeclStmt*>(s)) {
            checkVarDecl(d);
        } else if (auto i = dynamic_cast<IfStmt*>(s)) {
            checkCondition(i->condition.get(), "if");
            checkBlock(i->thenBranch);
            checkBlock(i->elseBranch);
        } else if (auto w = dynamic_cast<WhileStmt*>(s)) {
            checkCondition(w->condition.get(), "while");
            checkBlock(w->body);
        } else if (auto f = dynamic_cast<ForStmt*>(s)) {
            checkStmt(f->initializer.get());
            checkCondition(f->condition.get(), "for");
            checkStmt(f->increment.get());
            checkBlock(f->body);
        } else if (auto fieldSet = dynamic_cast<FieldSetStmt*>(s)) {
            checkFieldSet(fieldSet);
        } else if (dynamic_cast<StructDeclStmt*>(s)) {
            return;
        } else if (auto p = dynamic_cast<PrintStmt*>(s)) {
            for (const auto& e : p->interpolations) {
                typeOfRequired(e.get(), 0, "interpolação do 'print'");
            }
        } else if (auto sleep = dynamic_cast<SleepStmt*>(s)) {
            std::string durationType = typeOfRequired(
                sleep->duration.get(), sleep->keyword.line, "duração do 'sleep'");
            if (!durationType.empty() && durationType != "number") {
                rep.report(ErrorCode::NS_01_MismatchedTypesError,
                           "A duração do 'sleep' deve ser do tipo 'number', recebido '" +
                           durationType + "'.",
                           sleep->keyword.line);
            }
        } else if (auto r = dynamic_cast<ReturnStmt*>(s)) {
            checkReturn(r);
        } else if (auto f = dynamic_cast<FunctionDeclStmt*>(s)) {
            checkFunction(f);
        } else if (auto expr = dynamic_cast<ExpressionStmt*>(s)) {
            typeOfRequired(expr->expression.get(), 0, "chamada de função");
        } else if (auto assignment = dynamic_cast<ArraySetStmt*>(s)) {
            checkArraySet(assignment);
        }
    }

public:
    explicit TypeChecker(ErrorReporter& reporter) : rep(reporter) {
        scopes.push_back({});
        fns["sleep"] = {{"number"}, "void"};
        fns["read"] = {{}, "string"};
    }

    void check(const std::vector<std::unique_ptr<Stmt>>& program) {
        for (const auto& s : program) {
            if (auto decl = dynamic_cast<StructDeclStmt*>(s.get())) {
                if (structs.count(decl->name.lexeme)) {
                    rep.report(ErrorCode::NS_04_RedeclarationError,
                               "A struct '" + decl->name.lexeme + "' já foi declarada.",
                               decl->name.line);
                    continue;
                }
                StructInfo info;
                for (const auto& field : decl->fields) {
                    info.fields.push_back({field.first, field.second.lexeme});
                }
                structs.emplace(decl->name.lexeme, std::move(info));
            }
        }
        for (const auto& s : program) {
            if (auto f = dynamic_cast<FunctionDeclStmt*>(s.get())) registerFunction(f);
        }
        for (const auto& s : program) {
            if (auto f = dynamic_cast<ExternFunctionDeclStmt*>(s.get())) {
                if (fns.count(f->name.lexeme)) {
                    rep.report(ErrorCode::NS_04_RedeclarationError,
                               "A função externa '" + f->name.lexeme +
                               "' já está declarada.",
                               f->name.line);
                    continue;
                }

                FnSig sig;
                sig.ret = f->returnType;
                if (sig.ret != "void" && !isFfiType(sig.ret)) {
                    rep.report(ErrorCode::NS_13_InvalidTypeCast,
                               "Tipo de retorno externo não suportado: '" + sig.ret + "'.",
                               f->name.line);
                }
                for (const auto& param : f->parameters) {
                    if (!isFfiType(param.first)) {
                        rep.report(ErrorCode::NS_13_InvalidTypeCast,
                                   "Tipo de parâmetro externo não suportado: '" +
                                   param.first + "'.",
                                   param.second.line);
                    }
                    sig.params.push_back(param.first);
                }
                fns.emplace(f->name.lexeme, std::move(sig));
            }
        }
        for (const auto& s : program) {
            if (auto decl = dynamic_cast<StructDeclStmt*>(s.get())) {
                auto structInfo = structs.find(decl->name.lexeme);
                if (structInfo == structs.end()) continue;
                std::unordered_map<std::string, bool> names;
                for (const auto& field : decl->fields) {
                    if (names[field.second.lexeme]) {
                        rep.report(ErrorCode::NS_04_RedeclarationError,
                                   "Campo duplicado: '" + field.second.lexeme + "'.",
                                   field.second.line);
                    }
                    names[field.second.lexeme] = true;
                    if (!isKnownType(field.first)) {
                        rep.report(ErrorCode::NS_05_UnexpectedToken,
                                   "Tipo de campo não declarado: '" + field.first + "'.",
                                   field.second.line);
                    }
                }
            }
            checkStmt(s.get());
        }
    }
};

#endif // TYPE_CHECKER_HPP