# Grammar Reference
[<- Index](index.md)
This page presents the complete K language grammar in one place.
Each production links to the reference section that explains it in detail.
Rule names are hyperlinked: usages in right-hand sides link to the rule's own definition on this page.
Grammar notation used here:
- `{` x `}` -- zero or more occurrences of x
- `[` x `]` -- zero or one occurrence of x
- x `|` y -- alternative: x or y
- `'token'` -- literal token
- `(one of)` -- one of the listed alternatives
---
## Lexical grammar
*Full description:* [Lexical Conventions](basic/lexical.md)
<a id="lineterminator"></a>**LineTerminator:**
    LF | CR | CR LF
<a id="whitespace"></a>**WhiteSpace:**
    SP | HT | FF | [LineTerminator](#lineterminator)
<a id="comment"></a>**Comment:**
    `'/*'` CommentTail
    | `'//'` {{ InputCharacter }}
<a id="token"></a>**Token:**
    [Identifier](#identifier) | [Keyword](#keyword) | [Literal](#literal) | [Punctuator](#punctuator) | [Operator](#operator)
<a id="identifier"></a>**Identifier:**
    [IdentifierLetter](#identifierletter) {{ [IdentifierLetterOrDigit](#identifierletterordigit) }}
    *(but not a [Keyword](#keyword), nor a sequence of underscores only)*
<a id="identifierletter"></a>**IdentifierLetter:**
    `'A'`-`'Z'` | `'a'`-`'z'` | `'_'`
<a id="identifierletterordigit"></a>**IdentifierLetterOrDigit:**
    `'A'`-`'Z'` | `'a'`-`'z'` | `'0'`-`'9'` | `'_'`
*See also:* [Keywords](basic/keywords.md)
### Keywords
*Full description:* [Keywords](basic/keywords.md)
<a id="keyword"></a>**Keyword:**
    *(one of)*
    `bool` `byte` `char` `short` `int` `long`
    `float` `double` `unsigned`
    `struct` `namespace` `module` `import`
    `static` `const` `abstract` `final`
    `public` `protected` `private`
    `this` `return`
    `if` `else` `while` `for`
    `new` `delete`
### Literals
*Full description:* [Literals](expressions/literals.md)
<a id="literal"></a>**Literal:**
    [IntegerLiteral](#integerliteral) | [FloatLiteral](#floatliteral) | [BooleanLiteral](#booleanliteral)
    | CharacterLiteral | StringLiteral | [NullLiteral](#nullliteral)
<a id="integerliteral"></a>**IntegerLiteral:**
    DecimalLiteral `[` [IntegerSuffix](#integersuffix) `]`
    | `'0x'` HexDigit {{ HexDigit }} `[` [IntegerSuffix](#integersuffix) `]`
    | `'0'` OctalDigit {{ OctalDigit }} `[` [IntegerSuffix](#integersuffix) `]`
    | `'0b'` BinaryDigit {{ BinaryDigit }} `[` [IntegerSuffix](#integersuffix) `]`
<a id="integersuffix"></a>**IntegerSuffix:**
    *(one of)*
    `u` `l` `ul` `lu` `s` `b`
<a id="floatliteral"></a>**FloatLiteral:**
    FloatContent `[` [FloatSuffix](#floatsuffix) `]`
<a id="floatsuffix"></a>**FloatSuffix:**
    *(one of)*
    `f` `d`
<a id="booleanliteral"></a>**BooleanLiteral:**
    `'true'` | `'false'`
<a id="nullliteral"></a>**NullLiteral:**
    `'null'`
### Punctuators and operators
*Full description:* [Lexical Conventions](basic/lexical.md)
<a id="punctuator"></a>**Punctuator:**
    *(one of)*
    `(` `)` `{{` `}}` `[` `]` `;` `,` `::` `...` `@`
<a id="operator"></a>**Operator:**
    *(one of)*
    `.` `->` `.*` `->*`
    `?` `:` `!` `~`
    `=` `+` `-` `*` `/` `&` `|` `^` `%`
    `<<` `>>`
    `+=` `-=` `*=` `/=` `&=` `|=` `^=` `%=` `<<=` `>>=`
    `==` `!=` `<` `>` `<=` `>=`
    `&&` `||` `++` `--` `**`

    *Note: `!` is context-sensitive.  In an expression context it is the logical-NOT unary
    operator.  In a type context (after a type name in a [TypeSpec](#typespec)) it is the
    owner suffix — exactly as `*` is both the dereference operator and the pointer suffix.*
---
## Syntactic grammar
### Compilation unit
*Full description:* [Module System](basic/modules.md)
<a id="unit"></a>**Unit:**
    `[` [ModuleDeclaration](#moduledeclaration) `]` {{ [ImportDeclaration](#importdeclaration) }} {{ [Declaration](#declaration) }}
<a id="moduledeclaration"></a>**ModuleDeclaration:**
    `'module'` [QualifiedIdentifier](#qualifiedidentifier) `';'`
<a id="importdeclaration"></a>**ImportDeclaration:**
    `'import'` [Identifier](#identifier) `';'`
<a id="qualifiedidentifier"></a>**QualifiedIdentifier:**
    `[` `'::'` `]` [Identifier](#identifier) {{ `'::'` [Identifier](#identifier) }}
### Declarations
*Full description:* [Functions](functions/functions.md) - [Structures](structs/structs.md) - [Names and Lookup](basic/names.md)
<a id="declaration"></a>**Declaration:**
    [VisibilityDecl](#visibilitydecl)
    | [NamespaceDecl](#namespacedecl)
    | [AggregateDecl](#aggregatedecl)
    | [FunctionDecl](#functiondecl)
    | [VariableDecl](#variabledecl)
<a id="visibilitydecl"></a>**VisibilityDecl:**
    `(` `'public'` | `'protected'` | `'private'` `)` `':'`
<a id="namespacedecl"></a>**NamespaceDecl:**
    `'namespace'` `[` [Identifier](#identifier) `]` `'{{' {{ [Declaration](#declaration) }} '}}'`
<a id="aggregatedecl"></a>**AggregateDecl:**
    {{ [Specifier](#specifier) }} `(` `'struct'` | `'class'` | `'interface'` `)` [Identifier](#identifier) `[` `':'` [BaseClause](#baseclause) `]` `'{{' {{ [Declaration](#declaration) }} '}}'`

    *Notes:*
    - *`'abstract'` in [Specifier](#specifier) is only valid with `'class'`, not `'struct'`. For `'interface'` it is accepted but redundant (warning `0x2002A`).*
    - *`'interface'` bodies may not contain fields, constructors, or destructors. Method bodies are forbidden (implicit abstract, warning `0x2002B` if `abstract` is written explicitly).*
<a id="baseclause"></a>**BaseClause:**
    [BaseSpec](#basespec) {{ `','` [BaseSpec](#basespec) }}
<a id="basespec"></a>**BaseSpec:**
    `[` `(` `'public'` | `'protected'` | `'private'` `)` `]` [Identifier](#identifier)
<a id="functiondecl"></a>**FunctionDecl:**
    {{ [Specifier](#specifier) }} `[` `'~'` `]` [Identifier](#identifier) `'('` `[` [ParameterList](#parameterlist) `]` `')'` `[` `':'` [TypeSpec](#typespec) `]`
    `[` `':'` `(` [MemberInitList](#memberinitlist) | [StaticDepList](#staticdeplist) `)` `]`
    ( [BlockStatement](#blockstatement) | `'->'` `(` `'default'` | `'delete'` `)` `';'` | `';'` *(abstract only)* )

    *Note: the bare `';'` form (no body) is only valid when `'abstract'` appears in the [Specifier](#specifier) list.*
<a id="variabledecl"></a>**VariableDecl:**
    {{ [Specifier](#specifier) }} [Identifier](#identifier) `':'` [TypeSpec](#typespec) `[` [Initialiser](#initialiser) `]` `';'`
<a id="specifier"></a>**Specifier:**
    *(one of)*
    `'public'` `'protected'` `'private'` `'static'`
    `'const'` `'abstract'` `'final'`
### Type specifiers
*Full description:* [Types](basic/types.md)
<a id="typespec"></a>**TypeSpec:**
    [FundamentalTypeSpec](#fundamentaltypespec) {{ [TypeSuffix](#typesuffix) }}
    | [QualifiedIdentifier](#qualifiedidentifier) {{ [TypeSuffix](#typesuffix) }}
    | [FunctionRefType](#functionreftype)
    | [QualifiedIdentifier](#qualifiedidentifier) `'::'` [FunctionRefType](#functionreftype)
<a id="functionreftype"></a>**FunctionRefType:**
    [FunctionRefQualifier](#functionrefqualifier) `'('` `[` [TypeList](#typelist) `]` `')'`
<a id="functionrefqualifier"></a>**FunctionRefQualifier:**
    `'*'` | `'^'` | `'~'`
<a id="typelist"></a>**TypeList:**
    [TypeSpec](#typespec) {{ `','` [TypeSpec](#typespec) }}
*Full description:* [Function References](functions/function_references.md)
<a id="fundamentaltypespec"></a>**FundamentalTypeSpec:**
    `[` `'unsigned'` `]` `(` `'byte'` | `'char'` | `'short'` | `'int'` | `'long'` | `'float'` | `'double'` `)`
    | `'bool'`
<a id="typesuffix"></a>**TypeSuffix:**
    `'['` `[` [IntegerLiteral](#integerliteral) `]` `']'`     -- array (sized or unsized)
    | `'!'`                                 -- owner (move-only, nullable, exclusive ownership)
    | `'*'`                                 -- pointer (mutable, nullable)
    | `'&'`                                 -- reference (immutable binding, non-null)
    | `'~'`                                 -- link (mutable binding, non-null)
    | `'^'`                                 -- pinned (immutable binding, nullable)
### Parameters
*Full description:* [Functions - Parameters](functions/functions.md#2-parameters)
<a id="parameterlist"></a>**ParameterList:**
    [ParameterSpec](#parameterspec) {{ `','` [ParameterSpec](#parameterspec) }}
<a id="parameterspec"></a>**ParameterSpec:**
    {{ [Specifier](#specifier) }} `[` [Identifier](#identifier) `':'` `]` [TypeSpec](#typespec) `[` `'='` [ConditionalExpr](#conditionalexpr) `]`
### Member and static initializer lists
*Full description:* [Constructors](structs/constructors.md)
<a id="memberinitlist"></a>**MemberInitList:**
    [MemberInit](#memberinit) {{ `','` [MemberInit](#memberinit) }}
<a id="memberinit"></a>**MemberInit:**
    [Identifier](#identifier) `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
<a id="staticdeplist"></a>**StaticDepList:**
    [StaticDep](#staticdep) {{ `','` [StaticDep](#staticdep) }}
<a id="staticdep"></a>**StaticDep:**
    [QualifiedIdentifier](#qualifiedidentifier) `'('` `')'`
### Initialiser
<a id="initialiser"></a>**Initialiser:**
    `'='` [ConditionalExpr](#conditionalexpr)
    | `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
### Statements
*Full description:* [Statements](statements/statements.md)
<a id="statement"></a>**Statement:**
    [BlockStatement](#blockstatement)
    | [ReturnStatement](#returnstatement)
    | [IfElseStatement](#ifelsestatement)
    | [WhileStatement](#whilestatement)
    | [ForStatement](#forstatement)
    | [VariableDecl](#variabledecl) `';'`
    | [ExpressionStatement](#expressionstatement)
<a id="blockstatement"></a>**BlockStatement:**
    `'{{'` {{ [Statement](#statement) }} `'}}'`
<a id="returnstatement"></a>**ReturnStatement:**
    `'return'` `[` [Expression](#expression) `]` `';'`
<a id="ifelsestatement"></a>**IfElseStatement:**
    `'if'` `'('` [Expression](#expression) `')'` [Statement](#statement) `[` `'else'` [Statement](#statement) `]`
<a id="whilestatement"></a>**WhileStatement:**
    `'while'` `'('` [Expression](#expression) `')'` [Statement](#statement)
<a id="forstatement"></a>**ForStatement:**
    `'for'` `'('` `[` [ForInit](#forinit) `]` `';'` `[` [Expression](#expression) `]` `';'` `[` [Expression](#expression) `]` `')'` [Statement](#statement)
<a id="forinit"></a>**ForInit:**
    {{ [Specifier](#specifier) }} [Identifier](#identifier) `':'` [TypeSpec](#typespec) `[` [Initialiser](#initialiser) `]`
<a id="expressionstatement"></a>**ExpressionStatement:**
    `[` [Expression](#expression) `]` `';'`
*Full description of each:* [If](statements/if.md) - [While](statements/while.md) - [For](statements/for.md) - [Return](statements/return.md)
### Expressions
*Full description:* [Expressions](expressions/expressions.md)
<a id="expression"></a>**Expression:**
    [AssignmentExpr](#assignmentexpr) {{ `','` [AssignmentExpr](#assignmentexpr) }}
<a id="expressionlist"></a>**ExpressionList:**
    [AssignmentExpr](#assignmentexpr) {{ `','` [AssignmentExpr](#assignmentexpr) }}
<a id="assignmentexpr"></a>**AssignmentExpr:**
    [ConditionalExpr](#conditionalexpr) `[` [AssignmentOperator](#assignmentoperator) [AssignmentExpr](#assignmentexpr) `]`
<a id="assignmentoperator"></a>**AssignmentOperator:**
    *(one of)*
    `=` `*=` `/=` `%=` `+=` `-=` `>>=` `<<=` `&=` `^=` `|=`
<a id="conditionalexpr"></a>**ConditionalExpr:**
    [LogicalOrExpr](#logicalorexpr) `[` `'?'` [ConditionalExpr](#conditionalexpr) `':'` [ConditionalExpr](#conditionalexpr) `]`
<a id="logicalorexpr"></a>**LogicalOrExpr:**
    [LogicalAndExpr](#logicalandexpr) {{ `'||'` [LogicalAndExpr](#logicalandexpr) }}
<a id="logicalandexpr"></a>**LogicalAndExpr:**
    [InclusiveBinOrExpr](#inclusivebinoexpr) {{ `'&&'` [InclusiveBinOrExpr](#inclusivebinoexpr) }}
<a id="inclusivebinoexpr"></a>**InclusiveBinOrExpr:**
    [ExclusiveBinOrExpr](#exclusivebinoexpr) {{ `'|'` [ExclusiveBinOrExpr](#exclusivebinoexpr) }}
<a id="exclusivebinoexpr"></a>**ExclusiveBinOrExpr:**
    [BinAndExpr](#binandexpr) {{ `'^'` [BinAndExpr](#binandexpr) }}
<a id="binandexpr"></a>**BinAndExpr:**
    [EqualityExpr](#equalityexpr) {{ `'&'` [EqualityExpr](#equalityexpr) }}
<a id="equalityexpr"></a>**EqualityExpr:**
    [RelationalExpr](#relationalexpr) {{ `(` `'=='` | `'!='` `)` [RelationalExpr](#relationalexpr) }}
<a id="relationalexpr"></a>**RelationalExpr:**
    [ShiftingExpr](#shiftingexpr) {{ `(` `'<'` | `'>'` | `'<='` | `'>='` `)` [ShiftingExpr](#shiftingexpr) }}
<a id="shiftingexpr"></a>**ShiftingExpr:**
    [AdditiveExpr](#additiveexpr) {{ `(` `'<<'` | `'>>'` `)` [AdditiveExpr](#additiveexpr) }}
<a id="additiveexpr"></a>**AdditiveExpr:**
    [MultiplicativeExpr](#multiplicativeexpr) {{ `(` `'+'` | `'-'` `)` [MultiplicativeExpr](#multiplicativeexpr) }}
<a id="multiplicativeexpr"></a>**MultiplicativeExpr:**
    [PmExpr](#pmexpr) {{ `(` `'*'` | `'/'` | `'%'` `)` [PmExpr](#pmexpr) }}
<a id="pmexpr"></a>**PmExpr:**
    [CastExpr](#castexpr) {{ `(` `'.*'` | `'->*'` `)` [CastExpr](#castexpr) }}
*The `.*` and `->*` operators are used for pointer-to-member calls; see [Function References](functions/function_references.md#5-calling-through-a-member-function-reference--operators--and--).*
<a id="castexpr"></a>**CastExpr:**
    `'('` [TypeSpec](#typespec) `')'` [CastExpr](#castexpr)
    | [UnaryExpr](#unaryexpr)
<a id="unaryexpr"></a>**UnaryExpr:**
    `'new'` [TypeName](#typename) `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
    | `'delete'` [CastExpr](#castexpr)
    | `(` `'++'` | `'--'` | `'*'` | `'&'` | `'+'` | `'-'` | `'!'` | `'~'` `)` [CastExpr](#castexpr)
    | [PostfixExpr](#postfixexpr)

    *Note: `'new' TypeName '(' … ')'` returns a `T!` owner.  `'delete' CastExpr` returns `void` and may only appear as an expression statement.*

<a id="typename"></a>**TypeName:**
    [QualifiedIdentifier](#qualifiedidentifier)
    | [FundamentalTypeSpec](#fundamentaltypespec)

    *Note: `TypeName` is a bare type identifier — indirection suffixes (`*`, `!`, `&`, …) are not permitted in a `new` expression.*
<a id="postfixexpr"></a>**PostfixExpr:**
    [PrimaryExpr](#primaryexpr) {{ [PostfixOp](#postfixop) }}
<a id="postfixop"></a>**PostfixOp:**
    `'++'`
    | `'--'`
    | `'['` [Expression](#expression) `']'`
    | `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
    | `(` `'.'` | `'->'` `)` [IdentifierExpr](#identifierexpr)
<a id="primaryexpr"></a>**PrimaryExpr:**
    [Literal](#literal)
    | `'this'`
    | `'('` [Expression](#expression) `')'`
    | [IdentifierExpr](#identifierexpr)
<a id="identifierexpr"></a>**IdentifierExpr:**
    [QualifiedIdentifier](#qualifiedidentifier)
*Full description of operators:* [Unary](expressions/unary.md) - [Binary](expressions/binary.md) - [Assignment](expressions/assignment.md) - [Call and member access](expressions/call.md)
---
*See also:* [Index](index.md) for per-topic reference pages.
