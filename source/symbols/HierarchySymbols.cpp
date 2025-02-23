//------------------------------------------------------------------------------
// HierarchySymbols.cpp
// Contains hierarchy-related symbol definitions.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#include "slang/symbols/HierarchySymbols.h"

#include <nlohmann/json.hpp>

#include "slang/binding/Expressions.h"
#include "slang/compilation/Compilation.h"
#include "slang/diagnostics/DeclarationsDiags.h"
#include "slang/diagnostics/LookupDiags.h"
#include "slang/diagnostics/PreprocessorDiags.h"
#include "slang/util/StackContainer.h"
#include "slang/symbols/MemberSymbols.h"
#include "slang/symbols/TypeSymbols.h"
#include "slang/syntax/AllSyntax.h"

namespace slang {

CompilationUnitSymbol::CompilationUnitSymbol(Compilation& compilation) :
    Symbol(SymbolKind::CompilationUnit, "", SourceLocation()), Scope(compilation, this) {

    // Default the time scale to the compilation default. If it turns out
    // this scope has a time unit declaration it will overwrite the member.
    timeScale = compilation.getDefaultTimeScale();
}

void CompilationUnitSymbol::addMembers(const SyntaxNode& syntax) {
    if (syntax.kind == SyntaxKind::TimeUnitsDeclaration)
        setTimeScale(*this, syntax.as<TimeUnitsDeclarationSyntax>(), !anyMembers);
    else {
        anyMembers = true;
        Scope::addMembers(syntax);
    }
}

PackageSymbol::PackageSymbol(Compilation& compilation, string_view name, SourceLocation loc,
                             const NetType& defaultNetType) :
    Symbol(SymbolKind::Package, name, loc),
    Scope(compilation, this), defaultNetType(defaultNetType) {
}

PackageSymbol& PackageSymbol::fromSyntax(Compilation& compilation,
                                         const ModuleDeclarationSyntax& syntax,
                                         const Scope& scope) {

    auto result = compilation.emplace<PackageSymbol>(compilation, syntax.header->name.valueText(),
                                                     syntax.header->name.location(),
                                                     compilation.getDefaultNetType(syntax));

    bool first = true;
    for (auto member : syntax.members) {
        if (member->kind == SyntaxKind::TimeUnitsDeclaration)
            result->setTimeScale(*result, member->as<TimeUnitsDeclarationSyntax>(), first);
        else {
            first = false;
            result->addMembers(*member);
        }
    }

    result->finalizeTimeScale(scope, syntax);

    result->setSyntax(syntax);
    compilation.addAttributes(*result, syntax.attributes);
    return *result;
}

DefinitionSymbol::DefinitionSymbol(Compilation& compilation, string_view name, SourceLocation loc,
                                   DefinitionKind definitionKind, const NetType& defaultNetType) :
    Symbol(SymbolKind::Definition, name, loc),
    Scope(compilation, this), definitionKind(definitionKind), defaultNetType(defaultNetType),
    portMap(compilation.allocSymbolMap()) {
}

const ModportSymbol* DefinitionSymbol::getModportOrError(string_view modport, const Scope& scope,
                                                         SourceRange range) const {
    if (modport.empty())
        return nullptr;

    auto symbol = find(modport);
    if (!symbol) {
        auto& diag = scope.addDiag(diag::UnknownMember, range);
        diag << modport;
        diag << this->name;
        return nullptr;
    }

    if (symbol->kind != SymbolKind::Modport) {
        auto& diag = scope.addDiag(diag::NotAModport, range);
        diag << modport;
        diag.addNote(diag::NoteDeclarationHere, symbol->location);
        return nullptr;
    }

    return &symbol->as<ModportSymbol>();
}

DefinitionSymbol& DefinitionSymbol::fromSyntax(Compilation& compilation,
                                               const ModuleDeclarationSyntax& syntax,
                                               const Scope& scope) {
    auto nameToken = syntax.header->name;
    auto result = compilation.emplace<DefinitionSymbol>(
        compilation, nameToken.valueText(), nameToken.location(),
        SemanticFacts::getDefinitionKind(syntax.kind), compilation.getDefaultNetType(syntax));

    result->setSyntax(syntax);
    compilation.addAttributes(*result, syntax.attributes);

    for (auto import : syntax.header->imports)
        result->addMembers(*import);

    SmallVectorSized<const ParameterSymbolBase*, 8> parameters;
    bool hasPortParams = syntax.header->parameters;
    if (hasPortParams) {
        bool lastLocal = false;
        for (auto declaration : syntax.header->parameters->declarations) {
            // It's legal to leave off the parameter keyword in the parameter port list.
            // If you do so, we "inherit" the parameter or localparam keyword from the previous
            // entry. This isn't allowed in a module body, but the parser will take care of the
            // error for us.
            if (declaration->keyword)
                lastLocal = declaration->keyword.kind == TokenKind::LocalParamKeyword;

            if (declaration->kind == SyntaxKind::ParameterDeclaration) {
                SmallVectorSized<ParameterSymbol*, 8> params;
                ParameterSymbol::fromSyntax(*result, declaration->as<ParameterDeclarationSyntax>(),
                                            lastLocal, /* isPort */ true, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
            else {
                SmallVectorSized<TypeParameterSymbol*, 8> params;
                TypeParameterSymbol::fromSyntax(*result,
                                                declaration->as<TypeParameterDeclarationSyntax>(),
                                                lastLocal, /* isPort */ true, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
        }
    }

    if (syntax.header->ports)
        result->addMembers(*syntax.header->ports);

    bool first = true;
    for (auto member : syntax.members) {
        if (member->kind == SyntaxKind::TimeUnitsDeclaration)
            result->setTimeScale(*result, member->as<TimeUnitsDeclarationSyntax>(), first);
        else if (member->kind != SyntaxKind::ParameterDeclarationStatement) {
            result->addMembers(*member);
            first = false;
        }
        else {
            first = false;

            auto declaration = member->as<ParameterDeclarationStatementSyntax>().parameter;
            bool isLocal =
                hasPortParams || declaration->keyword.kind == TokenKind::LocalParamKeyword;

            if (declaration->kind == SyntaxKind::ParameterDeclaration) {
                SmallVectorSized<ParameterSymbol*, 8> params;
                ParameterSymbol::fromSyntax(*result, declaration->as<ParameterDeclarationSyntax>(),
                                            isLocal, false, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
            else {
                SmallVectorSized<TypeParameterSymbol*, 8> params;
                TypeParameterSymbol::fromSyntax(*result,
                                                declaration->as<TypeParameterDeclarationSyntax>(),
                                                isLocal, false, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
        }
    }

    result->finalizeTimeScale(scope, syntax);
    result->parameters = parameters.copy(compilation);
    return *result;
}

void DefinitionSymbol::toJson(json& j) const {
    j["definitionKind"] = toString(definitionKind);
}

namespace {

Symbol* createInstance(Compilation& compilation, const DefinitionSymbol& definition,
                       const HierarchicalInstanceSyntax& syntax,
                       span<const ParameterSymbolBase* const> parameters,
                       SmallVector<int32_t>& path,
                       span<const AttributeInstanceSyntax* const> attributes) {
    InstanceSymbol* inst;
    switch (definition.definitionKind) {
        case DefinitionKind::Module:
            inst = &ModuleInstanceSymbol::instantiate(compilation, syntax, definition, parameters);
            break;
        case DefinitionKind::Interface:
            inst =
                &InterfaceInstanceSymbol::instantiate(compilation, syntax, definition, parameters);
            break;
        case DefinitionKind::Program: // TODO: handle this
        default:
            THROW_UNREACHABLE;
    }

    inst->arrayPath = path.copy(compilation);
    inst->setSyntax(syntax);
    compilation.addAttributes(*inst, attributes);
    return inst;
};

using DimIterator = span<VariableDimensionSyntax*>::iterator;

Symbol* recurseInstanceArray(Compilation& compilation, const DefinitionSymbol& definition,
                             const HierarchicalInstanceSyntax& instanceSyntax,
                             span<const ParameterSymbolBase* const> parameters,
                             const BindContext& context, DimIterator it, DimIterator end,
                             SmallVector<int32_t>& path,
                             span<const AttributeInstanceSyntax* const> attributes) {
    if (it == end) {
        return createInstance(compilation, definition, instanceSyntax, parameters, path,
                              attributes);
    }

    // Evaluate the dimensions of the array. If this fails for some reason,
    // make up an empty array so that we don't get further errors when
    // things try to reference this symbol.
    auto nameToken = instanceSyntax.name;
    EvaluatedDimension dim = context.evalDimension(**it, true);
    if (!dim.isRange()) {
        return compilation.emplace<InstanceArraySymbol>(
            compilation, nameToken.valueText(), nameToken.location(), span<const Symbol* const>{},
            ConstantRange());
    }

    ++it;

    ConstantRange range = dim.range;
    SmallVectorSized<const Symbol*, 8> elements;
    for (int32_t i = range.lower(); i <= range.upper(); i++) {
        path.append(i);
        auto symbol = recurseInstanceArray(compilation, definition, instanceSyntax, parameters,
                                           context, it, end, path, attributes);
        path.pop();

        if (!symbol)
            return nullptr;

        symbol->name = "";
        elements.append(symbol);
    }

    auto result = compilation.emplace<InstanceArraySymbol>(compilation, nameToken.valueText(),
                                                           nameToken.location(),
                                                           elements.copy(compilation), range);

    for (auto element : elements)
        result->addMember(*element);

    return result;
}

Scope& createTempInstance(Compilation& compilation, const DefinitionSymbol& def) {
    // Construct a temporary scope that has the right parent to house instance parameters
    // as we're evaluating them. We hold on to the initializer expressions and give them
    // to the instances later when we create them.
    struct TempInstance : public ModuleInstanceSymbol {
        using ModuleInstanceSymbol::ModuleInstanceSymbol;
        void setParent(const Scope& scope) { ModuleInstanceSymbol::setParent(scope); }
    };

    auto& tempDef = *compilation.emplace<TempInstance>(compilation, def.name, def.location, def);
    tempDef.setParent(*def.getParentScope());

    // Need the imports here as well, since parameters may depend on them.
    for (auto import : def.getSyntax()->as<ModuleDeclarationSyntax>().header->imports)
        tempDef.addMembers(*import);

    return tempDef;
}

} // namespace

void InstanceSymbol::fromSyntax(Compilation& compilation,
                                const HierarchyInstantiationSyntax& syntax, LookupLocation location,
                                const Scope& scope, SmallVector<const Symbol*>& results) {

    auto definition = compilation.getDefinition(syntax.type.valueText(), scope);
    if (!definition) {
        scope.addDiag(diag::UnknownModule, syntax.type.range()) << syntax.type.valueText();
        return;
    }

    SmallMap<string_view, const ExpressionSyntax*, 8> paramOverrides;
    if (syntax.parameters) {
        // Build up data structures to easily index the parameter assignments. We need to handle
        // both ordered assignment as well as named assignment, though a specific instance can only
        // use one method or the other.
        bool hasParamAssignments = false;
        bool orderedAssignments = true;
        SmallVectorSized<const OrderedArgumentSyntax*, 8> orderedParams;
        SmallMap<string_view, std::pair<const NamedArgumentSyntax*, bool>, 8> namedParams;

        for (auto paramBase : syntax.parameters->assignments->parameters) {
            bool isOrdered = paramBase->kind == SyntaxKind::OrderedArgument;
            if (!hasParamAssignments) {
                hasParamAssignments = true;
                orderedAssignments = isOrdered;
            }
            else if (isOrdered != orderedAssignments) {
                scope.addDiag(diag::MixingOrderedAndNamedParams,
                              paramBase->getFirstToken().location());
                break;
            }

            if (isOrdered)
                orderedParams.append(&paramBase->as<OrderedArgumentSyntax>());
            else {
                const NamedArgumentSyntax& nas = paramBase->as<NamedArgumentSyntax>();
                auto name = nas.name.valueText();
                if (!name.empty()) {
                    auto pair = namedParams.emplace(name, std::make_pair(&nas, false));
                    if (!pair.second) {
                        auto& diag =
                            scope.addDiag(diag::DuplicateParamAssignment, nas.name.location());
                        diag << name;
                        diag.addNote(diag::NotePreviousUsage,
                                     pair.first->second.first->name.location());
                    }
                }
            }
        }

        // For each parameter assignment we have, match it up to a real parameter
        if (orderedAssignments) {
            uint32_t orderedIndex = 0;
            for (auto param : definition->parameters) {
                if (orderedIndex >= orderedParams.size())
                    break;

                if (param->isLocalParam())
                    continue;

                paramOverrides.emplace(param->symbol.name, orderedParams[orderedIndex++]->expr);
            }

            // Make sure there aren't extra param assignments for non-existent params.
            if (orderedIndex < orderedParams.size()) {
                auto loc = orderedParams[orderedIndex]->getFirstToken().location();
                auto& diag = scope.addDiag(diag::TooManyParamAssignments, loc);
                diag << definition->name;
                diag << orderedParams.size();
                diag << orderedIndex;
            }
        }
        else {
            // Otherwise handle named assignments.
            for (auto param : definition->parameters) {
                auto it = namedParams.find(param->symbol.name);
                if (it == namedParams.end())
                    continue;

                const NamedArgumentSyntax* arg = it->second.first;
                it->second.second = true;
                if (param->isLocalParam()) {
                    // Can't assign to localparams, so this is an error.
                    DiagCode code = param->isPortParam() ? diag::AssignedToLocalPortParam
                                                         : diag::AssignedToLocalBodyParam;

                    auto& diag = scope.addDiag(code, arg->name.location());
                    diag.addNote(diag::NoteDeclarationHere, param->symbol.location);
                    continue;
                }

                // It's allowed to have no initializer in the assignment; it means to just use the
                // default.
                if (!arg->expr)
                    continue;

                paramOverrides.emplace(param->symbol.name, arg->expr);
            }

            for (const auto& pair : namedParams) {
                // We marked all the args that we used, so anything left over is a param assignment
                // for a non-existent parameter.
                if (!pair.second.second) {
                    auto& diag = scope.addDiag(diag::ParameterDoesNotExist,
                                               pair.second.first->name.location());
                    diag << pair.second.first->name.valueText();
                    diag << definition->name;
                }
            }
        }
    }

    // As an optimization, determine values for all parameters now so that they can be
    // shared between instances. That way an instance array with hundreds of entries
    // doesn't recompute the same param values over and over again.
    Scope& tempDef = createTempInstance(compilation, *definition);

    BindContext context(scope, location, BindFlags::Constant);
    SmallVectorSized<const ParameterSymbolBase*, 8> parameters;

    for (auto param : definition->parameters) {
        if (param->symbol.kind == SymbolKind::Parameter) {
            // This is a value parameter.
            ParameterSymbol& newParam = param->symbol.as<ParameterSymbol>().clone(compilation);
            tempDef.addMember(newParam);
            parameters.append(&newParam);

            if (auto it = paramOverrides.find(newParam.name); it != paramOverrides.end()) {
                auto& expr = *it->second;
                newParam.setInitializerSyntax(expr, expr.getFirstToken().location());

                auto declared = newParam.getDeclaredType();
                declared->clearResolved();
                declared->resolveAt(context);
            }
            else if (!newParam.isLocalParam() && newParam.isPortParam() &&
                     !newParam.getInitializer()) {
                auto& diag =
                    scope.addDiag(diag::ParamHasNoValue, syntax.getFirstToken().location());
                diag << definition->name;
                diag << newParam.name;
            }
            else {
                newParam.getDeclaredType()->clearResolved();
            }
        }
        else {
            // Otherwise this is a type parameter.
            auto& newParam = param->symbol.as<TypeParameterSymbol>().clone(compilation);
            tempDef.addMember(newParam);
            parameters.append(&newParam);

            auto& declared = newParam.targetType;

            if (auto it = paramOverrides.find(newParam.name); it != paramOverrides.end()) {
                auto& expr = *it->second;

                // If this is a NameSyntax, the parser didn't know we were assigning to
                // a type parameter, so fix it up into a NamedTypeSyntax to get a type from it.
                if (NameSyntax::isKind(expr.kind)) {
                    // const_cast is ugly but safe here, we're only going to refer to it
                    // by const reference everywhere down.
                    auto& nameSyntax = const_cast<NameSyntax&>(expr.as<NameSyntax>());
                    auto namedType = compilation.emplace<NamedTypeSyntax>(nameSyntax);
                    declared.setType(compilation.getType(*namedType, location, scope));
                }
                else if (!DataTypeSyntax::isKind(expr.kind)) {
                    scope.addDiag(diag::BadTypeParamExpr, expr.getFirstToken().location())
                        << newParam.name;
                    declared.clearResolved();
                }
                else {
                    declared.setType(
                        compilation.getType(expr.as<DataTypeSyntax>(), location, scope));
                }
            }
            else if (!newParam.isLocalParam() && newParam.isPortParam() &&
                     !declared.getTypeSyntax()) {
                auto& diag =
                    scope.addDiag(diag::ParamHasNoValue, syntax.getFirstToken().location());
                diag << definition->name;
                diag << newParam.name;
            }
            else {
                declared.clearResolved();
            }
        }
    }

    for (auto instanceSyntax : syntax.instances) {
        SmallVectorSized<int32_t, 4> path;
        auto dims = instanceSyntax->dimensions;
        auto symbol =
            recurseInstanceArray(compilation, *definition, *instanceSyntax, parameters, context,
                                 dims.begin(), dims.end(), path, syntax.attributes);
        if (symbol)
            results.append(symbol);
    }
}

InstanceSymbol::InstanceSymbol(SymbolKind kind, Compilation& compilation, string_view name,
                               SourceLocation loc, const DefinitionSymbol& definition) :
    Symbol(kind, name, loc),
    Scope(compilation, this), definition(definition), portMap(compilation.allocSymbolMap()) {
}

void InstanceSymbol::toJson(json& j) const {
    j["definition"] = jsonLink(definition);
}

bool InstanceSymbol::isKind(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::ModuleInstance:
        case SymbolKind::InterfaceInstance:
        case SymbolKind::Program:
            return true;
        default:
            return false;
    }
}

void InstanceSymbol::populate(const HierarchicalInstanceSyntax* instanceSyntax,
                              span<const ParameterSymbolBase* const> parameters) {
    // TODO: getSyntax dependency
    auto& declSyntax = definition.getSyntax()->as<ModuleDeclarationSyntax>();
    Compilation& comp = getCompilation();

    // Package imports from the header always come first.
    for (auto import : declSyntax.header->imports)
        addMembers(*import);

    // Now add in all parameter ports.
    auto paramIt = parameters.begin();
    while (paramIt != parameters.end()) {
        auto original = *paramIt;
        if (!original->isPortParam())
            break;

        if (original->symbol.kind == SymbolKind::Parameter)
            addMember(original->symbol.as<ParameterSymbol>().clone(comp));
        else
            addMember(original->symbol.as<TypeParameterSymbol>().clone(comp));

        paramIt++;
    }

    // It's important that the port syntax is added before any body members, so that port
    // connections are elaborated before anything tries to depend on any interface port params.
    if (declSyntax.header->ports)
        addMembers(*declSyntax.header->ports);

    // Connect all ports to external sources.
    if (instanceSyntax)
        setPortConnections(instanceSyntax->connections);

    // Finally add members from the body.
    for (auto member : declSyntax.members) {
        // If this is a parameter declaration, we should already have metadata for it in our
        // parameters list. The list is given in declaration order, so we should be be able to move
        // through them incrementally.
        if (member->kind != SyntaxKind::ParameterDeclarationStatement)
            addMembers(*member);
        else {
            auto paramBase = member->as<ParameterDeclarationStatementSyntax>().parameter;
            if (paramBase->kind == SyntaxKind::ParameterDeclaration) {
                for (auto declarator : paramBase->as<ParameterDeclarationSyntax>().declarators) {
                    ASSERT(paramIt != parameters.end());

                    auto& symbol = (*paramIt)->symbol;
                    ASSERT(declarator->name.valueText() == symbol.name);

                    addMember(symbol.as<ParameterSymbol>().clone(comp));
                    paramIt++;
                }
            }
            else {
                for (auto declarator :
                     paramBase->as<TypeParameterDeclarationSyntax>().declarators) {
                    ASSERT(paramIt != parameters.end());

                    auto& symbol = (*paramIt)->symbol;
                    ASSERT(declarator->name.valueText() == symbol.name);

                    addMember(symbol.as<TypeParameterSymbol>().clone(comp));
                    paramIt++;
                }
            }
        }
    }
}

ModuleInstanceSymbol& ModuleInstanceSymbol::instantiate(Compilation& compilation, string_view name,
                                                        SourceLocation loc,
                                                        const DefinitionSymbol& definition) {
    auto instance = compilation.emplace<ModuleInstanceSymbol>(compilation, name, loc, definition);
    instance->populate(nullptr, definition.parameters);
    return *instance;
}

ModuleInstanceSymbol& ModuleInstanceSymbol::instantiate(
    Compilation& compilation, const HierarchicalInstanceSyntax& syntax,
    const DefinitionSymbol& definition, span<const ParameterSymbolBase* const> parameters) {

    auto instance = compilation.emplace<ModuleInstanceSymbol>(compilation, syntax.name.valueText(),
                                                              syntax.name.location(), definition);
    instance->populate(&syntax, parameters);
    return *instance;
}

InterfaceInstanceSymbol& InterfaceInstanceSymbol::instantiate(
    Compilation& compilation, const HierarchicalInstanceSyntax& syntax,
    const DefinitionSymbol& definition, span<const ParameterSymbolBase* const> parameters) {

    auto instance = compilation.emplace<InterfaceInstanceSymbol>(
        compilation, syntax.name.valueText(), syntax.name.location(), definition);

    instance->populate(&syntax, parameters);
    return *instance;
}

void InstanceArraySymbol::toJson(json& j) const {
    j["range"] = range.toString();
}

const Statement& SequentialBlockSymbol::getBody() const {
    return binder.getStatement(BindContext(*this, LookupLocation::max));
}

SequentialBlockSymbol& SequentialBlockSymbol::fromSyntax(Compilation& compilation,
                                                         const BlockStatementSyntax& syntax) {
    string_view name;
    SourceLocation loc;
    if (syntax.blockName) {
        auto token = syntax.blockName->name;
        name = token.valueText();
        loc = token.location();
    }
    else {
        name = "";
        loc = syntax.begin.location();
    }

    auto result = compilation.emplace<SequentialBlockSymbol>(compilation, name, loc);
    result->binder.setItems(*result, syntax.items);
    result->setSyntax(syntax);

    compilation.addAttributes(*result, syntax.attributes);

    return *result;
}

SequentialBlockSymbol& SequentialBlockSymbol::fromSyntax(Compilation& compilation,
                                                         const ForLoopStatementSyntax& syntax) {
    auto result =
        compilation.emplace<SequentialBlockSymbol>(compilation, "", syntax.forKeyword.location());
    result->setSyntax(syntax);

    // If one entry is a variable declaration, they should all be.
    const VariableSymbol* lastVar = nullptr;
    for (auto init : syntax.initializers) {
        auto& var = VariableSymbol::fromSyntax(compilation,
                                               init->as<ForVariableDeclarationSyntax>(), lastVar);

        lastVar = &var;
        result->addMember(var);
    }

    result->binder.setSyntax(*result, syntax);
    for (auto block : result->binder.getBlocks())
        result->addMember(*block);

    compilation.addAttributes(*result, syntax.attributes);

    return *result;
}

const Statement& ProceduralBlockSymbol::getBody() const {
    return binder.getStatement(BindContext(*getParentScope(), LookupLocation::after(*this)));
}

ProceduralBlockSymbol& ProceduralBlockSymbol::fromSyntax(
    const Scope& scope, const ProceduralBlockSyntax& syntax,
    span<const SequentialBlockSymbol* const>& additionalBlocks) {

    auto& comp = scope.getCompilation();
    auto kind = SemanticFacts::getProceduralBlockKind(syntax.kind);
    auto result = comp.emplace<ProceduralBlockSymbol>(syntax.keyword.location(), kind);

    result->binder.setSyntax(scope, *syntax.statement);
    result->setSyntax(syntax);
    comp.addAttributes(*result, syntax.attributes);

    additionalBlocks = result->binder.getBlocks();

    return *result;
}

void ProceduralBlockSymbol::toJson(json& j) const {
    j["procedureKind"] = toString(procedureKind);
}

static string_view getGenerateBlockName(const SyntaxNode& node) {
    if (node.kind != SyntaxKind::GenerateBlock)
        return "";

    // Try to find a name for this block. Generate blocks allow the name to be specified twice
    // (for no good reason) so check both locations. The parser has already checked for
    // inconsistencies here.
    const GenerateBlockSyntax& block = node.as<GenerateBlockSyntax>();
    if (block.label)
        return block.label->name.valueText();

    if (block.beginName)
        return block.beginName->name.valueText();

    return "";
}

static void createCondGenBlock(Compilation& compilation, const SyntaxNode& syntax,
                               LookupLocation location, const Scope& parent,
                               uint32_t constructIndex, bool isInstantiated,
                               const SyntaxList<AttributeInstanceSyntax>& attributes,
                               SmallVector<GenerateBlockSymbol*>& results) {
    // [27.5] If a generate block in a conditional generate construct consists of only one item
    // that is itself a conditional generate construct and if that item is not surrounded by
    // begin-end keywords, then this generate block is not treated as a separate scope. The
    // generate construct within this block is said to be directly nested. The generate blocks
    // of the directly nested construct are treated as if they belong to the outer construct.
    switch (syntax.kind) {
        case SyntaxKind::IfGenerate:
            GenerateBlockSymbol::fromSyntax(compilation, syntax.as<IfGenerateSyntax>(), location,
                                            parent, constructIndex, isInstantiated, results);
            return;
        case SyntaxKind::CaseGenerate:
            GenerateBlockSymbol::fromSyntax(compilation, syntax.as<CaseGenerateSyntax>(), location,
                                            parent, constructIndex, isInstantiated, results);
            return;
        default:
            break;
    }

    string_view name = getGenerateBlockName(syntax);
    SourceLocation loc = syntax.getFirstToken().location();

    auto block = compilation.emplace<GenerateBlockSymbol>(compilation, name, loc, constructIndex,
                                                          isInstantiated);

    block->addMembers(syntax);
    block->setSyntax(syntax);
    compilation.addAttributes(*block, attributes);
    results.append(block);
}

void GenerateBlockSymbol::fromSyntax(Compilation& compilation, const IfGenerateSyntax& syntax,
                                     LookupLocation location, const Scope& parent,
                                     uint32_t constructIndex, bool isInstantiated,
                                     SmallVector<GenerateBlockSymbol*>& results) {
    optional<bool> selector;
    if (isInstantiated) {
        BindContext bindContext(parent, location, BindFlags::Constant);
        const auto& cond = Expression::bind(*syntax.condition, bindContext);
        if (cond.constant && bindContext.requireBooleanConvertible(cond))
            selector = cond.constant->isTrue();
    }

    createCondGenBlock(compilation, *syntax.block, location, parent, constructIndex,
                       selector.has_value() && selector.value(), syntax.attributes, results);
    if (syntax.elseClause) {
        createCondGenBlock(compilation, *syntax.elseClause->clause, location, parent,
                           constructIndex, selector.has_value() && !selector.value(),
                           syntax.attributes, results);
    }
}

void GenerateBlockSymbol::fromSyntax(Compilation& compilation, const CaseGenerateSyntax& syntax,
                                     LookupLocation location, const Scope& parent,
                                     uint32_t constructIndex, bool isInstantiated,
                                     SmallVector<GenerateBlockSymbol*>& results) {

    SmallVectorSized<const ExpressionSyntax*, 8> expressions;
    const SyntaxNode* defBlock = nullptr;
    for (auto item : syntax.items) {
        switch (item->kind) {
            case SyntaxKind::StandardCaseItem: {
                auto& sci = item->as<StandardCaseItemSyntax>();
                for (auto es : sci.expressions)
                    expressions.append(es);
                break;
            }
            case SyntaxKind::DefaultCaseItem:
                // The parser already errored for duplicate defaults,
                // so just ignore if it happens here.
                defBlock = item->as<DefaultCaseItemSyntax>().clause;
                break;
            default:
                THROW_UNREACHABLE;
        }
    }

    BindContext bindContext(parent, location, BindFlags::Constant);
    SmallVectorSized<const Expression*, 8> bound;
    if (!Expression::bindCaseExpressions(bindContext, TokenKind::CaseKeyword, *syntax.condition,
                                         expressions, bound)) {
        return;
    }

    auto boundIt = bound.begin();
    auto condExpr = *boundIt++;
    if (!condExpr->constant)
        return;

    SourceRange matchRange;
    bool found = false;
    bool warned = false;

    for (auto item : syntax.items) {
        if (item->kind != SyntaxKind::StandardCaseItem)
            continue;

        // Check each case expression to see if it matches the condition value.
        bool currentFound = false;
        SourceRange currentMatchRange;
        auto& sci = item->as<StandardCaseItemSyntax>();
        for (size_t i = 0; i < sci.expressions.size(); i++) {
            // Have to keep incrementing the iterator here so that we stay in sync.
            auto expr = *boundIt++;
            auto val = expr->constant;
            if (!currentFound && val && val->equivalentTo(*condExpr->constant)) {
                currentFound = true;
                currentMatchRange = expr->sourceRange;
            }
        }

        if (currentFound && !found) {
            // This is the first match for this entire case generate.
            found = true;
            matchRange = currentMatchRange;
            createCondGenBlock(compilation, *sci.clause, location, parent, constructIndex,
                               isInstantiated, syntax.attributes, results);
        }
        else {
            // If we previously found a block, this block also matched, which we should warn about.
            if (currentFound && !warned) {
                auto& diag = parent.addDiag(diag::CaseGenerateDup, currentMatchRange);
                diag << *condExpr->constant;
                diag.addNote(diag::NotePreviousMatch, matchRange.start());
                warned = true;
            }

            // This block is not taken, so create it as uninstantiated.
            createCondGenBlock(compilation, *sci.clause, location, parent, constructIndex, false,
                               syntax.attributes, results);
        }
    }

    if (defBlock) {
        // Only instantiated if no other blocks were instantiated.
        createCondGenBlock(compilation, *defBlock, location, parent, constructIndex,
                           isInstantiated && !found, syntax.attributes, results);
    }
    else if (!found) {
        auto& diag = parent.addDiag(diag::CaseGenerateNoBlock, condExpr->sourceRange);
        diag << *condExpr->constant;
    }
}

void GenerateBlockSymbol::toJson(json& j) const {
    j["constructIndex"] = constructIndex;
    j["isInstantiated"] = isInstantiated;
}

GenerateBlockArraySymbol& GenerateBlockArraySymbol::fromSyntax(
    Compilation& compilation, const LoopGenerateSyntax& syntax, SymbolIndex scopeIndex,
    LookupLocation location, const Scope& parent, uint32_t constructIndex) {

    string_view name = getGenerateBlockName(*syntax.block);
    SourceLocation loc = syntax.block->getFirstToken().location();
    auto result =
        compilation.emplace<GenerateBlockArraySymbol>(compilation, name, loc, constructIndex);
    result->setSyntax(syntax);
    compilation.addAttributes(*result, syntax.attributes);

    auto genvar = syntax.identifier;
    if (genvar.isMissing())
        return *result;

    // If the loop initializer has a `genvar` keyword, we can use the name directly
    // Otherwise we need to do a lookup to make sure we have the actual genvar somewhere.
    if (!syntax.genvar) {
        auto symbol = parent.lookupUnqualifiedName(genvar.valueText(), location, genvar.range(),
                                                   LookupFlags::None, true);
        if (!symbol)
            return *result;

        if (symbol->kind != SymbolKind::Genvar) {
            auto& diag = parent.addDiag(diag::NotAGenvar, genvar.range());
            diag << genvar.valueText();
            diag.addNote(diag::NoteDeclarationHere, symbol->location);
        }
    }

    SmallVectorSized<ArrayEntry, 8> entries;
    auto createBlock = [&](ConstantValue value, bool isInstantiated) {
        // Spec: each generate block gets their own scope, with an implicit
        // localparam of the same name as the genvar.
        auto block =
            compilation.emplace<GenerateBlockSymbol>(compilation, "", loc, 1u, isInstantiated);
        auto implicitParam = compilation.emplace<ParameterSymbol>(
            genvar.valueText(), genvar.location(), true /* isLocal */, false /* isPort */);

        block->addMember(*implicitParam);
        block->addMembers(*syntax.block);
        block->setSyntax(*syntax.block);
        result->addMember(*block);

        implicitParam->setType(compilation.getIntegerType());
        implicitParam->setValue(std::move(value));

        entries.append({ &implicitParam->getValue().integer(), block });
    };

    // Bind the initialization expression.
    BindContext bindContext(parent, location, BindFlags::Constant);
    const auto& initial = Expression::bind(compilation.getIntegerType(), *syntax.initialExpr,
                                           syntax.equals.location(), bindContext);
    if (!initial.constant)
        return *result;

    // Fabricate a local variable that will serve as the loop iteration variable.
    auto& iterScope = *compilation.emplace<SequentialBlockSymbol>(compilation, "", loc);
    auto& local = *compilation.emplace<VariableSymbol>(genvar.valueText(), genvar.location());
    local.setType(compilation.getIntegerType());

    iterScope.setTemporaryParent(parent, scopeIndex);
    iterScope.addMember(local);

    // Bind the stop and iteration expressions so we can reuse them on each iteration.
    BindContext iterContext(iterScope, LookupLocation::max, BindFlags::NoHierarchicalNames);
    const auto& stopExpr = Expression::bind(*syntax.stopExpr, iterContext);
    const auto& iterExpr = Expression::bind(*syntax.iterationExpr, iterContext);
    if (stopExpr.bad() || iterExpr.bad())
        return *result;

    if (!bindContext.requireBooleanConvertible(stopExpr))
        return *result;

    EvalContext stopVerifyContext(iterScope, EvalFlags::IsVerifying);
    bool canBeConst = stopExpr.verifyConstant(stopVerifyContext);
    stopVerifyContext.reportDiags(iterContext, stopExpr.sourceRange);
    if (!canBeConst)
        return *result;

    EvalContext iterVerifyContext(iterScope, EvalFlags::IsVerifying);
    canBeConst = iterExpr.verifyConstant(iterVerifyContext);
    iterVerifyContext.reportDiags(iterContext, iterExpr.sourceRange);
    if (!canBeConst)
        return *result;

    // Create storage for the iteration variable.
    EvalContext evalContext(iterScope);
    auto loopVal = evalContext.createLocal(&local, *initial.constant);

    if (loopVal->integer().hasUnknown())
        iterContext.addDiag(diag::GenvarUnknownBits, genvar.range()) << *loopVal;

    // Generate blocks!
    SmallSet<SVInt, 8> usedValues;
    bool any = false;
    while (true) {
        auto stop = stopExpr.eval(evalContext);
        if (stop.bad() || !stop.isTrue())
            break;

        auto pair = usedValues.emplace(loopVal->integer());
        if (!pair.second) {
            iterContext.addDiag(diag::GenvarDuplicate, genvar.range()) << *loopVal;
            break;
        }

        any = true;
        createBlock(*loopVal, true);

        if (!iterExpr.eval(evalContext))
            break;

        if (loopVal->integer().hasUnknown())
            iterContext.addDiag(diag::GenvarUnknownBits, genvar.range()) << *loopVal;
    }

    evalContext.reportDiags(iterContext, syntax.sourceRange());

    result->entries = entries.copy(compilation);
    if (!any)
        createBlock(SVInt(32, 0, true), false);

    return *result;
}

void GenerateBlockArraySymbol::toJson(json& j) const {
    j["constructIndex"] = constructIndex;
}

void TimeScaleSymbolBase::setTimeScale(const Scope& scope, const TimeUnitsDeclarationSyntax& syntax,
                                       bool isFirst) {
    bool errored = false;
    auto handle = [&](Token token, optional<SourceRange>& prevRange, TimeScaleValue& value) {
        // If there were syntax errors just bail out, diagnostics have already been issued.
        if (token.isMissing() || token.kind != TokenKind::TimeLiteral)
            return;

        auto val = TimeScaleValue::fromLiteral(token.realValue(), token.numericFlags().unit());
        if (!val) {
            scope.addDiag(diag::InvalidTimeScaleSpecifier, token.location());
            return;
        }

        if (prevRange) {
            // If the value was previously set, we need to make sure this new
            // value is exactly the same, otherwise we error.
            if (value != *val && !errored) {
                auto& diag = scope.addDiag(diag::MismatchedTimeScales, token.range());
                diag.addNote(diag::NotePreviousDefinition, prevRange->start()) << *prevRange;
                errored = true;
            }
        }
        else {
            // The first time scale declarations must be the first elements in the parent scope.
            if (!isFirst && !errored) {
                scope.addDiag(diag::TimeScaleFirstInScope, token.range());
                errored = true;
            }

            value = *val;
            prevRange = token.range();
        }
    };

    if (syntax.keyword.kind == TokenKind::TimeUnitKeyword) {
        handle(syntax.time, unitsRange, timeScale.base);
        if (syntax.divider)
            handle(syntax.divider->value, precisionRange, timeScale.precision);
    }
    else {
        handle(syntax.time, precisionRange, timeScale.precision);
    }
}

void TimeScaleSymbolBase::finalizeTimeScale(const Scope& parentScope,
                                            const ModuleDeclarationSyntax& syntax) {
    // If no time unit was set, infer one based on the following rules:
    // - If the scope is nested (inside another definition), inherit from that definition.
    // - Otherwise use a `timescale directive if there is one.
    // - Otherwise, look for a time unit in the compilation scope.
    // - Finally use the compilation default.
    if (unitsRange && precisionRange)
        return;

    optional<TimeScale> ts;
    auto& comp = parentScope.getCompilation();
    if (parentScope.asSymbol().kind == SymbolKind::CompilationUnit)
        ts = comp.getDirectiveTimeScale(syntax);

    if (!ts)
        ts = parentScope.getTimeScale();

    if (!unitsRange)
        timeScale.base = ts->base;
    if (!precisionRange)
        timeScale.precision = ts->precision;
}

} // namespace slang
