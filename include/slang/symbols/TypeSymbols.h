//------------------------------------------------------------------------------
// TypeSymbols.h
// Type-related symbol definitions.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#pragma once

#include "slang/binding/ConstantValue.h"
#include "slang/symbols/MemberSymbols.h"
#include "slang/symbols/Scope.h"
#include "slang/symbols/Symbol.h"

namespace slang {

class Compilation;

/// Specifies possible traits for integral types.
enum class IntegralFlags : uint8_t {
    /// The type is unsigned. This is the default.
    Unsigned = 0,

    /// The type is two state. This is the default.
    TwoState = 0,

    /// The type is signed.
    Signed = 1,

    /// The type is four state.
    FourState = 2,

    /// The type used the 'reg' keyword instead of 'logic'; they are
    /// semantically identical but preserve the distinction to allow
    /// more useful messaging.
    Reg = 4
};
BITMASK_DEFINE_MAX_ELEMENT(IntegralFlags, Reg);

/// Base class for all data types in SystemVerilog.
///
/// Note that this can actually be an alias for some other type (such as with typedefs or
/// type parameters). Each type knows its "canonical" type, which in the case of most types
/// points to itself and for type aliases points to the fully unwrapped target type. Most
/// methods on this class that query traits drill down to the canonical type.
///
class Type : public Symbol {
public:
    /// Gets the canonical type for this type, which involves unwrapping any type aliases.
    const Type& getCanonicalType() const {
        if (!canonical)
            resolveCanonical();
        return *canonical;
    }

    /// Gets the total width of the type in bits. Returns zero if the type does not have a
    /// statically known size.
    bitwidth_t getBitWidth() const;

    /// Indicates whether the type can represent negative numeric values. For non-numeric types,
    /// this always returns false.
    bool isSigned() const;

    /// Indicates whether the type can represent unknown and high impedance numeric values.
    /// For aggregate types, this drills down into submembers to determine whether they are
    /// all two state or if some are four state. For all other types, this always returns false.
    bool isFourState() const;

    /// Indicates whether this is an aggregate type, which includes all unpacked structs, unions,
    /// and arrays.
    bool isAggregate() const;

    /// Indicates whether this is a singular type, which is the opposite of an aggregate type (that
    /// is, all types except unpacked structs, unions, and arrays).
    bool isSingular() const { return !isAggregate(); }

    /// Indicates whether this is an integral type, which includes all scalar types, predefined
    /// integer types, packed arrays, packed structures, packed unions, and enum types.
    bool isIntegral() const;

    /// Indicates whether this is a scalar integral type (bit, logic, or reg).
    bool isScalar() const { return getCanonicalType().kind == SymbolKind::ScalarType; }

    /// Indicates whether this is a predefined integer type.
    bool isPredefinedInteger() const {
        return getCanonicalType().kind == SymbolKind::PredefinedIntegerType;
    }

    /// Indicates whether this is a simple bit vector type, which encompasses all predefined integer
    /// types as well as scalar and vector types.
    bool isSimpleBitVector() const;

    /// Indicates whether this type is convertible to a boolean predicate for use in
    /// a conditional expression.
    bool isBooleanConvertible() const;

    /// Indicates whether this is a packed or unpacked array.
    bool isArray() const;

    /// Indicates whether this is a packed or unpacked struct.
    bool isStruct() const;

    /// Indicates whether this type can be packed into a stream of bits.
    bool isBitstreamType() const;

    /// Indicates whether this type is an unpacked array of bytes. Various string-related
    /// methods in the language check for this to interpret such arguments as strings.
    bool isByteArray() const;

    /// Indicates whether this is a numeric type, which includes all integral and floating types.
    bool isNumeric() const { return isIntegral() || isFloating(); }

    /// Indicates whether this is a packed array type.
    bool isPackedArray() const { return getCanonicalType().kind == SymbolKind::PackedArrayType; }

    /// Indicates whether this is an unpacked array type.
    bool isUnpackedArray() const {
        return getCanonicalType().kind == SymbolKind::UnpackedArrayType;
    }

    /// Indicates whether this is an unpacked structure type.
    bool isUnpackedStruct() const {
        return getCanonicalType().kind == SymbolKind::UnpackedStructType;
    }

    /// Indicates whether this is an unpacked union type.
    bool isUnpackedUnion() const {
        return getCanonicalType().kind == SymbolKind::UnpackedUnionType;
    }

    /// Indicates whether this is an enum type.
    bool isEnum() const { return getCanonicalType().kind == SymbolKind::EnumType; }

    /// Indicates whether this is a class type.
    bool isClass() const { return getCanonicalType().kind == SymbolKind::ClassType; }

    /// Indicates whether this is a floating point type.
    bool isFloating() const { return getCanonicalType().kind == SymbolKind::FloatingType; }

    /// Indicates whether this is the Void type.
    bool isVoid() const { return getCanonicalType().kind == SymbolKind::VoidType; }

    /// Indicates whether this is the null type.
    bool isNull() const { return getCanonicalType().kind == SymbolKind::NullType; }

    /// Indicates whether this is a C-handle type.
    bool isCHandle() const { return getCanonicalType().kind == SymbolKind::CHandleType; }

    /// Indicates whether this is a string type.
    bool isString() const { return getCanonicalType().kind == SymbolKind::StringType; }

    /// Indicates whether this is an event type.
    bool isEvent() const { return getCanonicalType().kind == SymbolKind::EventType; }

    /// Indicates whether this is a type alias.
    /// Note that unlike other methods, this one does not unwrap to the canonical type.
    bool isAlias() const { return kind == SymbolKind::TypeAlias; }

    /// Indicates whether this is the error type.
    bool isError() const { return getCanonicalType().kind == SymbolKind::ErrorType; }

    /// Determines whether the given type "matches" this one. For most intents
    /// and purposes, matching types are completely identical.
    bool isMatching(const Type& rhs) const;

    /// Determines whether the given type is "equivalent" to this one. This
    /// typically means that the two types can be implicitly converted between
    /// one another.
    bool isEquivalent(const Type& rhs) const;

    /// Determines whether the given type is "assignment compatible" to this one.
    /// This includes all equivalent types, plus types for which additional
    /// implicit conversion rules have been defined. Note that the
    /// reverse operation is not necessarily true.
    bool isAssignmentCompatible(const Type& rhs) const;

    /// Determines whether the given type is "cast compatible" to this one. This
    /// means that the type is either implicitly or explicitly convertible to
    /// this one. Note that the reverse operation is not necessarily true.
    bool isCastCompatible(const Type& rhs) const;

    /// Gets a combination of flags for integral types; for non-integral types,
    /// this returns all zeros.
    bitmask<IntegralFlags> getIntegralFlags() const;

    /// Gets the default value for the type. An uninitialized variable of this
    /// type will have the given default value.
    ConstantValue getDefaultValue() const;

    /// Returns the bounds of the array, if this is an array type.
    /// Otherwise returns an empty range.
    ConstantRange getArrayRange() const;

    std::string toString() const;

    static const Type& fromSyntax(Compilation& compilation, const DataTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);

    /// Constructs a type from the results of a lookup operation. Note that this will
    /// not issue any diagnostics from the result object; the caller must do that
    /// themselves if they wish.
    static const Type& fromLookupResult(Compilation& compilation, const LookupResult& result,
                                        const NameSyntax& syntax, LookupLocation location,
                                        const Scope& parent);

    static bool isKind(SymbolKind kind);

protected:
    Type(SymbolKind kind, string_view name, SourceLocation loc) :
        Symbol(kind, name, loc), canonical(this) {}

    mutable const Type* canonical;

private:
    void resolveCanonical() const;

    static const Type& lookupNamedType(Compilation& compilation, const NameSyntax& syntax,
                                       LookupLocation location, const Scope& parent);
};

struct IntegerTypeSyntax;

/// A base class for integral types, which include all scalar types, predefined integer types,
/// packed arrays, packed structures, packed unions, and enum types.
class IntegralType : public Type {
public:
    /// The total width of the type in bits.
    bitwidth_t bitWidth;

    /// Indicates whether or not the integer participates in signed arithmetic.
    bool isSigned;

    /// Indicates whether the integer is composed of 4-state bits or 2-state bits.
    bool isFourState;

    /// If this is a simple bit vector type, returns the address range of
    /// the bits in the vector. Otherwise the behavior is undefined (will assert).
    ConstantRange getBitVectorRange() const;

    /// Indicates whether the underlying type was declared using the 'reg' keyword.
    bool isDeclaredReg() const;

    static const Type& fromSyntax(Compilation& compilation, const IntegerTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);

    static const Type& fromSyntax(Compilation& compilation, SyntaxKind integerKind,
                                  span<const VariableDimensionSyntax* const> dimensions,
                                  bool isSigned, LookupLocation location, const Scope& scope);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind);

protected:
    IntegralType(SymbolKind kind, string_view name, SourceLocation loc, bitwidth_t bitWidth,
                 bool isSigned, bool isFourState);
};

/// Represents the single-bit scalar types.
class ScalarType : public IntegralType {
public:
    enum Kind { Bit, Logic, Reg } scalarKind;

    ScalarType(Kind scalarKind);
    ScalarType(Kind scalarKind, bool isSigned);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::ScalarType; }
};

/// Represents the predefined integer types, which are essentially predefined vector types.
class PredefinedIntegerType : public IntegralType {
public:
    enum Kind { ShortInt, Int, LongInt, Byte, Integer, Time } integerKind;

    PredefinedIntegerType(Kind integerKind);
    PredefinedIntegerType(Kind integerKind, bool isSigned);

    static bool isDefaultSigned(Kind integerKind);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PredefinedIntegerType; }
};

/// Represents one of the predefined floating point types, which are used for representing real
/// numbers.
class FloatingType : public Type {
public:
    enum Kind { Real, ShortReal, RealTime } floatKind;

    explicit FloatingType(Kind floatKind);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::FloatingType; }
};

class EnumValueSymbol;
struct EnumTypeSyntax;

/// Represents an enumerated type.
class EnumType : public IntegralType, public Scope {
public:
    const Type& baseType;

    EnumType(Compilation& compilation, SourceLocation loc, const Type& baseType,
             LookupLocation lookupLocation);

    static const Type& fromSyntax(Compilation& compilation, const EnumTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);
    static bool isKind(SymbolKind kind) { return kind == SymbolKind::EnumType; }

    iterator_range<specific_symbol_iterator<EnumValueSymbol>> values() const {
        return membersOfType<EnumValueSymbol>();
    }
};

/// Represents an enumerated value / member.
class EnumValueSymbol : public ValueSymbol {
public:
    EnumValueSymbol(string_view name, SourceLocation loc);

    const ConstantValue& getValue() const;
    void setValue(ConstantValue value);

    void toJson(json& j) const;

    static EnumValueSymbol& fromSyntax(Compilation& compilation, const DeclaratorSyntax& syntax,
                                       const Type& type, optional<int32_t> index);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::EnumValue; }

private:
    const ConstantValue* value = nullptr;
};

/// Represents a packed array of some simple element type (vectors, packed structures, other packed
/// arrays).
class PackedArrayType : public IntegralType {
public:
    const Type& elementType;
    ConstantRange range;

    PackedArrayType(const Type& elementType, ConstantRange range);

    static const Type& fromSyntax(Compilation& compilation, const Type& elementType,
                                  ConstantRange range, const SyntaxNode& syntax);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PackedArrayType; }
};

/// Represents an unpacked array of some other type.
class UnpackedArrayType : public Type {
public:
    const Type& elementType;
    ConstantRange range;

    UnpackedArrayType(const Type& elementType, ConstantRange range);

    static const Type& fromSyntax(Compilation& compilation, const Type& elementType,
                                  LookupLocation location, const Scope& scope,
                                  const SyntaxList<VariableDimensionSyntax>& dimensions);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::UnpackedArrayType; }
};

/// Represents a field member of a struct or union.
class FieldSymbol : public VariableSymbol {
public:
    /// The offset of the field within its parent structure or union. If the parent type is
    /// packed, this is an offset in bits. Otherwise it's an index into the list of fields.
    uint32_t offset;

    FieldSymbol(string_view name, SourceLocation loc, uint32_t offset) :
        VariableSymbol(SymbolKind::Field, name, loc, VariableLifetime::Automatic), offset(offset) {}

    void toJson(json& j) const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::Field; }
};

struct StructUnionTypeSyntax;

/// Represents a packed structure of members.
class PackedStructType : public IntegralType, public Scope {
public:
    PackedStructType(Compilation& compilation, bitwidth_t bitWidth, bool isSigned,
                     bool isFourState);

    static const Type& fromSyntax(Compilation& compilation, const StructUnionTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PackedStructType; }
};

/// Represents an unpacked structure of members.
class UnpackedStructType : public Type, public Scope {
public:
    explicit UnpackedStructType(Compilation& compilation);

    static const Type& fromSyntax(Compilation& compilation, const StructUnionTypeSyntax& syntax);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::UnpackedStructType; }
};

/// Represents a packed union of members.
class PackedUnionType : public IntegralType, public Scope {
public:
    PackedUnionType(Compilation& compilation, bitwidth_t bitWidth, bool isSigned, bool isFourState);

    static const Type& fromSyntax(Compilation& compilation, const StructUnionTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PackedUnionType; }
};

/// Represents an unpacked union of members.
class UnpackedUnionType : public Type, public Scope {
public:
    explicit UnpackedUnionType(Compilation& compilation);

    static const Type& fromSyntax(Compilation& compilation, const StructUnionTypeSyntax& syntax);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::UnpackedUnionType; }
};

/// Represents the Void (or lack of a) type. This can be used as the return type of functions
/// and as the type of members in tagged unions.
class VoidType : public Type {
public:
    VoidType() : Type(SymbolKind::VoidType, "void", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const { return nullptr; }

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::VoidType; }
};

/// Represents the Null type. This can be used as a literal for setting class handles and
/// chandles to null (or the default value).
class NullType : public Type {
public:
    NullType() : Type(SymbolKind::NullType, "null", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::NullType; }
};

/// Represents storage for pointers passed using the DPI (a "C" compatible handle).
class CHandleType : public Type {
public:
    CHandleType() : Type(SymbolKind::CHandleType, "chandle", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::CHandleType; }
};

/// Represents an ASCII string type.
class StringType : public Type {
public:
    StringType() : Type(SymbolKind::StringType, "string", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::StringType; }
};

/// Represents a SystemVerilog event handle, which is used for synchronization between
/// asynchronous processes.
class EventType : public Type {
public:
    EventType() : Type(SymbolKind::EventType, "event", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::EventType; }
};

struct ForwardInterfaceClassTypedefDeclarationSyntax;
struct ForwardTypedefDeclarationSyntax;

/// A forward declaration of a user-defined type name. A given type name can have
/// an arbitrary number of forward declarations in the same scope, so each symbol
/// forms a linked list, headed by the actual type definition.
class ForwardingTypedefSymbol : public Symbol {
public:
#define CATEGORY(x) x(None) x(Enum) x(Struct) x(Union) x(Class) x(InterfaceClass)
    ENUM_MEMBER(Category, CATEGORY);
#undef CATEGORY

    Category category;

    ForwardingTypedefSymbol(string_view name, SourceLocation loc, Category category) :
        Symbol(SymbolKind::ForwardingTypedef, name, loc), category(category) {}

    static const ForwardingTypedefSymbol& fromSyntax(Compilation& compilation,
                                                     const ForwardTypedefDeclarationSyntax& syntax);
    static const ForwardingTypedefSymbol& fromSyntax(
        Compilation& compilation, const ForwardInterfaceClassTypedefDeclarationSyntax& syntax);

    void addForwardDecl(const ForwardingTypedefSymbol& decl) const;
    const ForwardingTypedefSymbol* getNextForwardDecl() const { return next; }

    void toJson(json& j) const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::ForwardingTypedef; }

private:
    mutable const ForwardingTypedefSymbol* next = nullptr;
};

struct TypedefDeclarationSyntax;

/// Represents a type alias, which is introduced via a typedef or type parameter.
class TypeAliasType : public Type {
public:
    DeclaredType targetType;

    TypeAliasType(string_view name, SourceLocation loc) :
        Type(SymbolKind::TypeAlias, name, loc), targetType(*this) {
        canonical = nullptr;
    }

    static const TypeAliasType& fromSyntax(Compilation& compilation,
                                           const TypedefDeclarationSyntax& syntax);

    void addForwardDecl(const ForwardingTypedefSymbol& decl) const;
    const ForwardingTypedefSymbol* getFirstForwardDecl() const { return firstForward; }

    /// Checks all forward declarations for validity when considering the target type
    /// of this alias. Any inconsistencies will issue diagnostics.
    void checkForwardDecls() const;

    ConstantValue getDefaultValueImpl() const;

    void toJson(json& j) const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::TypeAlias; }

private:
    friend class TypeParameterSymbol;

    mutable const ForwardingTypedefSymbol* firstForward = nullptr;
};

/// An empty type symbol that indicates an error occurred while trying to
/// resolve the type of some expression or declaration.
class ErrorType : public Type {
public:
    ErrorType() : Type(SymbolKind::ErrorType, "", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const { return nullptr; }

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::ErrorType; }

    static const ErrorType Instance;
};

struct NetTypeDeclarationSyntax;

/// Base class for all net types in SystemVerilog.
///
/// There is a parallel type system for nets that exists independently from the data type
/// system. Most nets will be one of the built in types, but user defined net types can
/// exist too.
///
class NetType : public Symbol {
public:
    enum NetKind {
        Unknown,
        Wire,
        WAnd,
        WOr,
        Tri,
        TriAnd,
        TriOr,
        Tri0,
        Tri1,
        TriReg,
        Supply0,
        Supply1,
        UWire,
        UserDefined
    } netKind;

    NetType(NetKind netKind, string_view name, const Type& dataType);
    NetType(string_view name, SourceLocation location);

    /// If this net type is an alias, gets the target of the alias. Otherwise returns nullptr.
    const NetType* getAliasTarget() const;

    /// Gets the canonical net type for this net type, which involves unwrapping any aliases.
    const NetType& getCanonical() const;

    /// Gets the data type for nets of this particular net type.
    const Type& getDataType() const;

    /// Gets the custom resolution function for this net type, if it has one.
    const SubroutineSymbol* getResolutionFunction() const;

    bool isError() const { return netKind == Unknown; }
    bool isBuiltIn() const { return netKind != UserDefined; }

    void toJson(json& j) const;

    static NetType& fromSyntax(Compilation& compilation, const NetTypeDeclarationSyntax& syntax);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::NetType; }

private:
    friend class Symbol;

    void resolve() const;

    mutable DeclaredType declaredType;

    mutable const NetType* alias = nullptr;
    mutable const SubroutineSymbol* resolver = nullptr;
    mutable bool isResolved = false;
};

} // namespace slang
