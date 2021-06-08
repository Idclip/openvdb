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
    { "return;",          Node::Ptr(new Keyword(KeywordToken::RETURN)) },
    { "return a;",        Node::Ptr(new Keyword(KeywordToken::RETURN, new Local("a"))) },
    { "return a+b;",      Node::Ptr(new Keyword(KeywordToken::RETURN, new BinaryOperator(new Local("a"), new Local("b"), OperatorToken::PLUS))) },
    { "return -b;",       Node::Ptr(new Keyword(KeywordToken::RETURN, new UnaryOperator(new Local("b"), OperatorToken::MINUS))) },
    { "return a?b:c;",    Node::Ptr(new Keyword(KeywordToken::RETURN, new TernaryOperator(new Local("a"), new Local("b"), new Local("c")))) },
    { "return a=b;",      Node::Ptr(new Keyword(KeywordToken::RETURN, new AssignExpression(new Local("a"), new Local("b")))) },
    { "return a();",      Node::Ptr(new Keyword(KeywordToken::RETURN, new FunctionCall("a"))) },
    { "return a++;",      Node::Ptr(new Keyword(KeywordToken::RETURN, new Crement(new Local("a"), Crement::Operation::Increment, /*post=*/true))) },
    { "return a[0];",     Node::Ptr(new Keyword(KeywordToken::RETURN, new ArrayUnpack(new Local("a"), new Value<int32_t>(0)))) },
    { "return {a,b,c};",  Node::Ptr(new Keyword(KeywordToken::RETURN, new ArrayPack({new Local("a"), new Local("b"), new Local("c")}))) },
    { "return (a);",      Node::Ptr(new Keyword(KeywordToken::RETURN, new Local("a"))) },
    // break and continue
    { "break;", Node::Ptr(new Keyword(KeywordToken::BREAK)) },
    { "continue;", Node::Ptr(new Keyword(KeywordToken::CONTINUE)) }
};

}

class TestKeywordNode : public CppUnit::TestCase
{
public:

    CPPUNIT_TEST_SUITE(TestKeywordNode);
    CPPUNIT_TEST(testSyntax);
    CPPUNIT_TEST(testASTNode);
    CPPUNIT_TEST_SUITE_END();

    void testSyntax() { TEST_SYNTAX_PASSES(tests); }
    void testASTNode();
};

CPPUNIT_TEST_SUITE_REGISTRATION(TestKeywordNode);

void TestKeywordNode::testASTNode()
{
    for (const auto& test : tests) {
        const std::string& code = test.first;
        const Node* expected = test.second.get();
        const Tree::ConstPtr tree = parse(code.c_str());
        CPPUNIT_ASSERT_MESSAGE(ERROR_MSG("No AST returned", code), static_cast<bool>(tree));

        // get the first statement
        const Node* result = tree->child(0)->child(0);
        CPPUNIT_ASSERT(result);
        const Keyword* resultAsKeyword = static_cast<const Keyword*>(result);
        CPPUNIT_ASSERT(resultAsKeyword);
        CPPUNIT_ASSERT_MESSAGE(ERROR_MSG("Invalid AST node", code),
            Node::KeywordNode == result->nodetype());

        std::vector<const Node*> resultList, expectedList;
        linearize(*result, resultList);
        linearize(*expected, expectedList);

        if (!unittest_util::compareLinearTrees(expectedList, resultList)) {
            std::ostringstream os;
            os << "\nExpected:\n";
            openvdb::ax::ast::print(*expected, true, os);
            os << "Result:\n";
            openvdb::ax::ast::print(*result, true, os);
            CPPUNIT_FAIL(ERROR_MSG("Mismatching Trees for Return code", code) + os.str());
        }
    }
}

