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
    `struct` `class` `interface` `namespace` `module` `import` `using` `friend`
    `static` `const` `abstract` `final` `override`
    `public` `protected` `private`
    `this` `return`
    `if` `else` `while` `for` `break` `continue`
    `new` `delete` `default` `enum` `union`
    `operator` `annotation`
    `template` `typename` `generic`
    `throw` `try` `catch` `throws`
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
    `?` `:` `!` `+`
    `=` `+` `-` `*` `/` `&` `|` `?` `%`
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
    `[` `'::'` `]` [IdentifierSegment](#identifiersegment) {{ `'::'` [IdentifierSegment](#identifiersegment) }}

<a id="identifiersegment"></a>**IdentifierSegment:**
    [Identifier](#identifier) `[` [TemplateArgList](#templatearglist) `]`

### Templates

*Full description:* [Templates](templates/templates.md)

<a id="templatedeclaration"></a>**TemplateDeclaration:**
    `'template'` `'<'` [TemplateParameterList](#templateparameterlist) `'>'`

<a id="templateparameterlist"></a>**TemplateParameterList:**
    [TemplateParameter](#templateparameter) {{ `','` [TemplateParameter](#templateparameter) }}

<a id="templateparameter"></a>**TemplateParameter:**
    [TemplateParameterKind](#templateparameterkind) [Identifier](#identifier) `[` `':'` [TypeSpec](#typespec) `]` `[` `'='` [ConditionalExpr](#conditionalexpr) `]`

    *A **type parameter** is introduced by one of the keywords `typename`, `struct`, `class`, `interface`.
    The optional `: TypeSpec` after the identifier specifies a **base-type constraint**.
    The optional `= ConditionalExpr` specifies a default type.*

    *A **value parameter** has a type identifier or TypeSpec as its kind (instead of a keyword).
    The optional `= ConditionalExpr` specifies a default value.
    Value parameter types must be compile-time-evaluable (primitive integers, `bool`, `char`, enums).*

<a id="templateparameterkind"></a>**TemplateParameterKind:**
    `'typename'` | `'struct'` | `'class'` | `'interface'`
    | [Identifier](#identifier)
    | [TypeSpec](#typespec)

    *The first four are type parameter keywords. An identifier or type specifier denotes a
    value parameter — the identifier/type names the type of the expected compile-time constant value.*

<a id="templatearglist"></a>**TemplateArgList:**
    `'<'` [TemplateArg](#templatearg) {{ `','` [TemplateArg](#templatearg) }} `'>'`

<a id="templatearg"></a>**TemplateArg:**
    [TypeSpec](#typespec)
    | [ConditionalExpr](#conditionalexpr)

    *A type argument is a type specifier. A value argument is a constant expression.
    The parser disambiguates based on whether the template parameter at that position
    is a type or value parameter. When the name being instantiated is followed by `<`,
    the parser checks whether a template declaration exists with that name to distinguish
    from the less-than comparison operator.*

### Declarations
*Full description:* [Functions](functions/functions.md) - [Structures](structs/structs.md) - [Names and Lookup](basic/names.md) - [Using Directives](basic/using.md)
<a id="declaration"></a>**Declaration:**
    [VisibilityDecl](#visibilitydecl)
    | [NamespaceDecl](#namespacedecl)
    | [UsingDecl](#usingdecl)
    | [AggregateDecl](#aggregatedecl)
    | [FunctionDecl](#functiondecl)
    | [VariableDecl](#variabledecl)
<a id="visibilitydecl"></a>**VisibilityDecl:**
    `(` `'public'` | `'protected'` | `'private'` `)` `':'`
<a id="namespacedecl"></a>**NamespaceDecl:**
    `'namespace'` `[` [Identifier](#identifier) `]` `'{{' {{ [Declaration](#declaration) }} '}}'`
<a id="usingdecl"></a>**UsingDecl:**
    `'using'` `[` [UsingFilter](#usingfilter) `]` `[` [Identifier](#identifier) `'='` `]` [QualifiedIdentifier](#qualifiedidentifier) `';'`

    *A `using` directive can appear in both declaration context (namespace or aggregate body)
    and statement context (function body or block).  See [Using Directives](basic/using.md).*

<a id="usingfilter"></a>**UsingFilter:**
    `(` `'namespace'` | `'struct'` | `'interface'` | `'class'` `)`
<a id="aggregatedecl"></a>**AggregateDecl:**
    {{ [AnnotationDef](#annotationdef) }} `[` [TemplateDeclaration](#templatedeclaration) `]` {{ [Specifier](#specifier) }} `(` `'struct'` | `'class'` | `'interface'` | `'annotation'` `)` [Identifier](#identifier) `[` `':'` [BaseClause](#baseclause) `]` `'{{' {{ [Declaration](#declaration) }} '}}'`

    *Notes:*
    - *`'abstract'` in [Specifier](#specifier) is only valid with `'class'`, not `'struct'`. For `'interface'` it is accepted but redundant (warning `0x2002A`).*
    - *`'interface'` bodies may not contain fields, constructors, or destructors. Method bodies are forbidden (implicit abstract, warning `0x2002B` if `abstract` is written explicitly).*
    - *`'annotation'` declares an annotation type. The body may contain variable declarations and methods. Annotation types are implicitly `const`. See [Annotations](annotations/annotations.md).*
    - *[AnnotationDef](#annotationdef) elements appear before the specifiers and aggregate keyword.*

*Full description:* [Annotations](annotations/annotations.md)

<a id="annotationdef"></a>**AnnotationDef:**
    `'@'` [QualifiedIdentifier](#qualifiedidentifier)
    | `'@'` [QualifiedIdentifier](#qualifiedidentifier) `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
    | `'@'` [QualifiedIdentifier](#qualifiedidentifier) [DesignatedBraceInitList](#designatedbraceinitlist)
    | `'@'` [QualifiedIdentifier](#qualifiedidentifier) [BraceInitList](#braceinitlist)

<a id="annotationdeflist"></a>**AnnotationDefList:**
    {{ [AnnotationDef](#annotationdef) }}
<a id="baseclause"></a>**BaseClause:**
    [BaseSpec](#basespec) {{ `','` [BaseSpec](#basespec) }}
<a id="basespec"></a>**BaseSpec:**
    `[` `(` `'public'` | `'protected'` | `'private'` `)` `]` [Identifier](#identifier)
<a id="functiondecl"></a>**FunctionDecl:**
    {{ [AnnotationDef](#annotationdef) }} `[` [TemplateDeclaration](#templatedeclaration) `]` {{ [Specifier](#specifier) }} `[` `'+'` `]` [Identifier](#identifier) `'('` `[` [ParameterList](#parameterlist) `]` `')'`
    `[` [Identifier](#identifier) `]`
    `[` `':'` [TypeSpec](#typespec) `[` [Initialiser](#initialiser) `]` `]`
    `[` `':'` `(` [MemberInitList](#memberinitlist) | [StaticDepList](#staticdeplist) `)` `]`
    `[` [ThrowsClause](#throwsclause) `]`
    ( [BlockStatement](#blockstatement) | `'->'` `(` `'default'` | `'delete'` `)` `';'` | `';'` *(abstract only)* )
    | [OperatorFunctionDecl](#operatorfunctiondecl)

    *Note: the bare `';'` form (no body) is only valid when `'abstract'` appears in the [Specifier](#specifier) list.*

    *Note: the optional [Identifier](#identifier) after `')'` names the return variable.
    When present, the function uses **named return variable** semantics: the variable is
    declared as a local at function entry, and is implicitly returned when the function
    falls off the closing `'}'` or executes a bare `'return'` `';'`.  See [Named Return Variables](functions/named-return.md).*

<a id="throwsclause"></a>**ThrowsClause:**
    `'throws'` [TypeSpec](#typespec) {{ `','` [TypeSpec](#typespec) }}

    *Note: all types in the throws clause must derive from `::k::Throwable`.*

<a id="operatorfunctiondecl"></a>**OperatorFunctionDecl:**
    {{ [Specifier](#specifier) }} `'operator'` [OperatorSymbol](#operatorsymbol) `'('` `[` [ParameterList](#parameterlist) `]` `')'`
    `[` [Identifier](#identifier) `]`
    `[` `':'` [TypeSpec](#typespec) `[` [Initialiser](#initialiser) `]` `]`
    ( [BlockStatement](#blockstatement) | `';'` *(abstract only)* )
    | {{ [Specifier](#specifier) }} `'operator'` `'('` `')'` `':'` [TypeSpec](#typespec) [BlockStatement](#blockstatement)

<a id="operatorsymbol"></a>**OperatorSymbol:**
    *(one of)*
    `+` `-` `*` `/` `%` `&` `|` `?` `+` `<<` `>>` `&&` `||` `!`
    `==` `!=` `<` `>` `<=` `>=`
    `=` `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`
    `++_` `--_` `_++` `_--`

    *Note: `++_` and `--_` denote prefix increment/decrement; `_++` and `_--` denote postfix.
    The `'operator' '(' ')'` form (cast operator) has no OperatorSymbol — the empty parentheses identify it.
    Full description:* [Operator Overloading](functions/operators.md).

<a id="specifier"></a>**Specifier:**
    *(one of)*
    `'public'` `'protected'` `'private'` `'static'`
    `'const'` `'abstract'` `'final'` `'override'`
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
    `'*'` | `'?'` | `'+'`
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
    | `'+'`                                 -- link (mutable binding, non-null)
    | `'?'`                                 -- view (immutable binding, nullable)

*Suffixes are applied left-to-right. An indirection suffix followed by an array suffix
creates an array of indirections: `int+[3]` = array of 3 links to int.
See [Types — §9.7](basic/types.md#97-arrays-of-indirection-types).*
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
    | `'('` `[` [ExpressionList](#expressionlist) `]` `')'` `'['` [Expression](#expression) `']'`
    | [BraceInitList](#braceinitlist)
    | [DesignatedBraceInitList](#designatedbraceinitlist)
<a id="braceinitlist"></a>**BraceInitList:**
    `'{'` `[` [InitElement](#initelement) {{ `','` [InitElement](#initelement) }} `]` `'}'`
<a id="initelement"></a>**InitElement:**
    [ConditionalExpr](#conditionalexpr)
    | *empty*

*Full description:* [Designated Struct Initializers](structs/designated-init.md)
<a id="designatedbraceinitlist"></a>**DesignatedBraceInitList:**
    `'{'` [DesignatedInitElement](#designatedinitelement) {{ `','` [DesignatedInitElement](#designatedinitelement) }} `'}'`
<a id="designatedinitelement"></a>**DesignatedInitElement:**
    `'.'` [DesignatedMemberName](#designatedmembername) `'='` [ConditionalExpr](#conditionalexpr)
    | `'.'` [DesignatedMemberName](#designatedmembername) `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
    | `'.'` [DesignatedMemberName](#designatedmembername) `'='` [DesignatedBraceInitList](#designatedbraceinitlist)
<a id="designatedmembername"></a>**DesignatedMemberName:**
    `[` [Identifier](#identifier) `'::'` {{ [Identifier](#identifier) `'::'` }} `]` [Identifier](#identifier)
### Statements
*Full description:* [Statements](statements/statements.md)
<a id="statement"></a>**Statement:**
    [BlockStatement](#blockstatement)
    | [ReturnStatement](#returnstatement)
    | [BreakStatement](#breakstatement)
    | [IfElseStatement](#ifelsestatement)
    | [WhileStatement](#whilestatement)
    | [ForStatement](#forstatement)
    | [ThrowStatement](#throwstatement)
    | [TryCatchStatement](#trycatchstatement)
    | [UsingDecl](#usingdecl)
    | [VariableDecl](#variabledecl) `';'`
    | [ExpressionStatement](#expressionstatement)
<a id="blockstatement"></a>**BlockStatement:**
    `'{{'` {{ [Statement](#statement) }} `'}}'`
<a id="returnstatement"></a>**ReturnStatement:**
    `'return'` `[` [Expression](#expression) `]` `';'`
<a id="breakstatement"></a>**BreakStatement:**
    `'break'` `';'`
<a id="ifelsestatement"></a>**IfElseStatement:**
    `'if'` `'('` [Expression](#expression) `')'` [Statement](#statement) `[` `'else'` [Statement](#statement) `]`
    | `'if'` `'('` [IfCondVarDecl](#ifcondvardecl) `')'` [Statement](#statement) `[` `'else'` [Statement](#statement) `]`
    | `'if'` `'('` [IfCondVarDeclList](#ifcondvardecllist) `';'` [ConditionalExpr](#conditionalexpr) `')'` [Statement](#statement) `[` `'else'` [Statement](#statement) `]`
    | `'if'` `'('` [IfCondVarDeclList](#ifcondvardecllist) `')'` [Statement](#statement) `[` `'else'` [Statement](#statement) `]`
<a id="ifcondvardecllist"></a>**IfCondVarDeclList:**
    [IfCondVarDecl](#ifcondvardecl) {{ `';'` [IfCondVarDecl](#ifcondvardecl) }}
<a id="ifcondvardecl"></a>**IfCondVarDecl:**
    {{ [Specifier](#specifier) }} [Identifier](#identifier) `':'` [TypeSpec](#typespec) `[` [CondVarInitialiser](#condvarinitialiser) `]`
<a id="condvarinitialiser"></a>**CondVarInitialiser:**
    `'='` [ConditionalExpr](#conditionalexpr)
    | `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
    | [BraceInitList](#braceinitlist)
<a id="whilestatement"></a>**WhileStatement:**
    `'while'` `'('` [Expression](#expression) `')'` [Statement](#statement)
<a id="forstatement"></a>**ForStatement:**
    `'for'` `'('` `[` [ForInit](#forinit) `]` `';'` `[` [Expression](#expression) `]` `';'` `[` [Expression](#expression) `]` `')'` [Statement](#statement)
<a id="forinit"></a>**ForInit:**
    {{ [Specifier](#specifier) }} [Identifier](#identifier) `':'` [TypeSpec](#typespec) `[` [Initialiser](#initialiser) `]`
<a id="expressionstatement"></a>**ExpressionStatement:**
    `[` [Expression](#expression) `]` `';'`
<a id="throwstatement"></a>**ThrowStatement:**
    `'throw'` [Expression](#expression) `';'`
<a id="trycatchstatement"></a>**TryCatchStatement:**
    `'try'` [BlockStatement](#blockstatement) {{ [CatchClause](#catchclause) }}
<a id="catchclause"></a>**CatchClause:**
    `'catch'` `'('` `[` `'const'` `]` [Identifier](#identifier) `':'` [TypeSpec](#typespec) `')'` [BlockStatement](#blockstatement)
*Full description of each:* [If](statements/if.md) - [While](statements/while.md) - [For](statements/for.md) - [Return](statements/return.md) - [Break](statements/break.md) - [Exception Handling](statements/exceptions.md)
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
    [BinAndExpr](#binandexpr) {{ `'?'` [BinAndExpr](#binandexpr) }}
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
    | `'new'` [TypeName](#typename) `'('` `[` [ExpressionList](#expressionlist) `]` `')'` `'['` [Expression](#expression) `']'`
    | `'new'` [TypeName](#typename) `'['` `[` [IntegerLiteral](#integerliteral) `]` `']'` `[` [BraceInitList](#braceinitlist) `]`
    | `'new'` [TypeName](#typename) [BraceInitList](#braceinitlist)
    | `'delete'` [CastExpr](#castexpr)
    | `(` `'++'` | `'--'` | `'*'` | `'&'` | `'+'` | `'-'` | `'!'` | `'+'` `)` [CastExpr](#castexpr)
    | [PostfixExpr](#postfixexpr)

    *Note: `'new' TypeName '(' … ')'` returns a `T!` owner.  `'new' TypeName '(' … ')' '[' … ']'` returns a `T[N]!` or `T[]!` uniform array owner (see [Uniform Array Initialization](memory/uniform-array-init.md)).  `'new' TypeName '[' … ']' …` and `'new' TypeName '{' … '}'` return a `T[N]!` array owner.  `'delete' CastExpr` returns `void` and may only appear as an expression statement.*

<a id="typename"></a>**TypeName:**
    [QualifiedIdentifier](#qualifiedidentifier)
    | [FundamentalTypeSpec](#fundamentaltypespec)

    *Note: `TypeName` is a bare type identifier — indirection suffixes (`*`, `!`, `&`, …) are not permitted in a `new` expression.*

<a id="braceinitlist"></a>**BraceInitList:**
    `'{'` `[` [InitList](#initlist) `]` `'}'`
<a id="initlist"></a>**InitList:**
    `[` [Expression](#expression) `]` {{ `','` `[` [Expression](#expression) `]` }}
<a id="postfixexpr"></a>**PostfixExpr:**
    [PrimaryExpr](#primaryexpr) {{ [PostfixOp](#postfixop) }}
<a id="postfixop"></a>**PostfixOp:**
    `'++'`
    | `'--'`
    | `'['` [Expression](#expression) `']'`
    | `'('` `[` [ExpressionList](#expressionlist) `]` `')'`
    | `(` `'.'` | `'->'` `)` [IdentifierExpr](#identifierexpr)
    *A primary identifier may carry a [TemplateArgList](#templatearglist) (e.g. `Optional<byte>`).
    In expression context the parser commits to the `'<' … '>'` template-argument list only
    when the matching `'>'` is immediately followed by `'('`, `'.'`, or `'->'`
    (call / temporary construction, or member access); otherwise `'<'` is the less-than operator.*
<a id="primaryexpr"></a>**PrimaryExpr:**
    [Literal](#literal)
    | `'this'`
    | `'('` [Expression](#expression) `')'`
    | [IdentifierExpr](#identifierexpr)
<a id="identifierexpr"></a>**IdentifierExpr:**
    [QualifiedIdentifier](#qualifiedidentifier)
*Full description of operators:* [Unary](expressions/unary.md) - [Binary](expressions/binary.md) - [Assignment](expressions/assignment.md) - [Call and member access](expressions/call.md) - [Operator Overloading](functions/operators.md)
---
*See also:* [Index](index.md) for per-topic reference pages.

