//------------------------------------------------------------------------------
// Parser.h
// SystemVerilog language parser.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#pragma once

#include <flat_hash_map.hpp>

#include "slang/numeric/VectorBuilder.h"
#include "slang/parsing/ParserBase.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/Bag.h"

namespace slang {

class Preprocessor;

namespace detail {

/// Various options for parsing expressions.
enum class ExpressionOptions {
    /// No special options specified.
    None = 0,

    /// Allow pattern matching expressions; these are not allowed recursively so
    /// they're turned off after finding the first one.
    AllowPatternMatch = 1,

    /// In a procedural assignment context, <= is a non-blocking assignment, not the less
    /// than or equal to operator.
    ProceduralAssignmentContext = 2,

    /// In an event expression context, the "or" operator has special meaning.
    EventExpressionContext = 4,

    /// In a constraint block context, the -> operator has special meaning.
    ConstraintContext = 8
};
BITMASK_DEFINE_MAX_ELEMENT(ExpressionOptions, ConstraintContext);

/// Various options for parsing names.
enum class NameOptions {
    /// No special options specified.
    None = 0,

    /// Parsing the name of a foreach variable.
    InForEach = 1,

    /// This is the first element of a potentially dotted name path.
    IsFirst = 2,

    /// The previous element in the name path was the 'this' keyword.
    PreviousWasThis = 4,

    /// We are expecting an expression while parsing this name.
    ExpectingExpression = 8
};
BITMASK_DEFINE_MAX_ELEMENT(NameOptions, ExpectingExpression);

} // namespace detail

/// Contains various options that can control parsing behavior.
struct ParserOptions {
    /// The maximum depth of nested language constructs (statements, exceptions) before
    /// we give up for fear of stack overflow.
    uint32_t maxRecursionDepth = 1024;
};

/// Implements a full syntax parser for SystemVerilog.
class Parser : ParserBase {
public:
    explicit Parser(Preprocessor& preprocessor, const Bag& options = {});

    /// Parse a whole compilation unit.
    CompilationUnitSyntax& parseCompilationUnit();

    /// Parse an expression / statement / module / class / name.
    /// These are mostly for testing; only use if you know that the
    /// source stream is currently looking at one of these.
    ExpressionSyntax& parseExpression();
    StatementSyntax& parseStatement(bool allowEmpty = true);
    ModuleDeclarationSyntax& parseModule();
    ClassDeclarationSyntax& parseClass();
    MemberSyntax* parseMember();
    NameSyntax& parseName();

    /// Generalized node parse function that tries to figure out what we're
    /// looking at and parse that specifically. A normal batch compile won't call
    /// this, since in a well formed program every file is a compilation unit,
    /// but for snippets of code this can be convenient.
    SyntaxNode& parseGuess();

    /// Check whether the parser has consumed the entire input stream.
    bool isDone();

    /// Gets the EndOfFile token, if one has been consumed. Otherwise returns an empty token.
    Token getEOFToken();

    /// Collection of metadata that can be associated with a syntax node at parse time.
    struct NodeMetadata {
        TokenKind defaultNetType;
        optional<TimeScale> timeScale;
    };
    using MetadataMap = flat_hash_map<const SyntaxNode*, NodeMetadata>;

    /// Gets metadata that was in effect when certain syntax nodes were parsed (such as various
    /// bits of preprocessor state).
    MetadataMap&& getMetadataMap() { return std::move(metadataMap); }

private:
    using ExpressionOptions = detail::ExpressionOptions;
    using NameOptions = detail::NameOptions;

    // ---- Recursive-descent parsing routines, by syntax type ----

    ExpressionSyntax& parseMinTypMaxExpression();
    ExpressionSyntax& parsePrimaryExpression();
    ExpressionSyntax& parseIntegerExpression();
    ExpressionSyntax& parseInsideExpression(ExpressionSyntax& expr);
    ExpressionSyntax& parsePostfixExpression(ExpressionSyntax& expr);
    ExpressionSyntax& parseNewExpression(ExpressionSyntax* expr);
    ConcatenationExpressionSyntax& parseConcatenation(Token openBrace, ExpressionSyntax* first);
    StreamingConcatenationExpressionSyntax& parseStreamConcatenation(Token openBrace);
    StreamExpressionSyntax& parseStreamExpression();
    OpenRangeListSyntax& parseOpenRangeList();
    ExpressionSyntax& parseOpenRangeElement();
    ElementSelectSyntax& parseElementSelect();
    SelectorSyntax* parseElementSelector();
    NameSyntax& parseName(bitmask<NameOptions> options);
    NameSyntax& parseNamePart(bitmask<NameOptions> options);
    ParameterValueAssignmentSyntax* parseParameterValueAssignment();
    ArgumentListSyntax& parseArgumentList();
    ArgumentSyntax& parseArgument();
    PatternSyntax& parsePattern();
    AssignmentPatternExpressionSyntax& parseAssignmentPatternExpression(DataTypeSyntax* type);
    AssignmentPatternItemSyntax& parseAssignmentPatternItem(ExpressionSyntax* key);
    EventExpressionSyntax& parseEventExpression();
    NamedBlockClauseSyntax* parseNamedBlockClause();
    TimingControlSyntax* parseTimingControl();
    ConditionalPredicateSyntax& parseConditionalPredicate(ExpressionSyntax& first,
                                                          TokenKind endKind, Token& end);
    ConditionalPatternSyntax& parseConditionalPattern();
    ConditionalStatementSyntax& parseConditionalStatement(NamedLabelSyntax* label,
                                                          span<AttributeInstanceSyntax*> attributes,
                                                          Token uniqueOrPriority);
    ElseClauseSyntax* parseElseClause();
    CaseStatementSyntax& parseCaseStatement(NamedLabelSyntax* label,
                                            span<AttributeInstanceSyntax*> attributes,
                                            Token uniqueOrPriority, Token caseKeyword);
    DefaultCaseItemSyntax& parseDefaultCaseItem();
    LoopStatementSyntax& parseLoopStatement(NamedLabelSyntax* label,
                                            span<AttributeInstanceSyntax*> attributes);
    DoWhileStatementSyntax& parseDoWhileStatement(NamedLabelSyntax* label,
                                                  span<AttributeInstanceSyntax*> attributes);
    ForLoopStatementSyntax& parseForLoopStatement(NamedLabelSyntax* label,
                                                  span<AttributeInstanceSyntax*> attributes);
    SyntaxNode& parseForInitializer();
    ForeachLoopListSyntax& parseForeachLoopVariables();
    ForeachLoopStatementSyntax& parseForeachLoopStatement(
        NamedLabelSyntax* label, span<AttributeInstanceSyntax*> attributes);
    ReturnStatementSyntax& parseReturnStatement(NamedLabelSyntax* label,
                                                span<AttributeInstanceSyntax*> attributes);
    JumpStatementSyntax& parseJumpStatement(NamedLabelSyntax* label,
                                            span<AttributeInstanceSyntax*> attributes);
    ProceduralAssignStatementSyntax& parseProceduralAssignStatement(
        NamedLabelSyntax* label, span<AttributeInstanceSyntax*> attributes, SyntaxKind kind);
    ProceduralDeassignStatementSyntax& parseProceduralDeassignStatement(
        NamedLabelSyntax* label, span<AttributeInstanceSyntax*> attributes, SyntaxKind kind);
    StatementSyntax& parseDisableStatement(NamedLabelSyntax* label,
                                           span<AttributeInstanceSyntax*> attributes);
    StatementSyntax& parseAssertionStatement(NamedLabelSyntax* label,
                                             span<AttributeInstanceSyntax*> attributes);
    ConcurrentAssertionStatementSyntax& parseConcurrentAssertion(
        NamedLabelSyntax* label, span<AttributeInstanceSyntax*> attributes);
    PropertySpecSyntax& parsePropertySpec();
    ActionBlockSyntax& parseActionBlock();
    BlockStatementSyntax& parseBlock(SyntaxKind blockKind, TokenKind endKind,
                                     NamedLabelSyntax* label,
                                     span<AttributeInstanceSyntax*> attributes);
    StatementSyntax& parseWaitStatement(NamedLabelSyntax* label,
                                        span<AttributeInstanceSyntax*> attributes);
    WaitOrderStatementSyntax& parseWaitOrderStatement(NamedLabelSyntax* label,
                                                      span<AttributeInstanceSyntax*> attributes);
    RandCaseStatementSyntax& parseRandCaseStatement(NamedLabelSyntax* label,
                                                    span<AttributeInstanceSyntax*> attributes);
    EventTriggerStatementSyntax& parseEventTriggerStatement(
        NamedLabelSyntax* label, span<AttributeInstanceSyntax*> attributes);
    Token parseSigning();
    VariableDimensionSyntax* parseDimension();
    span<VariableDimensionSyntax*> parseDimensionList();
    StructUnionTypeSyntax& parseStructUnion(SyntaxKind syntaxKind);
    EnumTypeSyntax& parseEnum();
    DataTypeSyntax& parseDataType(bool allowImplicit);
    DotMemberClauseSyntax* parseDotMemberClause();
    span<AttributeInstanceSyntax*> parseAttributes();
    AttributeSpecSyntax& parseAttributeSpec();
    ModuleHeaderSyntax& parseModuleHeader();
    ParameterPortListSyntax* parseParameterPortList();
    ModuleDeclarationSyntax& parseModule(span<AttributeInstanceSyntax*> attributes);
    MemberSyntax& parseModportSubroutinePortList(span<AttributeInstanceSyntax*> attributes);
    MemberSyntax& parseModportPort();
    ModportItemSyntax& parseModportItem();
    ModportDeclarationSyntax& parseModportDeclaration(span<AttributeInstanceSyntax*> attributes);
    PortReferenceSyntax& parsePortReference();
    PortExpressionSyntax& parsePortExpression();
    NonAnsiPortSyntax& parseNonAnsiPort();
    MemberSyntax& parseAnsiPort();
    AnsiPortListSyntax& parseAnsiPortList(Token openParen);
    PortHeaderSyntax& parsePortHeader(Token direction);
    PortDeclarationSyntax& parsePortDeclaration(span<AttributeInstanceSyntax*> attributes);
    TimeUnitsDeclarationSyntax& parseTimeUnitsDeclaration(
        span<AttributeInstanceSyntax*> attributes);
    span<PackageImportDeclarationSyntax*> parsePackageImports();
    PackageImportDeclarationSyntax& parseImportDeclaration(
        span<AttributeInstanceSyntax*> attributes);
    PackageImportItemSyntax& parsePackageImportItem();
    NetTypeDeclarationSyntax& parseNetTypeDecl(span<AttributeInstanceSyntax*> attributes);
    DPIImportExportSyntax& parseDPIImportExport(span<AttributeInstanceSyntax*> attributes);
    AssertionItemPortListSyntax* parseAssertionItemPortList(TokenKind declarationKind);
    PropertyDeclarationSyntax& parsePropertyDeclaration(span<AttributeInstanceSyntax*> attributes);
    SequenceDeclarationSyntax& parseSequenceDeclaration(span<AttributeInstanceSyntax*> attributes);
    ParameterDeclarationBaseSyntax& parseParameterPort();
    ParameterDeclarationBaseSyntax& parseParameterDecl(Token keyword, Token* semi);
    TypeAssignmentSyntax& parseTypeAssignment();
    ClockingSkewSyntax* parseClockingSkew();
    ClockingDeclarationSyntax& parseClockingDeclaration(span<AttributeInstanceSyntax*> attributes);
    MemberSyntax& parseVariableDeclaration(span<AttributeInstanceSyntax*> attributes);
    MemberSyntax& parseNetDeclaration(span<AttributeInstanceSyntax*> attributes);
    HierarchyInstantiationSyntax& parseHierarchyInstantiation(
        span<AttributeInstanceSyntax*> attributes);
    HierarchicalInstanceSyntax& parseHierarchicalInstance();
    PortConnectionSyntax& parsePortConnection();
    FunctionPortSyntax& parseFunctionPort();
    FunctionPrototypeSyntax& parseFunctionPrototype(bool allowTasks = true);
    FunctionDeclarationSyntax& parseFunctionDeclaration(span<AttributeInstanceSyntax*> attributes,
                                                        SyntaxKind functionKind, TokenKind endKind);
    Token parseLifetime();
    span<SyntaxNode*> parseBlockItems(TokenKind endKind, Token& end);
    GenvarDeclarationSyntax& parseGenvarDeclaration(span<AttributeInstanceSyntax*> attributes);
    LoopGenerateSyntax& parseLoopGenerateConstruct(span<AttributeInstanceSyntax*> attributes);
    IfGenerateSyntax& parseIfGenerateConstruct(span<AttributeInstanceSyntax*> attributes);
    CaseGenerateSyntax& parseCaseGenerateConstruct(span<AttributeInstanceSyntax*> attributes);
    MemberSyntax& parseGenerateBlock();
    ImplementsClauseSyntax* parseImplementsClause(TokenKind keywordKind, Token& semi);
    ClassDeclarationSyntax& parseClassDeclaration(span<AttributeInstanceSyntax*> attributes,
                                                  Token virtualOrInterface);
    MemberSyntax* parseClassMember();
    ContinuousAssignSyntax& parseContinuousAssign(span<AttributeInstanceSyntax*> attributes);
    DeclaratorSyntax& parseDeclarator();
    span<TokenOrSyntax> parseOneDeclarator();
    MemberSyntax* parseCoverageMember();
    BlockEventExpressionSyntax& parseBlockEventExpression();
    WithClauseSyntax* parseWithClause();
    CovergroupDeclarationSyntax& parseCovergroupDeclaration(
        span<AttributeInstanceSyntax*> attributes);
    CoverpointSyntax* parseCoverpoint(span<AttributeInstanceSyntax*> attributes,
                                      DataTypeSyntax* type, NamedLabelSyntax* label);
    CoverageOptionSyntax* parseCoverageOption(span<AttributeInstanceSyntax*> attributes);
    MemberSyntax* parseCoverpointMember();
    MemberSyntax& parseConstraint(span<AttributeInstanceSyntax*> attributes,
                                  span<Token> qualifiers);
    ConstraintBlockSyntax& parseConstraintBlock();
    ConstraintItemSyntax* parseConstraintItem(bool allowBlock);
    DistConstraintListSyntax& parseDistConstraintList();
    DistItemSyntax& parseDistItem();
    ExpressionSyntax& parseArrayOrRandomizeMethod(ExpressionSyntax& expr);
    DefParamAssignmentSyntax& parseDefParamAssignment();
    DefParamSyntax& parseDefParam(span<AttributeInstanceSyntax*> attributes);
    ExpressionSyntax& parseExpressionOrDist();
    TransRangeSyntax& parseTransRange();
    TransSetSyntax& parseTransSet();
    TransListCoverageBinInitializerSyntax& parseTransListInitializer();
    ExpressionSyntax& parseSubExpression(bitmask<ExpressionOptions> options, int precedence);
    ExpressionSyntax& parsePrefixExpression(bitmask<ExpressionOptions> options, SyntaxKind opKind);

    template<bool (*IsEnd)(TokenKind)>
    span<TokenOrSyntax> parseDeclarators(TokenKind endKind, Token& end);
    span<TokenOrSyntax> parseDeclarators(Token& semi);

    template<typename TMember, typename TParseFunc>
    span<TMember*> parseMemberList(TokenKind endKind, Token& endToken, TParseFunc&& parseFunc);

    // ---- Lookahead routines, for determining which kind of syntax to parse ----

    bool isPortDeclaration();
    bool isNetDeclaration();
    bool isVariableDeclaration();
    bool isHierarchyInstantiation();
    bool isNonAnsiPort();
    bool isPlainPortName();
    bool scanDimensionList(uint32_t& index);
    bool scanQualifiedName(uint32_t& index);

    template<bool (*IsEnd)(TokenKind)>
    bool scanTypePart(uint32_t& index, TokenKind start, TokenKind end);

    // ---- Stack recursion error detection ----

    class DepthGuard {
    public:
        DepthGuard(Parser& _parser) : parser(_parser) {
            if (++parser.recursionDepth > parser.parseOptions.maxRecursionDepth)
                parser.handleTooDeep();
        }
        ~DepthGuard() { --parser.recursionDepth; }

    private:
        Parser& parser;
    };
    DepthGuard setDepthGuard() { return DepthGuard(*this); }
    void handleTooDeep();

    class RecursionException : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    // ---- Various helper methods ----

    // Reports an error if there are attributes in the given span.
    void errorIfAttributes(span<AttributeInstanceSyntax*> attributes);

    // Handle splitting out an exponent from a token that was otherwise miscategorized by the lexer.
    void handleExponentSplit(Token token, size_t offset);

    // ---- Member variables ----

    // The factory used to create new syntax nodes.
    SyntaxFactory factory;

    // Stored parse options.
    ParserOptions parseOptions;

    // Map of metadata for previously parsed nodes.
    MetadataMap metadataMap;

    // Scratch space for building up integer vector literals.
    VectorBuilder vectorBuilder;

    // The current depth of recursion in the parser.
    size_t recursionDepth = 0;

    // The held EOF token, if we've encountered it.
    Token eofToken;
};

template<bool (*IsEnd)(TokenKind)>
bool Parser::scanTypePart(uint32_t& index, TokenKind start, TokenKind end) {
    int nesting = 1;
    while (true) {
        auto kind = peek(index).kind;
        if (IsEnd(kind) || kind == TokenKind::EndOfFile)
            return false;

        index++;
        if (kind == start)
            nesting++;
        else if (kind == end) {
            nesting--;
            if (nesting <= 0)
                return true;
        }
    }
}

} // namespace slang
