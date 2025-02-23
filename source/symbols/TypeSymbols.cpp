//------------------------------------------------------------------------------
// TypeSymbols.cpp
// Contains type-related symbol definitions.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#include "slang/symbols/TypeSymbols.h"

#include <nlohmann/json.hpp>

#include "slang/binding/ConstantValue.h"
#include "slang/compilation/Compilation.h"
#include "slang/diagnostics/LookupDiags.h"
#include "slang/diagnostics/TypesDiags.h"
#include "slang/symbols/ASTVisitor.h"
#include "slang/symbols/TypePrinter.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/StackContainer.h"

namespace {

using namespace slang;

// clang-format off
bitwidth_t getWidth(PredefinedIntegerType::Kind kind) {
    switch (kind) {
        case PredefinedIntegerType::ShortInt: return 16;
        case PredefinedIntegerType::Int: return 32;
        case PredefinedIntegerType::LongInt: return 64;
        case PredefinedIntegerType::Byte: return 8;
        case PredefinedIntegerType::Integer: return 32;
        case PredefinedIntegerType::Time: return 64;
        default: THROW_UNREACHABLE;
    }
}

bool getSigned(PredefinedIntegerType::Kind kind) {
    switch (kind) {
        case PredefinedIntegerType::ShortInt: return true;
        case PredefinedIntegerType::Int: return true;
        case PredefinedIntegerType::LongInt: return true;
        case PredefinedIntegerType::Byte: return true;
        case PredefinedIntegerType::Integer: return true;
        case PredefinedIntegerType::Time: return false;
        default: THROW_UNREACHABLE;
    }
}

bool getFourState(PredefinedIntegerType::Kind kind) {
    switch (kind) {
        case PredefinedIntegerType::ShortInt: return false;
        case PredefinedIntegerType::Int: return false;
        case PredefinedIntegerType::LongInt: return false;
        case PredefinedIntegerType::Byte: return false;
        case PredefinedIntegerType::Integer: return true;
        case PredefinedIntegerType::Time: return true;
        default: THROW_UNREACHABLE;
    }
}
// clang-format on

struct GetDefaultVisitor {
    template<typename T>
    using getDefault_t = decltype(std::declval<T>().getDefaultValueImpl());

    template<typename T>
    ConstantValue visit([[maybe_unused]] const T& type) {
        if constexpr (is_detected_v<getDefault_t, T>) {
            return type.getDefaultValueImpl();
        }
        else {
            THROW_UNREACHABLE;
        }
    }
};

const Type& getPredefinedType(Compilation& compilation, SyntaxKind kind, bool isSigned) {
    auto& predef = compilation.getType(kind).as<IntegralType>();
    if (isSigned == predef.isSigned)
        return predef;

    auto flags = predef.getIntegralFlags();
    if (isSigned)
        flags |= IntegralFlags::Signed;
    else
        flags &= ~IntegralFlags::Signed;

    return compilation.getType(predef.bitWidth, flags);
}

} // namespace

namespace slang {

const ErrorType ErrorType::Instance;

bitwidth_t Type::getBitWidth() const {
    const Type& ct = getCanonicalType();
    if (ct.isIntegral())
        return ct.as<IntegralType>().bitWidth;

    if (ct.isFloating()) {
        switch (ct.as<FloatingType>().floatKind) {
            case FloatingType::Real:
                return 64;
            case FloatingType::RealTime:
                return 64;
            case FloatingType::ShortReal:
                return 32;
            default:
                THROW_UNREACHABLE;
        }
    }
    return 0;
}

bool Type::isSigned() const {
    const Type& ct = getCanonicalType();
    return ct.isIntegral() && ct.as<IntegralType>().isSigned;
}

bool Type::isFourState() const {
    const Type& ct = getCanonicalType();
    if (ct.isIntegral())
        return ct.as<IntegralType>().isFourState;

    switch (ct.kind) {
        case SymbolKind::UnpackedArrayType:
            return ct.as<UnpackedArrayType>().elementType.isFourState();
        case SymbolKind::UnpackedStructType: {
            auto& us = ct.as<UnpackedStructType>();
            for (auto& field : us.membersOfType<FieldSymbol>()) {
                if (field.getType().isFourState())
                    return true;
            }
            return false;
        }
        case SymbolKind::UnpackedUnionType: {
            auto& us = ct.as<UnpackedUnionType>();
            for (auto& field : us.membersOfType<FieldSymbol>()) {
                if (field.getType().isFourState())
                    return true;
            }
            return false;
        }
        default:
            return false;
    }
}

bool Type::isIntegral() const {
    const Type& ct = getCanonicalType();
    return IntegralType::isKind(ct.kind);
}

bool Type::isAggregate() const {
    switch (getCanonicalType().kind) {
        case SymbolKind::UnpackedArrayType:
        case SymbolKind::UnpackedStructType:
        case SymbolKind::UnpackedUnionType:
            return true;
        default:
            return false;
    }
}

bool Type::isSimpleBitVector() const {
    const Type& ct = getCanonicalType();
    if (ct.isPredefinedInteger() || ct.isScalar())
        return true;

    return ct.kind == SymbolKind::PackedArrayType &&
           ct.as<PackedArrayType>().elementType.isScalar();
}

bool Type::isBooleanConvertible() const {
    switch (getCanonicalType().kind) {
        case SymbolKind::NullType:
        case SymbolKind::CHandleType:
        case SymbolKind::StringType:
        case SymbolKind::EventType:
            return true;
        default:
            return isNumeric();
    }
}

bool Type::isArray() const {
    const Type& ct = getCanonicalType();
    switch (ct.kind) {
        case SymbolKind::PackedArrayType:
        case SymbolKind::UnpackedArrayType:
            return true;
        default:
            return false;
    }
}

bool Type::isStruct() const {
    const Type& ct = getCanonicalType();
    switch (ct.kind) {
        case SymbolKind::PackedStructType:
        case SymbolKind::UnpackedStructType:
            return true;
        default:
            return false;
    }
}

bool Type::isBitstreamType() const {
    // TODO: dynamic types, classes
    return isIntegral() || isUnpackedArray() || isUnpackedStruct();
}

bool Type::isByteArray() const {
    const Type& ct = getCanonicalType();
    if (!ct.isUnpackedArray())
        return false;

    auto& elem = ct.as<UnpackedArrayType>().elementType.getCanonicalType();
    return elem.isPredefinedInteger() &&
           elem.as<PredefinedIntegerType>().integerKind == PredefinedIntegerType::Byte;
}

bool Type::isMatching(const Type& rhs) const {
    // See [6.22.1] for Matching Types.
    const Type* l = &getCanonicalType();
    const Type* r = &rhs.getCanonicalType();

    // If the two types have the same address, they are literally the same type.
    // This handles all built-in types, which are allocated once and then shared,
    // and also handles simple bit vector types that share the same range, signedness,
    // and four-stateness because we uniquify them in the compilation cache.
    // This handles checks [6.22.1] (a), (b), (c), (d), (g), and (h).
    if (l == r || (l->getSyntax() && l->getSyntax() == r->getSyntax()))
        return true;

    // Special casing for type synonyms: logic/reg
    if (l->isScalar() && r->isScalar()) {
        auto ls = l->as<ScalarType>().scalarKind;
        auto rs = r->as<ScalarType>().scalarKind;
        return (ls == ScalarType::Logic || ls == ScalarType::Reg) &&
               (rs == ScalarType::Logic || rs == ScalarType::Reg);
    }

    // Special casing for type synonyms: real/realtime
    if (l->isFloating() && r->isFloating()) {
        auto lf = l->as<FloatingType>().floatKind;
        auto rf = r->as<FloatingType>().floatKind;
        return (lf == FloatingType::Real || lf == FloatingType::RealTime) &&
               (rf == FloatingType::Real || rf == FloatingType::RealTime);
    }

    // Handle check (e) and (f): matching predefined integers and matching vector types
    if (l->isSimpleBitVector() && r->isSimpleBitVector() &&
        l->isPredefinedInteger() != r->isPredefinedInteger()) {
        auto& li = l->as<IntegralType>();
        auto& ri = r->as<IntegralType>();
        return li.isSigned == ri.isSigned && li.isFourState == ri.isFourState &&
               li.getBitVectorRange() == ri.getBitVectorRange();
    }

    // Handle check (f): matching array types
    if (l->kind == SymbolKind::PackedArrayType && r->kind == SymbolKind::PackedArrayType) {
        auto& la = l->as<PackedArrayType>();
        auto& ra = r->as<PackedArrayType>();
        return la.range == ra.range && la.elementType.isMatching(ra.elementType);
    }
    if (l->kind == SymbolKind::UnpackedArrayType && r->kind == SymbolKind::UnpackedArrayType) {
        auto& la = l->as<UnpackedArrayType>();
        auto& ra = r->as<UnpackedArrayType>();
        return la.range == ra.range && la.elementType.isMatching(ra.elementType);
    }

    return false;
}

bool Type::isEquivalent(const Type& rhs) const {
    // See [6.22.2] for Equivalent Types
    const Type* l = &getCanonicalType();
    const Type* r = &rhs.getCanonicalType();
    if (l->isMatching(*r))
        return true;

    if (l->isIntegral() && r->isIntegral() && !l->isEnum() && !r->isEnum()) {
        const auto& li = l->as<IntegralType>();
        const auto& ri = r->as<IntegralType>();
        return li.isSigned == ri.isSigned && li.isFourState == ri.isFourState &&
               li.bitWidth == ri.bitWidth;
    }

    if (l->kind == SymbolKind::UnpackedArrayType && r->kind == SymbolKind::UnpackedArrayType) {
        auto& la = l->as<UnpackedArrayType>();
        auto& ra = r->as<UnpackedArrayType>();
        return la.range.width() == ra.range.width() && la.elementType.isEquivalent(ra.elementType);
    }

    return false;
}

bool Type::isAssignmentCompatible(const Type& rhs) const {
    // See [6.22.3] for Assignment Compatible
    const Type* l = &getCanonicalType();
    const Type* r = &rhs.getCanonicalType();
    if (l->isEquivalent(*r))
        return true;

    // Any integral or floating value can be implicitly converted to a packed integer
    // value or to a floating value.
    if ((l->isIntegral() && !l->isEnum()) || l->isFloating())
        return r->isIntegral() || r->isFloating();

    return false;
}

bool Type::isCastCompatible(const Type& rhs) const {
    // See [6.22.4] for Cast Compatible
    const Type* l = &getCanonicalType();
    const Type* r = &rhs.getCanonicalType();
    if (l->isAssignmentCompatible(*r))
        return true;

    if (l->isEnum())
        return r->isIntegral() || r->isFloating();

    if (l->isString())
        return r->isIntegral();

    if (r->isString())
        return l->isIntegral();

    return false;
}

bitmask<IntegralFlags> Type::getIntegralFlags() const {
    bitmask<IntegralFlags> flags;
    if (!isIntegral())
        return flags;

    const IntegralType& it = getCanonicalType().as<IntegralType>();
    if (it.isSigned)
        flags |= IntegralFlags::Signed;
    if (it.isFourState)
        flags |= IntegralFlags::FourState;
    if (it.isDeclaredReg())
        flags |= IntegralFlags::Reg;

    return flags;
}

ConstantValue Type::getDefaultValue() const {
    GetDefaultVisitor visitor;
    return visit(visitor);
}

ConstantRange Type::getArrayRange() const {
    const Type& t = getCanonicalType();
    if (t.isIntegral())
        return t.as<IntegralType>().getBitVectorRange();

    if (t.isUnpackedArray())
        return t.as<UnpackedArrayType>().range;

    return {};
}

std::string Type::toString() const {
    TypePrinter printer;
    printer.append(*this);
    return printer.toString();
}

const Type& Type::fromSyntax(Compilation& compilation, const DataTypeSyntax& node,
                             LookupLocation location, const Scope& parent, bool forceSigned) {
    switch (node.kind) {
        case SyntaxKind::BitType:
        case SyntaxKind::LogicType:
        case SyntaxKind::RegType:
            return IntegralType::fromSyntax(compilation, node.as<IntegerTypeSyntax>(), location,
                                            parent, forceSigned);
        case SyntaxKind::ByteType:
        case SyntaxKind::ShortIntType:
        case SyntaxKind::IntType:
        case SyntaxKind::LongIntType:
        case SyntaxKind::IntegerType:
        case SyntaxKind::TimeType: {
            auto& its = node.as<IntegerTypeSyntax>();
            if (!its.dimensions.empty()) {
                // Error but don't fail out; just remove the dims and keep trucking
                auto& diag = parent.addDiag(diag::PackedDimsOnPredefinedType,
                                            its.dimensions[0]->openBracket.location());
                diag << getTokenKindText(its.keyword.kind);
            }

            if (!its.signing)
                return compilation.getType(node.kind);

            return getPredefinedType(compilation, node.kind,
                                     its.signing.kind == TokenKind::SignedKeyword);
        }
        case SyntaxKind::RealType:
        case SyntaxKind::RealTimeType:
        case SyntaxKind::ShortRealType:
        case SyntaxKind::StringType:
        case SyntaxKind::CHandleType:
        case SyntaxKind::EventType:
        case SyntaxKind::VoidType:
            return compilation.getType(node.kind);
        case SyntaxKind::EnumType:
            return EnumType::fromSyntax(compilation, node.as<EnumTypeSyntax>(), location, parent,
                                        forceSigned);
        case SyntaxKind::StructType: {
            const auto& structUnion = node.as<StructUnionTypeSyntax>();
            return structUnion.packed ? PackedStructType::fromSyntax(compilation, structUnion,
                                                                     location, parent, forceSigned)
                                      : UnpackedStructType::fromSyntax(compilation, structUnion);
        }
        case SyntaxKind::UnionType: {
            const auto& structUnion = node.as<StructUnionTypeSyntax>();
            return structUnion.packed ? PackedUnionType::fromSyntax(compilation, structUnion,
                                                                    location, parent, forceSigned)
                                      : UnpackedUnionType::fromSyntax(compilation, structUnion);
        }
        case SyntaxKind::NamedType:
            return lookupNamedType(compilation, *node.as<NamedTypeSyntax>().name, location, parent);
        case SyntaxKind::ImplicitType: {
            auto& implicit = node.as<ImplicitTypeSyntax>();
            return IntegralType::fromSyntax(
                compilation, SyntaxKind::LogicType, implicit.dimensions,
                implicit.signing.kind == TokenKind::SignedKeyword || forceSigned, location, parent);
        }
        case SyntaxKind::PropertyType:
        case SyntaxKind::SequenceType:
        case SyntaxKind::TypeReference:
        case SyntaxKind::TypeType:
        case SyntaxKind::Untyped:
        case SyntaxKind::VirtualInterfaceType:
            parent.addDiag(diag::NotYetSupported, node.sourceRange());
            return compilation.getErrorType();
        default:
            THROW_UNREACHABLE;
    }
}

bool Type::isKind(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::PredefinedIntegerType:
        case SymbolKind::ScalarType:
        case SymbolKind::FloatingType:
        case SymbolKind::EnumType:
        case SymbolKind::PackedArrayType:
        case SymbolKind::UnpackedArrayType:
        case SymbolKind::PackedStructType:
        case SymbolKind::UnpackedStructType:
        case SymbolKind::PackedUnionType:
        case SymbolKind::UnpackedUnionType:
        case SymbolKind::ClassType:
        case SymbolKind::VoidType:
        case SymbolKind::NullType:
        case SymbolKind::CHandleType:
        case SymbolKind::StringType:
        case SymbolKind::EventType:
        case SymbolKind::TypeAlias:
        case SymbolKind::ErrorType:
            return true;
        default:
            return false;
    }
}

void Type::resolveCanonical() const {
    ASSERT(kind == SymbolKind::TypeAlias);
    canonical = this;
    do {
        canonical = &canonical->as<TypeAliasType>().targetType.getType();
    } while (canonical->isAlias());
}

const Type& Type::lookupNamedType(Compilation& compilation, const NameSyntax& syntax,
                                  LookupLocation location, const Scope& parent) {
    LookupResult result;
    parent.lookupName(syntax, location, LookupFlags::Type, result);

    if (result.hasError())
        compilation.addDiagnostics(result.getDiagnostics());

    return fromLookupResult(compilation, result, syntax, location, parent);
}

const Type& Type::fromLookupResult(Compilation& compilation, const LookupResult& result,
                                   const NameSyntax& syntax, LookupLocation location,
                                   const Scope& parent) {
    const Symbol* symbol = result.found;
    if (!symbol)
        return compilation.getErrorType();

    if (!symbol->isType()) {
        parent.addDiag(diag::NotAType, syntax.sourceRange()) << symbol->name;
        return compilation.getErrorType();
    }

    BindContext context(parent, location);

    const Type* finalType = &symbol->as<Type>();
    size_t count = result.selectors.size();
    for (size_t i = 0; i < count; i++) {
        // TODO: handle dotted selectors
        auto selectSyntax = std::get<const ElementSelectSyntax*>(result.selectors[count - i - 1]);
        auto dim = context.evalPackedDimension(*selectSyntax);
        if (!dim)
            return compilation.getErrorType();

        finalType = &PackedArrayType::fromSyntax(compilation, *finalType, *dim, *selectSyntax);
    }

    return *finalType;
}

IntegralType::IntegralType(SymbolKind kind, string_view name, SourceLocation loc,
                           bitwidth_t bitWidth_, bool isSigned_, bool isFourState_) :
    Type(kind, name, loc),
    bitWidth(bitWidth_), isSigned(isSigned_), isFourState(isFourState_) {
}

bool IntegralType::isKind(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::PredefinedIntegerType:
        case SymbolKind::ScalarType:
        case SymbolKind::EnumType:
        case SymbolKind::PackedArrayType:
        case SymbolKind::PackedStructType:
        case SymbolKind::PackedUnionType:
            return true;
        default:
            return false;
    }
}

ConstantRange IntegralType::getBitVectorRange() const {
    if (isPredefinedInteger() || isScalar() || kind == SymbolKind::PackedStructType ||
        kind == SymbolKind::PackedUnionType) {

        return { int32_t(bitWidth - 1), 0 };
    }

    return as<PackedArrayType>().range;
}

bool IntegralType::isDeclaredReg() const {
    const Type* type = this;
    while (type->kind == SymbolKind::PackedArrayType)
        type = &type->as<PackedArrayType>().elementType.getCanonicalType();

    if (type->isScalar())
        return type->as<ScalarType>().scalarKind == ScalarType::Reg;

    return false;
}

const Type& IntegralType::fromSyntax(Compilation& compilation, SyntaxKind integerKind,
                                     span<const VariableDimensionSyntax* const> dimensions,
                                     bool isSigned, LookupLocation location, const Scope& scope) {
    // This is a simple integral vector (possibly of just one element).
    BindContext context(scope, location);
    SmallVectorSized<std::pair<ConstantRange, const SyntaxNode*>, 4> dims;
    for (auto dimSyntax : dimensions) {
        auto dim = context.evalPackedDimension(*dimSyntax);
        if (!dim)
            return compilation.getErrorType();

        dims.emplace(*dim, dimSyntax);
    }

    if (dims.empty())
        return getPredefinedType(compilation, integerKind, isSigned);

    bitmask<IntegralFlags> flags;
    if (integerKind == SyntaxKind::RegType)
        flags |= IntegralFlags::Reg;
    if (isSigned)
        flags |= IntegralFlags::Signed;
    if (integerKind != SyntaxKind::BitType)
        flags |= IntegralFlags::FourState;

    if (dims.size() == 1 && dims[0].first.right == 0) {
        // if we have the common case of only one dimension and lsb == 0
        // then we can use the shared representation
        return compilation.getType(dims[0].first.width(), flags);
    }

    const Type* result = &compilation.getScalarType(flags);
    size_t count = dims.size();
    for (size_t i = 0; i < count; i++) {
        auto& pair = dims[count - i - 1];
        result = &PackedArrayType::fromSyntax(compilation, *result, pair.first, *pair.second);
    }

    return *result;
}

const Type& IntegralType::fromSyntax(Compilation& compilation, const IntegerTypeSyntax& syntax,
                                     LookupLocation location, const Scope& scope,
                                     bool forceSigned) {
    return fromSyntax(compilation, syntax.kind, syntax.dimensions,
                      syntax.signing.kind == TokenKind::SignedKeyword || forceSigned, location,
                      scope);
}

ConstantValue IntegralType::getDefaultValueImpl() const {
    if (isEnum())
        return as<EnumType>().baseType.getDefaultValue();

    if (isFourState)
        return SVInt::createFillX(bitWidth, isSigned);
    else
        return SVInt(bitWidth, 0, isSigned);
}

PredefinedIntegerType::PredefinedIntegerType(Kind integerKind) :
    PredefinedIntegerType(integerKind, getSigned(integerKind)) {
}

PredefinedIntegerType::PredefinedIntegerType(Kind integerKind, bool isSigned) :
    IntegralType(SymbolKind::PredefinedIntegerType, "", SourceLocation(), getWidth(integerKind),
                 isSigned, getFourState(integerKind)),
    integerKind(integerKind) {
}

bool PredefinedIntegerType::isDefaultSigned(Kind integerKind) {
    return getSigned(integerKind);
}

ScalarType::ScalarType(Kind scalarKind) : ScalarType(scalarKind, false) {
}

ScalarType::ScalarType(Kind scalarKind, bool isSigned) :
    IntegralType(SymbolKind::ScalarType, "", SourceLocation(), 1, isSigned,
                 scalarKind != Kind::Bit),
    scalarKind(scalarKind) {
}

FloatingType::FloatingType(Kind floatKind_) :
    Type(SymbolKind::FloatingType, "", SourceLocation()), floatKind(floatKind_) {
}

ConstantValue FloatingType::getDefaultValueImpl() const {
    if (floatKind == ShortReal)
        return shortreal_t(0.0f);

    return real_t(0.0);
}

EnumType::EnumType(Compilation& compilation, SourceLocation loc, const Type& baseType_,
                   LookupLocation lookupLocation) :
    IntegralType(SymbolKind::EnumType, "", loc, baseType_.getBitWidth(), baseType_.isSigned(),
                 baseType_.isFourState()),
    Scope(compilation, this), baseType(baseType_) {

    // Enum types don't live as members of the parent scope (they're "owned" by the declaration
    // containing them) but we hook up the parent pointer so that it can participate in name
    // lookups.
    auto scope = lookupLocation.getScope();
    ASSERT(scope);
    setParent(*scope, lookupLocation.getIndex());
}

const Type& EnumType::fromSyntax(Compilation& compilation, const EnumTypeSyntax& syntax,
                                 LookupLocation location, const Scope& scope, bool forceSigned) {
    const Type* base;
    const Type* cb;
    if (!syntax.baseType) {
        // If no explicit base type is specified we default to an int.
        base = &compilation.getIntType();
        cb = base;
    }
    else {
        base = &compilation.getType(*syntax.baseType, location, scope, forceSigned);
        cb = &base->getCanonicalType();
        if (!cb->isError() && !cb->isSimpleBitVector()) {
            scope.addDiag(diag::InvalidEnumBase, syntax.baseType->getFirstToken().location())
                << *base;
            cb = &compilation.getErrorType();
        }
    }

    SVInt allOnes(cb->getBitWidth(), 0, cb->isSigned());
    allOnes.setAllOnes();

    SVInt one(cb->getBitWidth(), 1, cb->isSigned());
    SVInt previous;
    SourceRange previousRange;
    bool first = true;

    auto resultType =
        compilation.emplace<EnumType>(compilation, syntax.keyword.location(), *base, location);
    resultType->setSyntax(syntax);

    // Enum values must be unique; this set and lambda are used to check that.
    SmallMap<SVInt, SourceLocation, 8> usedValues;
    auto checkValue = [&usedValues, &scope](const SVInt& value, SourceLocation loc) {
        auto pair = usedValues.emplace(value, loc);
        if (!pair.second) {
            auto& diag = scope.addDiag(diag::EnumValueDuplicate, loc) << value;
            diag.addNote(diag::NotePreviousDefinition, pair.first->second);
            return false;
        }
        return true;
    };

    // For enumerands that have an initializer, set it up appropriately.
    auto setInitializer = [&](EnumValueSymbol& ev, const EqualsValueClauseSyntax& initializer) {
        ev.setInitializerSyntax(*initializer.expr, initializer.equals.location());
        auto& cv = ev.getConstantValue();
        if (!cv)
            return;

        first = false;
        previous = cv.integer();
        previousRange = ev.getInitializer()->sourceRange;

        checkValue(previous, previousRange.start());
    };

    // For enumerands that have no initializer, infer the value via this function.
    auto inferValue = [&](EnumValueSymbol& ev, SourceRange range) {
        auto loc = range.start();
        SVInt value;
        if (first) {
            value = SVInt(cb->getBitWidth(), 0, cb->isSigned());
            first = false;
        }
        else if (previous.hasUnknown()) {
            auto& diag = scope.addDiag(diag::EnumIncrementUnknown, loc);
            diag << previous << *base << previousRange;
            return;
        }
        else if (previous == allOnes) {
            auto& diag = scope.addDiag(diag::EnumValueOverflow, loc);
            diag << previous << *base << previousRange;
            return;
        }
        else {
            value = previous + one;
        }

        if (!checkValue(value, loc))
            return;

        ev.setValue(value);
        previous = std::move(value);
        previousRange = range;
    };

    BindContext context(scope, location);

    for (auto member : syntax.members) {
        if (member->dimensions.empty()) {
            auto& ev = EnumValueSymbol::fromSyntax(compilation, *member, *resultType, std::nullopt);
            resultType->addMember(ev);

            if (member->initializer)
                setInitializer(ev, *member->initializer);
            else
                inferValue(ev, member->sourceRange());
        }
        else {
            if (member->dimensions.size() > 1) {
                scope.addDiag(diag::EnumRangeMultiDimensional, member->dimensions.sourceRange());
                return compilation.getErrorType();
            }

            auto range = context.evalUnpackedDimension(*member->dimensions[0]);
            if (!range)
                return compilation.getErrorType();

            // Range must be positive.
            if (!context.requirePositive(range->left, member->dimensions[0]->sourceRange()) ||
                !context.requirePositive(range->right, member->dimensions[0]->sourceRange())) {
                return compilation.getErrorType();
            }

            // Set up the first element using the initializer. All other elements (if there are any)
            // don't get the initializer.
            int32_t index = range->left;
            {
                auto& ev = EnumValueSymbol::fromSyntax(compilation, *member, *resultType, index);
                resultType->addMember(ev);

                if (member->initializer)
                    setInitializer(ev, *member->initializer);
                else
                    inferValue(ev, member->sourceRange());
            }

            bool down = range->isLittleEndian();
            while (index != range->right) {
                index = down ? index - 1 : index + 1;

                auto& ev = EnumValueSymbol::fromSyntax(compilation, *member, *resultType, index);
                resultType->addMember(ev);

                inferValue(ev, member->sourceRange());
            }
        }
    }

    return *resultType;
}

EnumValueSymbol::EnumValueSymbol(string_view name, SourceLocation loc) :
    ValueSymbol(SymbolKind::EnumValue, name, loc, DeclaredTypeFlags::RequireConstant) {
}

EnumValueSymbol& EnumValueSymbol::fromSyntax(Compilation& compilation,
                                             const DeclaratorSyntax& syntax, const Type& type,
                                             optional<int32_t> index) {
    string_view name = syntax.name.valueText();
    if (index && !name.empty()) {
        ASSERT(*index >= 0);

        size_t sz = (size_t)snprintf(nullptr, 0, "%d", *index);
        char* mem = (char*)compilation.allocate(sz + name.size() + 1, 1);
        memcpy(mem, name.data(), name.size());
        snprintf(mem + name.size(), sz + 1, "%d", *index);

        name = string_view(mem, sz + name.size());
    }

    auto ev = compilation.emplace<EnumValueSymbol>(name, syntax.name.location());
    ev->setType(type);
    ev->setSyntax(syntax);

    return *ev;
}

const ConstantValue& EnumValueSymbol::getValue() const {
    return value ? *value : getConstantValue();
}

void EnumValueSymbol::setValue(ConstantValue newValue) {
    auto scope = getParentScope();
    ASSERT(scope);
    value = scope->getCompilation().allocConstant(std::move(newValue));
}

void EnumValueSymbol::toJson(json& j) const {
    if (value)
        j["value"] = *value;
}

PackedArrayType::PackedArrayType(const Type& elementType, ConstantRange range) :
    IntegralType(SymbolKind::PackedArrayType, "", SourceLocation(),
                 elementType.getBitWidth() * range.width(), elementType.isSigned(),
                 elementType.isFourState()),
    elementType(elementType), range(range) {
}

const Type& PackedArrayType::fromSyntax(Compilation& compilation, const Type& elementType,
                                        ConstantRange range, const SyntaxNode& syntax) {
    if (elementType.isError())
        return elementType;

    // TODO: check bitwidth of array
    // TODO: ensure integral
    auto result = compilation.emplace<PackedArrayType>(elementType, range);
    result->setSyntax(syntax);
    return *result;
}

UnpackedArrayType::UnpackedArrayType(const Type& elementType, ConstantRange range) :
    Type(SymbolKind::UnpackedArrayType, "", SourceLocation()), elementType(elementType),
    range(range) {
}

const Type& UnpackedArrayType::fromSyntax(Compilation& compilation, const Type& elementType,
                                          LookupLocation location, const Scope& scope,
                                          const SyntaxList<VariableDimensionSyntax>& dimensions) {
    if (elementType.isError())
        return elementType;

    BindContext context(scope, location);

    const Type* result = &elementType;
    size_t count = dimensions.size();
    for (size_t i = 0; i < count; i++) {
        // TODO: handle other kinds of unpacked arrays
        EvaluatedDimension dim = context.evalDimension(*dimensions[count - i - 1], true);
        if (!dim.isRange())
            return compilation.getErrorType();

        auto unpacked = compilation.emplace<UnpackedArrayType>(*result, dim.range);
        unpacked->setSyntax(*dimensions[count - i - 1]);
        result = unpacked;
    }

    return *result;
}

ConstantValue UnpackedArrayType::getDefaultValueImpl() const {
    return std::vector<ConstantValue>(range.width(), elementType.getDefaultValue());
}

void FieldSymbol::toJson(json& j) const {
    VariableSymbol::toJson(j);
    j["offset"] = offset;
}

PackedStructType::PackedStructType(Compilation& compilation, bitwidth_t bitWidth, bool isSigned,
                                   bool isFourState) :
    IntegralType(SymbolKind::PackedStructType, "", SourceLocation(), bitWidth, isSigned,
                 isFourState),
    Scope(compilation, this) {
}

const Type& PackedStructType::fromSyntax(Compilation& compilation,
                                         const StructUnionTypeSyntax& syntax,
                                         LookupLocation location, const Scope& scope,
                                         bool forceSigned) {
    ASSERT(syntax.packed);
    bool isSigned = syntax.signing.kind == TokenKind::SignedKeyword || forceSigned;
    bool isFourState = false;
    bitwidth_t bitWidth = 0;

    // We have to look at all the members up front to know our width and four-statedness.
    // We have to iterate in reverse because members are specified from MSB to LSB order.
    SmallVectorSized<const Symbol*, 8> members;
    for (auto member : make_reverse_range(syntax.members)) {
        const Type& type = compilation.getType(*member->type, location, scope);
        isFourState |= type.isFourState();

        bool issuedError = false;
        if (!type.isIntegral() && !type.isError()) {
            issuedError = true;
            auto& diag = scope.addDiag(diag::PackedMemberNotIntegral,
                                       member->type->getFirstToken().location());
            diag << type;
            diag << member->type->sourceRange();
        }

        for (auto decl : member->declarators) {
            auto variable = compilation.emplace<FieldSymbol>(decl->name.valueText(),
                                                             decl->name.location(), bitWidth);
            variable->setType(type);
            variable->setSyntax(*decl);
            compilation.addAttributes(*variable, member->attributes);
            members.append(variable);

            // Unpacked arrays are disallowed in packed structs.
            if (const Type& dimType = compilation.getType(type, decl->dimensions, location, scope);
                dimType.isUnpackedArray() && !issuedError) {

                auto& diag = scope.addDiag(diag::PackedMemberNotIntegral, decl->name.range());
                diag << dimType;
                diag << decl->dimensions.sourceRange();
                issuedError = true;
            }

            bitWidth += type.getBitWidth();

            if (decl->initializer) {
                auto& diag = scope.addDiag(diag::PackedMemberHasInitializer,
                                           decl->initializer->equals.location());
                diag << decl->initializer->expr->sourceRange();
            }
        }
    }

    // TODO: cannot be empty
    if (!bitWidth)
        return compilation.getErrorType();

    auto structType =
        compilation.emplace<PackedStructType>(compilation, bitWidth, isSigned, isFourState);
    for (auto member : make_reverse_range(members))
        structType->addMember(*member);

    structType->setSyntax(syntax);

    const Type* result = structType;
    BindContext context(scope, location);

    size_t count = syntax.dimensions.size();
    for (size_t i = 0; i < count; i++) {
        auto& dimSyntax = *syntax.dimensions[count - i - 1];
        auto dim = context.evalPackedDimension(dimSyntax);
        if (!dim)
            return compilation.getErrorType();

        result = &PackedArrayType::fromSyntax(compilation, *result, *dim, dimSyntax);
    }

    return *result;
}

UnpackedStructType::UnpackedStructType(Compilation& compilation) :
    Type(SymbolKind::UnpackedStructType, "", SourceLocation()), Scope(compilation, this) {
}

ConstantValue UnpackedStructType::getDefaultValueImpl() const {
    std::vector<ConstantValue> elements;
    for (auto& field : membersOfType<FieldSymbol>())
        elements.emplace_back(field.getType().getDefaultValue());

    return elements;
}

const Type& UnpackedStructType::fromSyntax(Compilation& compilation,
                                           const StructUnionTypeSyntax& syntax) {
    ASSERT(!syntax.packed);

    uint32_t fieldIndex = 0;
    auto result = compilation.emplace<UnpackedStructType>(compilation);
    for (auto member : syntax.members) {
        for (auto decl : member->declarators) {
            auto variable = compilation.emplace<FieldSymbol>(decl->name.valueText(),
                                                             decl->name.location(), fieldIndex);
            variable->setDeclaredType(*member->type);
            variable->setFromDeclarator(*decl);
            compilation.addAttributes(*variable, member->attributes);

            result->addMember(*variable);
            fieldIndex++;
        }
    }

    // TODO: error if dimensions
    // TODO: error if signing
    // TODO: check for void types
    // TODO: cannot be empty

    result->setSyntax(syntax);
    return *result;
}

PackedUnionType::PackedUnionType(Compilation& compilation, bitwidth_t bitWidth, bool isSigned,
                                 bool isFourState) :
    IntegralType(SymbolKind::PackedUnionType, "", SourceLocation(), bitWidth, isSigned,
                 isFourState),
    Scope(compilation, this) {
}

const Type& PackedUnionType::fromSyntax(Compilation& compilation,
                                        const StructUnionTypeSyntax& syntax,
                                        LookupLocation location, const Scope& scope,
                                        bool forceSigned) {
    ASSERT(syntax.packed);
    bool isSigned = syntax.signing.kind == TokenKind::SignedKeyword || forceSigned;
    bool isFourState = false;
    bitwidth_t bitWidth = 0;

    // We have to look at all the members up front to know our width and four-statedness.
    SmallVectorSized<const Symbol*, 8> members;
    for (auto member : syntax.members) {
        const Type& type = compilation.getType(*member->type, location, scope);
        isFourState |= type.isFourState();

        bool issuedError = false;
        if (!type.isIntegral() && !type.isError()) {
            issuedError = true;
            auto& diag = scope.addDiag(diag::PackedMemberNotIntegral,
                                       member->type->getFirstToken().location());
            diag << type;
            diag << member->type->sourceRange();
        }

        for (auto decl : member->declarators) {
            auto variable =
                compilation.emplace<FieldSymbol>(decl->name.valueText(), decl->name.location(), 0u);
            variable->setType(type);
            variable->setSyntax(*decl);
            compilation.addAttributes(*variable, member->attributes);
            members.append(variable);

            // Unpacked arrays are disallowed in packed unions.
            if (const Type& dimType = compilation.getType(type, decl->dimensions, location, scope);
                dimType.isUnpackedArray() && !issuedError) {

                auto& diag = scope.addDiag(diag::PackedMemberNotIntegral, decl->name.range());
                diag << dimType;
                diag << decl->dimensions.sourceRange();
                issuedError = true;
            }

            if (!bitWidth)
                bitWidth = type.getBitWidth();
            else if (bitWidth != type.getBitWidth() && !issuedError) {
                scope.addDiag(diag::PackedUnionWidthMismatch, decl->name.range());
                issuedError = true;
            }

            if (decl->initializer) {
                auto& diag = scope.addDiag(diag::PackedMemberHasInitializer,
                                           decl->initializer->equals.location());
                diag << decl->initializer->expr->sourceRange();
            }
        }
    }

    // TODO: cannot be empty
    if (!bitWidth)
        return compilation.getErrorType();

    auto unionType =
        compilation.emplace<PackedUnionType>(compilation, bitWidth, isSigned, isFourState);
    for (auto member : members)
        unionType->addMember(*member);

    unionType->setSyntax(syntax);

    const Type* result = unionType;
    BindContext context(scope, location);

    size_t count = syntax.dimensions.size();
    for (size_t i = 0; i < count; i++) {
        auto& dimSyntax = *syntax.dimensions[count - i - 1];
        auto dim = context.evalPackedDimension(dimSyntax);
        if (!dim)
            return compilation.getErrorType();

        result = &PackedArrayType::fromSyntax(compilation, *result, *dim, dimSyntax);
    }

    return *result;
}

UnpackedUnionType::UnpackedUnionType(Compilation& compilation) :
    Type(SymbolKind::UnpackedUnionType, "", SourceLocation()), Scope(compilation, this) {
}

ConstantValue UnpackedUnionType::getDefaultValueImpl() const {
    auto range = membersOfType<FieldSymbol>();
    auto it = range.begin();
    if (it == range.end())
        return nullptr;

    return it->getType().getDefaultValue();
}

const Type& UnpackedUnionType::fromSyntax(Compilation& compilation,
                                          const StructUnionTypeSyntax& syntax) {
    ASSERT(!syntax.packed);

    auto result = compilation.emplace<UnpackedUnionType>(compilation);
    for (auto member : syntax.members) {
        for (auto decl : member->declarators) {
            auto variable =
                compilation.emplace<FieldSymbol>(decl->name.valueText(), decl->name.location(), 0u);
            variable->setDeclaredType(*member->type);
            variable->setFromDeclarator(*decl);
            compilation.addAttributes(*variable, member->attributes);

            result->addMember(*variable);
        }
    }

    // TODO: error if dimensions
    // TODO: error if signing
    // TODO: check for void types
    // TODO: cannot be empty

    result->setSyntax(syntax);
    return *result;
}

ConstantValue NullType::getDefaultValueImpl() const {
    return ConstantValue::NullPlaceholder{};
}

ConstantValue CHandleType::getDefaultValueImpl() const {
    return ConstantValue::NullPlaceholder{};
}

ConstantValue StringType::getDefaultValueImpl() const {
    return ""s;
}

ConstantValue EventType::getDefaultValueImpl() const {
    return ConstantValue::NullPlaceholder{};
}

const ForwardingTypedefSymbol& ForwardingTypedefSymbol::fromSyntax(
    Compilation& compilation, const ForwardTypedefDeclarationSyntax& syntax) {

    Category category;
    switch (syntax.keyword.kind) {
        case TokenKind::EnumKeyword:
            category = Category::Enum;
            break;
        case TokenKind::StructKeyword:
            category = Category::Struct;
            break;
        case TokenKind::UnionKeyword:
            category = Category::Union;
            break;
        case TokenKind::ClassKeyword:
            category = Category::Class;
            break;
        default:
            category = Category::None;
            break;
    }

    auto result = compilation.emplace<ForwardingTypedefSymbol>(syntax.name.valueText(),
                                                               syntax.name.location(), category);
    result->setSyntax(syntax);
    compilation.addAttributes(*result, syntax.attributes);
    return *result;
}

const ForwardingTypedefSymbol& ForwardingTypedefSymbol::fromSyntax(
    Compilation& compilation, const ForwardInterfaceClassTypedefDeclarationSyntax& syntax) {

    auto result = compilation.emplace<ForwardingTypedefSymbol>(
        syntax.name.valueText(), syntax.name.location(), Category::InterfaceClass);
    result->setSyntax(syntax);
    compilation.addAttributes(*result, syntax.attributes);
    return *result;
}

void ForwardingTypedefSymbol::addForwardDecl(const ForwardingTypedefSymbol& decl) const {
    if (!next)
        next = &decl;
    else
        next->addForwardDecl(decl);
}

void ForwardingTypedefSymbol::toJson(json& j) const {
    j["category"] = toString(category);
    if (next)
        j["next"] = *next;
}

const TypeAliasType& TypeAliasType::fromSyntax(Compilation& compilation,
                                               const TypedefDeclarationSyntax& syntax) {
    // TODO: interface based typedefs
    // TODO: unpacked dimensions
    auto result =
        compilation.emplace<TypeAliasType>(syntax.name.valueText(), syntax.name.location());
    result->targetType.setTypeSyntax(*syntax.type);
    result->setSyntax(syntax);
    compilation.addAttributes(*result, syntax.attributes);
    return *result;
}

void TypeAliasType::addForwardDecl(const ForwardingTypedefSymbol& decl) const {
    if (!firstForward)
        firstForward = &decl;
    else
        firstForward->addForwardDecl(decl);
}

void TypeAliasType::checkForwardDecls() const {
    ForwardingTypedefSymbol::Category category;
    switch (targetType.getType().kind) {
        case SymbolKind::PackedStructType:
        case SymbolKind::UnpackedStructType:
            category = ForwardingTypedefSymbol::Struct;
            break;
        case SymbolKind::PackedUnionType:
        case SymbolKind::UnpackedUnionType:
            category = ForwardingTypedefSymbol::Union;
            break;
        case SymbolKind::EnumType:
            category = ForwardingTypedefSymbol::Enum;
            break;
        default:
            // TODO:
            return;
    }

    const ForwardingTypedefSymbol* forward = firstForward;
    while (forward) {
        if (forward->category != ForwardingTypedefSymbol::None && forward->category != category) {
            auto& diag =
                getParentScope()->addDiag(diag::ForwardTypedefDoesNotMatch, forward->location);
            switch (forward->category) {
                case ForwardingTypedefSymbol::Enum:
                    diag << "enum"sv;
                    break;
                case ForwardingTypedefSymbol::Struct:
                    diag << "struct"sv;
                    break;
                case ForwardingTypedefSymbol::Union:
                    diag << "union"sv;
                    break;
                case ForwardingTypedefSymbol::Class:
                    diag << "class"sv;
                    break;
                case ForwardingTypedefSymbol::InterfaceClass:
                    diag << "interface class"sv;
                    break;
                default:
                    THROW_UNREACHABLE;
            }
            diag.addNote(diag::NoteDeclarationHere, location);
            return;
        }
        forward = forward->getNextForwardDecl();
    }
}

ConstantValue TypeAliasType::getDefaultValueImpl() const {
    return targetType.getType().getDefaultValue();
}

void TypeAliasType::toJson(json& j) const {
    j["target"] = targetType.getType();
    if (firstForward)
        j["forward"] = *firstForward;
}

NetType::NetType(NetKind netKind, string_view name, const Type& dataType) :
    Symbol(SymbolKind::NetType, name, SourceLocation()), netKind(netKind), declaredType(*this),
    isResolved(true) {

    declaredType.setType(dataType);
}

NetType::NetType(string_view name, SourceLocation location) :
    Symbol(SymbolKind::NetType, name, location), netKind(UserDefined), declaredType(*this) {
}

const NetType* NetType::getAliasTarget() const {
    if (!isResolved)
        resolve();
    return alias;
}

const NetType& NetType::getCanonical() const {
    if (auto target = getAliasTarget())
        return target->getCanonical();
    return *this;
}

const Type& NetType::getDataType() const {
    if (!isResolved)
        resolve();
    return declaredType.getType();
}

const SubroutineSymbol* NetType::getResolutionFunction() const {
    if (!isResolved)
        resolve();
    return resolver;
}

void NetType::toJson(json& j) const {
    j["type"] = getDataType();
    if (auto target = getAliasTarget())
        j["target"] = *target;
}

NetType& NetType::fromSyntax(Compilation& compilation, const NetTypeDeclarationSyntax& syntax) {
    auto result = compilation.emplace<NetType>(syntax.name.valueText(), syntax.name.location());
    result->setSyntax(syntax);
    compilation.addAttributes(*result, syntax.attributes);

    // If this is an enum, make sure the declared type is set up before we get added to
    // any scope, so that the enum members get picked up correctly.
    if (syntax.type->kind == SyntaxKind::EnumType)
        result->declaredType.setTypeSyntax(*syntax.type);

    return *result;
}

void NetType::resolve() const {
    ASSERT(!isResolved);
    isResolved = true;

    auto syntaxNode = getSyntax();
    ASSERT(syntaxNode);

    auto scope = getParentScope();
    ASSERT(scope);

    auto& declSyntax = syntaxNode->as<NetTypeDeclarationSyntax>();
    if (declSyntax.withFunction) {
        // TODO: lookup and validate the function here
    }

    // If this is an enum, we already set the type earlier.
    if (declSyntax.type->kind == SyntaxKind::EnumType)
        return;

    // Our type syntax is either a link to another net type we are aliasing, or an actual
    // data type that we are using as the basis for a custom net type.
    if (declSyntax.type->kind == SyntaxKind::NamedType) {
        LookupResult result;
        const NameSyntax& nameSyntax = *declSyntax.type->as<NamedTypeSyntax>().name;
        scope->lookupName(nameSyntax, LookupLocation::before(*this), LookupFlags::Type, result);

        if (result.found && result.found->kind == SymbolKind::NetType) {
            if (result.hasError())
                scope->getCompilation().addDiagnostics(result.getDiagnostics());

            alias = &result.found->as<NetType>();
            declaredType.copyTypeFrom(alias->getCanonical().declaredType);
            return;
        }
    }

    declaredType.setTypeSyntax(*declSyntax.type);
}

} // namespace slang
