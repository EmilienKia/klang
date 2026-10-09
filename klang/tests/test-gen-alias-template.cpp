/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Parameterised aliases: 'template<typename T> alias Vec : Array<T, 16>;'
 *
 * A parameterised alias renames a *family* of types. It is never instantiated
 * into an entity of its own: a use such as 'Vec<int>' substitutes the arguments
 * into the renamed type and resolves the result. A parameterised 'typedef'
 * keeps a nominal identity per distinct argument list.
 */

#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"
#include "../src/errors.hpp"
#include "../src/model/model_builder.hpp"

TEST_CASE("template substitution preserves caller provenance and scopes only fresh declaration references",
          "[gen][alias][template][type-provenance][regression]") {
    k::source source{R"K(
        module k;
        namespace declaration {
            struct Error { code: int; }
            template<typename T, typename E> struct Box {}
            template<typename T> alias Result: Box<const T&, Error>;
        }
        namespace caller {
            struct Value { value: int; }
            template<typename T> struct Arguments { value: Value; }
        }
    )K"};
    test_logger logger;
    k::parse::parser parser(logger, source);
    auto ast = parser.parse_unit();
    REQUIRE(ast);
    auto context = k::model::context::create();
    auto unit = k::model::unit::create(context);
    k::model::model_builder::visit(logger, context, *ast, *unit);
    REQUIRE_FALSE(logger.has_error());
    auto root = unit->get_root_namespace();
    auto declaration = root->get_child_namespace("declaration");
    auto target = std::dynamic_pointer_cast<k::model::unresolved_type>(
        declaration->get_alias("Result")->get_target_type());
    REQUIRE(target);
    CHECK_FALSE(target->get_definition_namespace());
    target->set_definition_namespace(declaration);

    auto argument = std::dynamic_pointer_cast<k::model::unresolved_type>(
        root->get_child_namespace("caller")->get_aggregate("Arguments")
            ->get_variable("value")->get_type());
    REQUIRE(argument);
    CHECK_FALSE(argument->is_resolved());
    auto substituted = target->clone_with_substituted_model_args({{"T", argument}});
    REQUIRE(substituted);
    CHECK(substituted->get_definition_namespace() == declaration);
    REQUIRE(substituted->get_model_template_args().size() == 2);
    auto wrapped_argument = substituted->get_model_template_args()[0];
    REQUIRE(k::model::type::is_reference(wrapped_argument));
    REQUIRE(k::model::type::is_const(wrapped_argument->get_subtype()));
    CHECK(wrapped_argument->get_subtype()->get_subtype() == argument);
    CHECK_FALSE(argument->get_definition_namespace());
    auto error = std::dynamic_pointer_cast<k::model::unresolved_type>(
        substituted->get_model_template_args()[1]);
    REQUIRE(error);
    CHECK(error->get_definition_namespace() == declaration);

    auto caller = root->get_child_namespace("caller");
    argument->set_definition_namespace(caller);
    auto repeated = target->clone_with_substituted_model_args({{"T", argument}});
    REQUIRE(repeated);
    CHECK(argument->get_definition_namespace() == caller);
}

TEST_CASE("template substitution composes enclosing and nested model argument bindings",
          "[gen][alias][template][substitution][regression]") {
    k::source source{R"K(
        module k;
        template<typename A, typename R> alias Function : *(A):R;
        template<typename C, typename S> struct Box {}
        template<typename T, typename U> alias Signature : Function<const T&, U>;
        template<typename T, typename U> alias Nested : Box<Function<const T&, U>, int>;
    )K"};
    test_logger logger;
    k::parse::parser parser(logger, source);
    auto ast = parser.parse_unit();
    REQUIRE(ast);
    auto context = k::model::context::create();
    auto unit = k::model::unit::create(context);
    k::model::model_builder::visit(logger, context, *ast, *unit);
    REQUIRE_FALSE(logger.has_error());
    auto root = unit->get_root_namespace();
    for (const auto* alias_name : {"Signature", "Nested"}) {
        INFO("alias: " << alias_name);
        auto target = std::dynamic_pointer_cast<k::model::unresolved_type>(
            root->get_alias(alias_name)->get_target_type());
        REQUIRE(target);
        auto enclosing = target->clone_with_substituted_model_args(
            {{"T", context->from_string("int")}});
        REQUIRE(enclosing);
        auto member = enclosing->clone_with_substituted_model_args(
            {{"U", context->from_string("long")}});
        REQUIRE(member);
        auto enclosing_signature = enclosing;
        auto member_signature = member;
        if (std::string{alias_name} == "Nested") {
            REQUIRE(enclosing->get_model_template_args().size() == 2);
            REQUIRE(member->get_model_template_args().size() == 2);
            CHECK_FALSE(enclosing->get_model_template_args()[1]);
            CHECK_FALSE(member->get_model_template_args()[1]);
            enclosing_signature = std::dynamic_pointer_cast<k::model::unresolved_type>(
                enclosing->get_model_template_args()[0]);
            member_signature = std::dynamic_pointer_cast<k::model::unresolved_type>(
                member->get_model_template_args()[0]);
        }
        REQUIRE(enclosing_signature);
        REQUIRE(member_signature);
        REQUIRE(enclosing_signature->get_model_template_args().size() == 2);
        REQUIRE(member_signature->get_model_template_args().size() == 2);
        auto parameter = member_signature->get_model_template_args()[0];
        CHECK(parameter == enclosing_signature->get_model_template_args()[0]);
        REQUIRE(k::model::type::is_reference(parameter));
        REQUIRE(k::model::type::is_const(parameter->get_subtype()));
        CHECK(parameter->get_subtype()->get_subtype() == context->from_string("int"));
        CHECK(member_signature->get_model_template_args()[1] == context->from_string("long"));
        auto unbound = std::dynamic_pointer_cast<k::model::unresolved_type>(
            enclosing_signature->get_model_template_args()[1]);
        REQUIRE(unbound);
        CHECK(unbound->type_id().to_string() == "U");
        CHECK_FALSE(unbound->is_resolved());
        CHECK_FALSE(member->clone_with_substituted_model_args(
            {{"T", context->from_string("byte")}}));
    }
}

TEST_CASE("template alias - member specialization retains enclosing callable arguments",
          "[gen][alias][template][member-template][callable][substitution][regression]") {
    auto compiler = compile_model(R"SRC(
        module k;
        template<typename A, typename R> alias Function : *(A):R;
        template<typename C> struct Callback { value : C; }
        template<typename T>
        struct Collection {
            template<typename U>
            map(mapper : Function<const T&, U>) : U { return (U)42; }
            template<typename U>
            nested(mapper : Callback<Function<const T&, U>>&) : U { return (U)42; }
        }
        use() {
            collection : Collection<int>;
            callback : Callback<Function<const int&, long>>;
            collection.map<long>(null);
            collection.nested<long>(callback);
        }
    )SRC");
    REQUIRE(compiler);
    REQUIRE_FALSE(compiler->has_compilation_error());
    auto context = compiler->get_context_for_test();
    auto collection = compiler->get_unit()->get_root_namespace()->get_aggregate("Collection__int");
    REQUIRE(collection);
    size_t specialized_methods = 0;
    for (const auto& fn : collection->functions()) {
        if (!fn->is_instantiation()
            || (fn->get_tpl_base_name() != "map" && fn->get_tpl_base_name() != "nested")) continue;
        INFO("method: " << fn->get_short_name());
        REQUIRE(fn->parameters().size() == 1);
        auto mapper_type = fn->parameters().front()->get_type();
        if (fn->get_tpl_base_name() == "nested") {
            REQUIRE(k::model::type::is_reference(mapper_type));
            auto callback_type = std::dynamic_pointer_cast<k::model::struct_type>(mapper_type->get_subtype());
            REQUIRE(callback_type);
            auto callback = callback_type->get_struct();
            REQUIRE(callback);
            REQUIRE(callback->get_tpl_args().size() == 1);
            mapper_type = callback->get_tpl_args()[0].type_arg;
        }
        auto mapper = std::dynamic_pointer_cast<k::model::callable_type>(mapper_type);
        REQUIRE(mapper);
        REQUIRE(mapper->get_parameter_types().size() == 1);
        auto parameter = mapper->get_parameter_types().front();
        REQUIRE(k::model::type::is_reference(parameter));
        REQUIRE(k::model::type::is_const(parameter->get_subtype()));
        CHECK(parameter->get_subtype()->get_subtype() == context->from_string("int"));
        CHECK(mapper->get_return_type() == context->from_string("long"));
        CHECK(fn->get_return_type() == context->from_string("long"));
        ++specialized_methods;
    }
    CHECK(specialized_methods == 2);
}

TEST_CASE("template alias - functional Consumer stays symbolic until specialization",
          "[gen][alias][template][callable][symbolic-alias]") {
    auto compiler = compile_model(R"SRC(
        module k;
        struct T { wrong: long; }
        namespace functional {
            template<typename T> alias Consumer : *(T);
            template<typename T> alias Broken : *(DoesNotExist):T;
        }
        template<typename T> struct Box { value: T; }
        template<typename T>
        struct Collection {
            visitor: functional::Consumer<const T&>;
            nestedVisitor: functional::Consumer<const Box<T>&>;
            forEach(consumer: functional::Consumer<const T&>) {}
        }
        template<typename T>
        consume(consumer: functional::Consumer<const T&>) {}
        template<typename T>
        struct Unused { callback: functional::Broken<T>; }
        struct Uses { values: Collection<int>; }
        use() { consume<int>(null); }
    )SRC");
    REQUIRE(compiler);
    CHECK(compiler->get_unit()->get_imports().empty());
    auto root = compiler->get_unit()->get_root_namespace();
    auto consumerAlias = root->get_child_namespace("functional")->get_alias("Consumer");
    REQUIRE(consumerAlias);
    auto consumerTarget = std::dynamic_pointer_cast<k::model::unresolved_callable_type>(
        consumerAlias->get_target_type());
    REQUIRE(consumerTarget);
    REQUIRE(consumerTarget->parameter_types().size() == 1);
    auto placeholder = std::dynamic_pointer_cast<k::model::unresolved_type>(
        consumerTarget->parameter_types().front());
    REQUIRE(placeholder);
    CHECK(placeholder->is_template_param_placeholder());
    CHECK_FALSE(placeholder->is_resolved());
    auto definition = root->get_aggregate("Collection");
    REQUIRE(definition);
    CHECK_FALSE(k::model::type::is_resolved(definition->get_variable("visitor")->get_type()));
    CHECK_FALSE(k::model::type::is_resolved(
        definition->get_function("forEach")->parameters().front()->get_type()));
    CHECK_FALSE(k::model::type::is_resolved(
        root->get_function("consume")->parameters().front()->get_type()));
    auto uses = root->get_aggregate("Uses");
    REQUIRE(uses);
    auto collectionType = std::dynamic_pointer_cast<k::model::struct_type>(
        uses->get_variable("values")->get_type());
    REQUIRE(collectionType);
    auto collection = collectionType->get_struct();
    REQUIRE(collection);
    auto visitor = std::dynamic_pointer_cast<k::model::callable_type>(
        collection->get_variable("visitor")->get_type());
    REQUIRE(visitor);
    REQUIRE(visitor->get_parameter_types().size() == 1);
    auto param = visitor->get_parameter_types().front();
    REQUIRE(k::model::type::is_reference(param));
    REQUIRE(k::model::type::is_const(param->get_subtype()));
    CHECK(param->get_subtype()->get_subtype() ==
          compiler->get_context_for_test()->from_string("int"));
    CHECK(k::model::type::are_equal(
        visitor, collection->get_function("forEach")->parameters().front()->get_type()));

    auto nested = std::dynamic_pointer_cast<k::model::callable_type>(
        collection->get_variable("nestedVisitor")->get_type());
    REQUIRE(nested);
    REQUIRE(nested->get_parameter_types().size() == 1);
    auto nestedParam = nested->get_parameter_types().front();
    REQUIRE(k::model::type::is_reference(nestedParam));
    REQUIRE(k::model::type::is_const(nestedParam->get_subtype()));
    auto box = std::dynamic_pointer_cast<k::model::struct_type>(
        nestedParam->get_subtype()->get_subtype());
    REQUIRE(box);
    REQUIRE(box->get_struct()->get_tpl_args().size() == 1);
    CHECK(box->get_struct()->get_tpl_args().front().type_arg ==
          compiler->get_context_for_test()->from_string("int"));
}

TEST_CASE("template alias - nested IoCompletion and plain callable aliases retain declaration types",
          "[gen][alias][template][callable][owner][declaration-scope][symbolic-alias]") {
    auto compiler = compile_model(R"SRC(
        module k;
        template<typename T, typename E>
        struct Expected { value: T; error: E; }
        namespace io {
            struct IoError { code: int; }
            struct Value { value: int; }
            alias PlainConsumer : *(const Value&);
            alias Prototype : (const Value&):Value;
            alias PlainOwner : !(const Value&):Value;
            template<typename T> alias PrototypeOf : (const T&):T;
            template<typename T> alias Owner : !(const T&):T;
            template<typename T> alias IoResult : Expected<T, IoError>;
            template<typename T> alias IoCompletion : !(IoResult<T>);
            template<typename T> struct Carrier { completion: IoCompletion<T>; }
        }
        namespace caller {
            struct T { wrong: long; }
            struct IoError { wrong: long; }
            struct Value { wrong: long; }
            template<typename T> alias IoResult : int;
            struct Uses {
                carrier: io::Carrier<Value>;
                completion: ::k::io::IoCompletion<Value>;
                consumer: io::PlainConsumer;
                prototype: io::Prototype*;
                specializedPrototype: io::PrototypeOf<io::Value>*;
                owner: io::PlainOwner;
                specializedOwner: io::Owner<io::Value>;
            }
        }
    )SRC");
    REQUIRE(compiler);
    auto root = compiler->get_unit()->get_root_namespace();
    auto io = root->get_child_namespace("io");
    auto caller = root->get_child_namespace("caller");
    REQUIRE(io);
    REQUIRE(caller);
    auto uses = caller->get_aggregate("Uses");
    REQUIRE(uses);
    auto completion = std::dynamic_pointer_cast<k::model::callable_type>(
        uses->get_variable("completion")->get_type());
    REQUIRE(completion);
    CHECK(completion->is_owner());
    REQUIRE(completion->get_parameter_types().size() == 1);
    auto result = std::dynamic_pointer_cast<k::model::struct_type>(
        completion->get_parameter_types().front());
    REQUIRE(result);
    CHECK(result->get_struct()->get_fq_name().starts_with("::k::Expected"));
    REQUIRE(result->get_struct()->get_tpl_args().size() == 2);
    CHECK(result->get_struct()->get_tpl_args()[0].type_arg ==
          caller->get_aggregate("Value")->get_struct_type());
    CHECK(result->get_struct()->get_tpl_args()[1].type_arg ==
          io->get_aggregate("IoError")->get_struct_type());
    auto carrier = std::dynamic_pointer_cast<k::model::struct_type>(
        uses->get_variable("carrier")->get_type());
    REQUIRE(carrier);
    CHECK(k::model::type::are_equal(
        carrier->get_struct()->get_variable("completion")->get_type(), completion));
    for (const auto* name : {"consumer", "prototype", "specializedPrototype", "owner", "specializedOwner"}) {
        auto callable = std::dynamic_pointer_cast<k::model::callable_type>(
            uses->get_variable(name)->get_type());
        REQUIRE(callable);
        REQUIRE(callable->get_parameter_types().size() == 1);
        auto param = callable->get_parameter_types().front();
        REQUIRE(k::model::type::is_reference(param));
        REQUIRE(k::model::type::is_const(param->get_subtype()));
        CHECK(param->get_subtype()->get_subtype() == io->get_aggregate("Value")->get_struct_type());
    }
    CHECK(k::model::type::are_equal(uses->get_variable("owner")->get_type(),
                                  uses->get_variable("specializedOwner")->get_type()));
    CHECK(k::model::type::are_equal(uses->get_variable("prototype")->get_type(),
                                  uses->get_variable("specializedPrototype")->get_type()));
    auto prototype = std::dynamic_pointer_cast<k::model::callable_type>(
        io->get_alias("Prototype")->get_target_type());
    REQUIRE(prototype);
    CHECK(prototype->is_prototype());
}

TEST_CASE("template alias - unknown concrete types are not symbolic parameters",
          "[gen][alias][template][diagnostics][symbolic-alias]") {
    std::string_view source;
    auto expectedCode = k::diag::alias_diag::ERR_ALIAS_TEMPLATE_ARG_MISMATCH;
    SECTION("An unknown argument used by a callable alias fails") {
        source = R"SRC(
            module k;
            template<typename T> alias Consumer : *(T);
            use(consumer: Consumer<DoesNotExist>) {}
        )SRC";
    }
    SECTION("An unknown argument ignored by the alias body still fails") {
        source = R"SRC(
            module k;
            template<typename T> alias Constant : int;
            use(value: Constant<DoesNotExist>) {}
        )SRC";
    }
    SECTION("An unknown concrete callable parameter in the alias body fails") {
        source = R"SRC(
            module k;
            template<typename T> alias Broken : *(DoesNotExist):T;
            use(callback: Broken<int>) {}
        )SRC";
        expectedCode = k::diag::alias_diag::ERR_ALIAS_TEMPLATE_TARGET_UNRESOLVED;
    }
    auto compiler = k::compiler::create();
    try {
        compiler->parse_source("unknown-alias-argument.k", source, true, false);
        FAIL("Expected an unresolved concrete alias argument diagnostic");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code == static_cast<unsigned int>(expectedCode));
        CHECK(error.get_diagnostic().message.find("could not be resolved") != std::string::npos);
    }
}

TEST_CASE("template alias - declaration types do not bind to use-site shadows",
          "[gen][alias][template][declaration-scope]") {
    auto compiler = compile_model(R"SRC(
        module aliases::declaration_scope;
        namespace definition {
            struct Error { code: int; }
            struct DefaultValue { value: int; }
            template<typename T, typename E>
            struct Result { value: T; error: E; }
            template<typename T = DefaultValue> alias Answer : Result<T, Error>;
            template<typename T> alias Completion : !(Answer<T>);
        }
        namespace consumer {
            struct Error { wrong: long; }
            struct DefaultValue { wrong: long; }
            struct Value { value: int; }
            struct Uses {
                result: ::aliases::declaration_scope::definition::Answer<Value>;
                fallback: definition::Answer<>;
                callback: definition::Completion<Value>;
            }
        }
    )SRC");
    REQUIRE(compiler);
    auto root = compiler->get_unit()->get_root_namespace();
    auto definition = root->get_child_namespace("definition");
    auto consumer = root->get_child_namespace("consumer");
    auto uses = consumer->get_aggregate("Uses");
    REQUIRE(uses);
    auto result = std::dynamic_pointer_cast<k::model::struct_type>(uses->get_variable("result")->get_type());
    auto fallback = std::dynamic_pointer_cast<k::model::struct_type>(uses->get_variable("fallback")->get_type());
    auto callback = std::dynamic_pointer_cast<k::model::callable_type>(uses->get_variable("callback")->get_type());
    REQUIRE(result);
    REQUIRE(fallback);
    REQUIRE(callback);
    CHECK(result->get_struct()->get_tpl_args()[0].type_arg == consumer->get_aggregate("Value")->get_struct_type());
    CHECK(result->get_struct()->get_tpl_args()[1].type_arg == definition->get_aggregate("Error")->get_struct_type());
    CHECK(fallback->get_struct()->get_tpl_args()[0].type_arg == definition->get_aggregate("DefaultValue")->get_struct_type());
    REQUIRE(callback->get_parameter_types().size() == 1);
    CHECK(callback->get_parameter_types()[0] == result);
}

TEST_CASE("template alias - owned callback carrier accepts a null decoder with canonical result types",
          "[gen][alias][template][callable][owner][null][callback-carrier]") {
    auto comp = compile_model(R"SRC(
        module gen_alias_owned_callback_carrier;
        template<typename R, typename E>
        struct Expected { value : R; error : E; }
        namespace io {
            struct IoDone {}
            struct IoError { code : int; }
            template<typename T> alias IoResult : Expected<T, IoError>;
            template<typename T> alias IoCompletion : !(IoResult<T>);
            template<typename T>
            struct IoCallbackCarrier {
                _completion : IoCompletion<T>;
                _decode : *(long):T;
                IoCallbackCarrier(completion: IoCompletion<T>, decode: *(long):T) {
                    _completion = completion;
                    _decode = decode;
                }
                ~IoCallbackCarrier() { _completion = null; }
            }
            template<typename T>
            unsupportedOperation(completion: IoCompletion<T>) : IoCallbackCarrier<T>! {
                return new IoCallbackCarrier<T>(completion, null);
            }
            makeDone(completion: IoCompletion<IoDone>) : IoCallbackCarrier<IoDone>! {
                return unsupportedOperation<IoDone>(completion);
            }
            makeInt(completion: IoCompletion<int>) : IoCallbackCarrier<int>! {
                return unsupportedOperation<int>(completion);
            }
        }
    )SRC");
    REQUIRE(comp);
    auto root = comp->get_unit()->get_root_namespace();
    auto io = root->get_child_namespace("io");
    REQUIRE(io);
    auto done = io->get_aggregate("IoDone");
    auto error = io->get_aggregate("IoError");
    REQUIRE(done);
    REQUIRE(error);
    CHECK(done->get_fq_name() == "::gen_alias_owned_callback_carrier::io::IoDone");
    CHECK(error->get_fq_name() == "::gen_alias_owned_callback_carrier::io::IoError");

    for (const auto& name : {"makeDone", "makeInt"}) {
        auto factory = io->get_function(name);
        REQUIRE(factory);
        REQUIRE(factory->parameters().size() == 1);
        auto completion = std::dynamic_pointer_cast<k::model::callable_type>(
            factory->parameters().front()->get_type());
        REQUIRE(completion);
        CHECK(completion->is_owner());
        REQUIRE(completion->get_parameter_types().size() == 1);
        auto result = std::dynamic_pointer_cast<k::model::struct_type>(
            completion->get_parameter_types().front());
        REQUIRE(result);
        auto expected = result->get_struct();
        REQUIRE(expected);
        REQUIRE(expected->get_tpl_args().size() == 2);
        CHECK(expected->get_tpl_args()[1].type_arg == error->get_struct_type());
        if (std::string{name} == "makeDone") {
            CHECK(expected->get_tpl_args()[0].type_arg == done->get_struct_type());
        }

        auto carrierType = std::dynamic_pointer_cast<k::model::struct_type>(
            factory->get_return_type()->get_subtype());
        REQUIRE(carrierType);
        auto carrier = carrierType->get_struct();
        REQUIRE(carrier);
        REQUIRE(carrier->constructors().size() == 1);
        const auto& params = carrier->constructors().front()->parameters();
        REQUIRE(params.size() == 2);
        CHECK(k::model::type::are_equal(params[0]->get_type(), completion));
        auto decoder = std::dynamic_pointer_cast<k::model::callable_type>(params[1]->get_type());
        REQUIRE(decoder);
        CHECK(decoder->is_nullable());
        CHECK(k::model::type::are_equal(
            decoder->get_return_type(), expected->get_tpl_args()[0].type_arg));
    }
}

TEST_CASE("template alias - owned IoCompletion moves through forwarding and carrier construction",
          "[gen][alias][template][callable][owner][move][lifecycle][callback-carrier]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_owned_completion_lifecycle;
        destructions: int = 0;
        total: int = 0;
        struct Resource {
            value: int;
            Resource(v: int) : value(v) {}
            ~Resource() { destructions += 1; }
        }
        template<typename T>
        struct IoResult { value: T; }
        template<typename T> alias IoCompletion : !(IoResult<T>);
        template<typename T>
        struct IoCallbackCarrier {
            _completion: IoCompletion<T>;
            _decode: *(long):T;
            IoCallbackCarrier(completion: IoCompletion<T>, decode: *(long):T)
                : _completion(completion), _decode(decode) {}
            ~IoCallbackCarrier() { _completion = null; }
            complete(value: T) {
                result: IoResult<T>;
                result.value = value;
                _completion(result);
            }
        }
        template<typename T>
        forward(completion: IoCompletion<T>) : IoCallbackCarrier<T>! {
            carrier: IoCallbackCarrier<T>! = new IoCallbackCarrier<T>(completion, null);
            if (completion != null) return null;
            return carrier;
        }
        run() : int {
            destructions = 0;
            total = 0;
            {
                resource: Resource! = new Resource(20);
                completion: IoCompletion<int> = [resource](result: IoResult<int>) {
                    total += resource.value + result.value;
                };
                carrier: IoCallbackCarrier<int>! = forward<int>(completion);
                if (completion != null || resource != null || carrier == null) return -1;
                if (destructions != 0) return -2;
                carrier.complete(22);
                delete carrier;
                if (destructions != 1) return -3;
            }
            if (destructions != 1) return -4;
            return total;
        }
    )SRC");
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("template alias — renames a template aggregate", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_01;
        template<typename T>
        struct Box { v : T; }
        template<typename T> alias BoxOf : Box<T>;
        f() : int {
            b : BoxOf<int>;
            b.v = 41;
            return b.v + 1;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — renames a bare parameter", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_02;
        template<typename T> alias Same : T;
        f(v : Same<int>) : Same<int> { return v * 2; }
        g() : int { return f(21); }
    )SRC");
    auto g = jit->lookup_symbol<int(*)()>("g");
    REQUIRE(g != nullptr);
    CHECK(g() == 42);
}

TEST_CASE("template alias — renames an addresser of a parameter", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_03;
        template<typename T> alias Ptr : T*;
        f() : int {
            v : int = 42;
            p : Ptr<int> = &v;
            return *p;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — renames an array of a parameter", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_04;
        template<typename T> alias Arr : T[3];
        f() : int {
            a : Arr<int>;
            a[0] = 40;
            a[1] = 2;
            return a[0] + a[1];
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — partially applies a multi-parameter template", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_05;
        template<typename T, typename U>
        struct Pair { a : T; b : U; }
        template<typename U> alias IntPair : Pair<int, U>;
        struct Holder { p : IntPair<int>; }
        f() : int {
            h : Holder;
            h.p.a = 40;
            h.p.b = 2;
            return h.p.a + h.p.b;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — several parameters are substituted positionally", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_06;
        template<typename T, typename U>
        struct Pair { a : T; b : U; }
        template<typename A, typename B> alias P : Pair<B, A>;
        f() : long {
            p : P<int, long>;
            p.a = 40;
            p.b = 2;
            return p.a + (long)p.b;
        }
    )SRC");
    auto f = jit->lookup_symbol<long(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — a default template argument is applied", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_07;
        template<typename T>
        struct Box { v : T; }
        template<typename T = int> alias BoxOf : Box<T>;
        f() : int {
            b : BoxOf<>;
            b.v = 42;
            return b.v;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — renames another parameterised alias", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_08;
        template<typename T> alias Ptr : T*;
        template<typename T> alias Handle : Ptr<T>;
        f() : int {
            v : int = 42;
            h : Handle<int> = &v;
            return *h;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — a nested template argument is substituted", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_09;
        template<typename T> struct Box { v : T; }
        template<typename T, typename U> struct Pair { a : T; b : U; }
        template<typename T> alias BoxPair : Pair<Box<T>, int>;
        f() : int {
            p : BoxPair<int>;
            p.a.v = 40;
            p.b = 2;
            return p.a.v + p.b;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — a nested alias argument is substituted", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_10;
        template<typename T> struct Box { v : T; }
        template<typename T> alias BoxOf : Box<T>;
        template<typename T> alias BoxOfBox : BoxOf<Box<T>>;
        f() : int {
            b : BoxOfBox<int>;
            b.v.v = 42;
            return b.v.v;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template typedef — an explicit cast converts from the renamed type", "[gen][typedef][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_11;
        template<typename T> typedef Id : T;
        f() : int {
            n : int = 40;
            a : Id<int> = (Id<int>)n;
            b : Id<int> = 2;
            return (int)a + (int)b;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template typedef — the renamed type accepts the strong alias without a cast", "[gen][typedef][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_12;
        template<typename T> typedef Id : T;
        f() : int {
            a : Id<int> = 42;
            n : int = a;
            return n;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — too many template arguments are rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_13;
        template<typename T> alias Same : T;
        f(v : Same<int, long>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — a missing template argument is rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_14;
        template<typename T, typename U> alias Two : T;
        f(v : Two<int>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — arguments given to a plain alias are rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_15;
        alias Plain : int;
        f(v : Plain<int>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — a value template parameter is rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_16;
        template<int N> alias Buf : int;
        f(v : Buf<4>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — an unknown target is rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_17;
        template<typename T> alias Nope : DoesNotExist<T>;
        f(v : Nope<int>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}
