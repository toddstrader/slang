//------------------------------------------------------------------------------
// SystemSubroutine.h
// System-defined subroutine handling.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#pragma once

#include "slang/binding/Expressions.h"
#include "slang/symbols/SemanticFacts.h"
#include "slang/util/SmallVector.h"
#include "slang/symbols/TypeSymbols.h"
#include "slang/util/Util.h"

namespace slang {

class SystemSubroutine {
public:
    virtual ~SystemSubroutine() = default;

    using Args = span<const Expression* const>;

    std::string name;
    SubroutineKind kind;

    virtual const Expression& bindArgument(size_t argIndex, const BindContext& context,
                                           const ExpressionSyntax& syntax) const;
    virtual const Type& checkArguments(const BindContext& context, const Args& args,
                                       SourceRange range) const = 0;
    virtual ConstantValue eval(EvalContext& context, const Args& args) const = 0;
    virtual bool verifyConstant(EvalContext& context, const Args& args) const = 0;

protected:
    SystemSubroutine(const std::string& name, SubroutineKind kind) : name(name), kind(kind) {}

    string_view kindStr() const;
    static bool checkArgCount(const BindContext& context, bool isMethod, const Args& args,
                              SourceRange callRange, size_t min, size_t max);

    static bool checkFormatArgs(const BindContext& context, const Args& args);
};

/// An implementation of the SystemSubroutine interface that has
/// basic argument types and a well-defined return type.
class SimpleSystemSubroutine : public SystemSubroutine {
public:
    const Expression& bindArgument(size_t argIndex, const BindContext& context,
                                   const ExpressionSyntax& syntax) const final;
    const Type& checkArguments(const BindContext& context, const Args& args,
                               SourceRange range) const final;
    bool verifyConstant(EvalContext&, const Args&) const override { return true; }

protected:
    SimpleSystemSubroutine(const std::string& name, SubroutineKind kind, size_t requiredArgs,
                           const std::vector<const Type*>& argTypes, const Type& returnType,
                           bool isMethod) :
        SystemSubroutine(name, kind),
        argTypes(argTypes), returnType(&returnType), requiredArgs(requiredArgs),
        isMethod(isMethod) {
        ASSERT(requiredArgs <= argTypes.size());
    }

private:
    std::vector<const Type*> argTypes;
    const Type* returnType;
    size_t requiredArgs;
    bool isMethod;
};

} // namespace slang