
#include <string>
#include <unordered_set>

#include "hinting/planner_hints.hpp"

#include "antlr4-runtime.h"
#include "HintBlockLexer.h"
#include "HintBlockParser.h"
#include "HintBlockBaseListener.h"

// Undefine ANTLR's INVALID_INDEX macro to avoid conflict with DuckDB's DConstants::INVALID_INDEX
#ifdef INVALID_INDEX
#undef INVALID_INDEX
#endif

namespace tud {

class HintParser : public HintBlockBaseListener {
public:
    explicit HintParser(PlannerHints &planner_hints) : planner_hints_(planner_hints) {};

    void enterJoin_op_hint(HintBlockParser::Join_op_hintContext *ctx) override {
        std::unordered_set<std::string> relations;
        for (const auto &rel_ctx : ctx->binary_rel_id()->relation_id()) {
            relations.insert(rel_ctx->getText());
        }

        for (const auto &rel_ctx : ctx->relation_id()) {
            relations.insert(rel_ctx->getText());
        }

        OperatorHint ophint;

        if (ctx->NESTLOOP()) {
            ophint = OperatorHint::NLJ;
        } else if (ctx->HASHJOIN()) {
            ophint = OperatorHint::HASH_JOIN;
        } else if (ctx->MERGEJOIN()) {
            ophint = OperatorHint::MERGE_JOIN;
        } else {
            throw std::runtime_error("Join operator hint not supported. Currently must be one of: NESTLOOP, HASHJOIN, MERGEJOIN.");
        }

        planner_hints_.AddOperatorHint(relations, ophint);
    }

    void enterCardinality_hint(HintBlockParser::Cardinality_hintContext *ctx) override {
        std::unordered_set<std::string> relations;
        for (const auto &rel_ctx : ctx->relation_id()) {
            relations.insert(rel_ctx->getText());
        }

        auto raw_card = ctx->INT()->getText();
        double card = std::stod(raw_card);
        planner_hints_.AddCardinalityHint(relations, card);
    }

private:
    PlannerHints &planner_hints_;
};

void PlannerHints::ParseHints() {
    auto hintblock_start = raw_query_.find("/*=quack_lab=");
    auto hintblock_end = raw_query_.find("*/");

    if (hintblock_start == std::string::npos || hintblock_end == std::string::npos) {
        contains_hint_ = false;
        return;
    }

    // make sure to include the hint prefix and suffix for the parser
    auto raw_hint = raw_query_.substr(hintblock_start, hintblock_end - hintblock_start + 2);

    antlr4::ANTLRInputStream parser_input(raw_hint);
    HintBlockLexer lexer(&parser_input);
    antlr4::CommonTokenStream tokens(&lexer);
    HintBlockParser parser(&tokens);

    antlr4::tree::ParseTree *tree = parser.hint_block();
    HintParser listener(*this);
    antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);
}

} // namespace tud
