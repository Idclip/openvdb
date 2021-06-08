// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

#include <openvdb_ax/ast/AST.h>
#include <openvdb_ax/ast/Scanners.h>
#include <openvdb_ax/ast/PrintTree.h>
#include <openvdb_ax/Exceptions.h>

#include "../util.h"

#include <cppunit/extensions/HelperMacros.h>

#include <string>

using namespace openvdb::ax::ast;
using namespace openvdb::ax::ast::tokens;

namespace {

static const unittest_util::CodeTests tests =
{
    { "int f() {}",                               Node::Ptr(new Function("f",  CoreType::INT32, new Block)) },
    { "void f() {}",                              Node::Ptr(new Function("f",  CoreType::VOID,  new Block)) },
    { "float _f() {}",                            Node::Ptr(new Function("_f", CoreType::FLOAT, new Block)) },
    { "int f(int a) {}",                          Node::Ptr(new Function("f",  CoreType::INT32, { new DeclareLocal(CoreType::INT32, new Local("a")) }, new Block)) },
    { "float f(float a) {}",                      Node::Ptr(new Function("f",  CoreType::FLOAT, { new DeclareLocal(CoreType::FLOAT, new Local("a")) }, new Block)) },
    { "double f(double a) {}",                    Node::Ptr(new Function("f",  CoreType::DOUBLE, { new DeclareLocal(CoreType::DOUBLE, new Local("a")) }, new Block)) },
    { "vec3i f(vec3i a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC3I, { new DeclareLocal(CoreType::VEC3I, new Local("a")) }, new Block)) },
    { "vec3f f(vec3f a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC3F, { new DeclareLocal(CoreType::VEC3F, new Local("a")) }, new Block)) },
    { "vec3d f(vec3d a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC3D, { new DeclareLocal(CoreType::VEC3D, new Local("a")) }, new Block)) },
    { "vec4i f(vec4i a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC4I, { new DeclareLocal(CoreType::VEC4I, new Local("a")) }, new Block)) },
    { "vec4f f(vec4f a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC4F, { new DeclareLocal(CoreType::VEC4F, new Local("a")) }, new Block)) },
    { "vec4d f(vec4d a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC4D, { new DeclareLocal(CoreType::VEC4D, new Local("a")) }, new Block)) },
    { "vec2i f(vec2i a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC2I, { new DeclareLocal(CoreType::VEC2I, new Local("a")) }, new Block)) },
    { "vec2f f(vec2f a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC2F, { new DeclareLocal(CoreType::VEC2F, new Local("a")) }, new Block)) },
    { "vec2d f(vec2d a) {}",                      Node::Ptr(new Function("f",  CoreType::VEC2D, { new DeclareLocal(CoreType::VEC2D, new Local("a")) }, new Block)) },
    { "mat3f f(mat3f a) {}",                      Node::Ptr(new Function("f",  CoreType::MAT3F, { new DeclareLocal(CoreType::MAT3F, new Local("a")) }, new Block)) },
    { "mat3d f(mat3d a) {}",                      Node::Ptr(new Function("f",  CoreType::MAT3D, { new DeclareLocal(CoreType::MAT3D, new Local("a")) }, new Block)) },
    { "mat4f f(mat4f a) {}",                      Node::Ptr(new Function("f",  CoreType::MAT4F, { new DeclareLocal(CoreType::MAT4F, new Local("a")) }, new Block)) },
    { "mat4d f(mat4d a) {}",                      Node::Ptr(new Function("f",  CoreType::MAT4D, { new DeclareLocal(CoreType::MAT4D, new Local("a")) }, new Block)) },
    { "float f(string a) {}",                     Node::Ptr(new Function("f",  CoreType::FLOAT, { new DeclareLocal(CoreType::STRING, new Local("a")) }, new Block)) },
    { "float f(float a, int b) {}",               Node::Ptr(new Function("f",  CoreType::FLOAT, {
                                                            new DeclareLocal(CoreType::FLOAT, new Local("a")), new DeclareLocal(CoreType::INT32, new Local("b"))
                                                        }, new Block))
                                                    },
    { "float f(float a, int b) { int c; }",       Node::Ptr(new Function("f",  CoreType::FLOAT, {
                                                            new DeclareLocal(CoreType::FLOAT, new Local("a")), new DeclareLocal(CoreType::INT32, new Local("b"))
                                                        }, new Block(new DeclareLocal(CoreType::INT32, new Local("c")))))
                                                    },
    { "float f(float a, int b) { a, b, c; }",     Node::Ptr(new Function("f",  CoreType::FLOAT, {
                                                            new DeclareLocal(CoreType::FLOAT, new Local("a")), new DeclareLocal(CoreType::INT32, new Local("b"))
                                                        }, new Block(new CommaOperator({
                                                            new Local("a"), new Local("b"), new Local("c")
                                                        }))))
                                                    },
    { "float f(float a, int b) { return 1.0f; }", Node::Ptr(new Function("f",  CoreType::FLOAT, {
                                                            new DeclareLocal(CoreType::FLOAT, new Local("a")), new DeclareLocal(CoreType::INT32, new Local("b"))
                                                        }, new Block(new Keyword(tokens::KeywordToken::RETURN, new Value<float>(1.0f)))))
                                                    },
    { "float f(float a, int b) { int b() {} }",   Node::Ptr(new Function("f",  CoreType::FLOAT, {
                                                            new DeclareLocal(CoreType::FLOAT, new Local("a")), new DeclareLocal(CoreType::INT32, new Local("b"))
                                                        }, new Block(new Function("b", CoreType::INT32, new Block))))
                                                    },
};

}

class TestFunctionNode : public CppUnit::TestCase
{
public:

    CPPUNIT_TEST_SUITE(TestFunctionNode);
    CPPUNIT_TEST(testSyntax);
    CPPUNIT_TEST(testASTNode);
    CPPUNIT_TEST_SUITE_END();

    void testSyntax() { TEST_SYNTAX_PASSES(tests); }
    void testASTNode();
};

CPPUNIT_TEST_SUITE_REGISTRATION(TestFunctionNode);

void TestFunctionNode::testASTNode()
{
    for (const auto& test : tests) {
        const std::string& code = test.first;
        const Node* expected = test.second.get();
        const Tree::ConstPtr tree = parse(code.c_str());
        CPPUNIT_ASSERT_MESSAGE(ERROR_MSG("No AST returned", code), static_cast<bool>(tree));

        // get the first statement
        const Node* result = tree->child(0)->child(0);
        CPPUNIT_ASSERT(result);
        CPPUNIT_ASSERT_MESSAGE(ERROR_MSG("Invalid AST node", code),
            Node::FunctionNode == result->nodetype());

        std::vector<const Node*> resultList, expectedList;
        linearize(*result, resultList);
        linearize(*expected, expectedList);

        if (!unittest_util::compareLinearTrees(expectedList, resultList)) {
            std::ostringstream os;
            os << "\nExpected:\n";
            openvdb::ax::ast::print(*expected, true, os);
            os << "Result:\n";
            openvdb::ax::ast::print(*result, true, os);
            CPPUNIT_FAIL(ERROR_MSG("Mismatching Trees for Function Call code", code) + os.str());
        }
    }
}

