/* Implementation of recursive-descent parser.
 * Author: Shihao Song
 * Modified by: Naga Kandasamy
 * Date modified: October 25, 2021
 * 
 * Student names(s): FIXME
 * Date modified: FIXME
 */

#include "parser/parser.hh"

namespace Frontend
{
Parser::Parser(const char* fn) : lexer(new Lexer(fn))
{
    // Load tokens from Lexer
    lexer->getToken(cur_token);
    lexer->getToken(next_token);

    // Fill in the pre-built functions: printVarInt and printVarFloat 
    std::vector<ValueType::Type> arg_types;
    ValueType::Type ret_type = ValueType::Type::VOID;
    FuncRecord record;

    // printVarInt prints an integer value to stdout
    arg_types.push_back(ValueType::Type::INT);
    record.ret_type = ret_type;
    record.arg_types = arg_types;
    record.is_built_in = true;
    func_def_tracker.insert({"printVarInt", record});

    // printVarFloat prints a floating-point value to stdout
    arg_types.clear();
    arg_types.push_back(ValueType::Type::FLOAT);
    record.ret_type = ret_type;
    record.arg_types = arg_types;
    record.is_built_in = true;
    func_def_tracker.insert({"printVarFloat", record});

    parseProgram();
}

// Advance current and lookahead tokens
void Parser::advanceTokens()
{
    cur_token = next_token;
    lexer->getToken(next_token);
}

// Parse program
void Parser::parseProgram()
{
    // We assume the program starts with a function 
    // Current implementation does not support global variables or structures
    while (!cur_token.isTokenEOF()) {
        ValueType::Type ret_type;
        std::unique_ptr<Identifier> iden;
        std::vector<FuncStatement::Argument> args;
        std::vector<std::shared_ptr<Statement>> codes;

        // Parse return type from function
        ret_type = ValueType::typeTokenToValueType(cur_token);
        if (ret_type == ValueType::Type::MAX) {
            std::cerr << "[Error] parseProgram: unsupported return type\n"
                      << "[Line] " << cur_token.getLine() << "\n";
            exit(0);
        }
                
        // Parse function name
        advanceTokens();
        iden = std::make_unique<Identifier>(cur_token);
        if (!next_token.isTokenLP()) {
            std::cerr << "[Error] Incorrect function defition.\n "
                      << "[Line] " << cur_token.getLine() << "\n";
            exit(0);
        }

        advanceTokens();
        assert(cur_token.isTokenLP());

        // Track local variables
        std::unordered_map<std::string,ValueType::Type> local_vars;
        local_vars_tracker.push_back(&local_vars);

        // Extract arguments
        while (!cur_token.isTokenRP()) {
            advanceTokens();
            if (cur_token.isTokenRP()) break; // Function accepts no arguments

            std::string arg_type = cur_token.getLiteral();

            advanceTokens();
            std::unique_ptr<Identifier> iden(new Identifier(cur_token));
            FuncStatement::Argument arg(arg_type, iden);
            args.push_back(arg);

            recordLocalVars(arg);

            advanceTokens();
        }
        assert(cur_token.isTokenRP()); // Check if right parenthesis terminates function definition

        // Record the function definition
        recordDefs(iden->getLiteral(), ret_type, args);

        // Parse function body
        advanceTokens();
        assert(cur_token.isTokenLBrace());

        while (true) {
            advanceTokens();
            if (cur_token.isTokenRBrace())
                    break; // End of function body

            parseStatement(iden->getLiteral(), codes);

            // Check if we just parsed an if/for/while statement
            if (codes.back()->isStatementIf() || 
                codes.back()->isStatementFor() || 
                codes.back()->isStatementWhile()) {
                // This ending right brace is from the statement and not end of function body
                // We are still within the function body
                assert(cur_token.isTokenRBrace());
            }
            else {
                if (cur_token.isTokenRBrace())
                    break;
            }
        }
        
        std::unique_ptr<Statement> func_proto
            (new FuncStatement(ret_type, 
                               iden, 
                               args, 
                               codes,
                               local_vars));
        
        local_vars_tracker.pop_back();

        program.addStatement(func_proto);
        
        advanceTokens();
    }
}

// Parse statement as per the grammar 
void Parser::parseStatement(std::string &cur_func_name, 
                            std::vector<std::shared_ptr<Statement>> &codes)
{
    // Parse if statement
    if (cur_token.isTokenIf()) {
        auto code = parseIfStatement(cur_func_name);
        codes.push_back(std::move(code));
        return;
    }

    // Parse for statement
    if (cur_token.isTokenFor()) {
        auto code = parseForStatement(cur_func_name);
        codes.push_back(std::move(code));
        return;
    }

    // Parse while statement 
    if (cur_token.isTokenWhile()) {
        auto code = parseWhileStatement(cur_func_name);
        codes.push_back(std::move(code));
        return;
    }

    // Parse function-call statement
    if (auto [is_def, is_built_in] = 
            isFuncDef(cur_token.getLiteral());
        is_def) {
        Statement::StatementType call_type = is_built_in ?
            Statement::StatementType::BUILT_IN_CALL_STATEMENT :
            Statement::StatementType::NORMAL_CALL_STATEMENT;

        auto code = parseCall();
        std::unique_ptr<CallStatement> call = 
            std::make_unique<CallStatement>(code, call_type); 

        codes.push_back(std::move(call));

        return;
    }

    // Parse return statement
    if (cur_token.isTokenReturn()) {
        advanceTokens();
        cur_expr_type = getFuncRetType(cur_func_name);

        auto ret = parseExpression();
        std::unique_ptr<RetStatement> ret_statement = 
            std::make_unique<RetStatement>(ret);

        codes.push_back(std::move(ret_statement));

        return;
    }

    // Parse variable-assignment statement
    if (isTokenTypeKeyword(cur_token) || cur_token.isTokenIden()) {
        auto code = parseAssnStatement();

        codes.push_back(std::move(code));

        return;
    }
}

// Parse assignment statement 
std::unique_ptr<Statement> Parser::parseAssnStatement()
{
    // Allocating new variables
    // Example: int variableName = value;
    // Example: float variableName = value; 
    if (isTokenTypeKeyword(cur_token)) {
        Token type_token = cur_token;

        advanceTokens();
        // Check if variable has been previously defined
        if (auto [already_defined, type] = isVarAlreadyDefined(cur_token);
            already_defined) {
            std::cerr << "[Error] Re-definition of "
                      << cur_token.getLiteral() << "\n";
            std::cerr << "[Line] " << cur_token.getLine() << "\n";
            exit(0);
        }

        // Check if variable is of type array
        bool is_array = (next_token.isTokenLBracket()) ? true : false;

        recordLocalVars(cur_token, type_token, is_array);

        // Build node for identifier on left-hand side of assignment statement
        std::unique_ptr<Expression> iden =
            std::make_unique<LiteralExpression>(cur_token);

        // Build node for expression on right-hand side of assignment statement
        std::unique_ptr<Expression> expr;
        
        if (!is_array) {
            if(next_token.isTokenEqual()) {
                advanceTokens();
                advanceTokens();
                expr = parseExpression();
            } else {
                assert(next_token.isTokenSemicolon());
                expr = std::make_unique<UndefinedExpression>(cur_token);
            }
        }
        else {
            expr = parseArrayExpr();
        }

        // Build node for assignment statement
        std::unique_ptr<Statement> statement = 
            std::make_unique<AssnStatement>(iden, expr);

        return statement;
    }
    else {  // Variable must have been previously defined 
        auto [already_defined, type] = isVarAlreadyDefined(cur_token);
        if (!already_defined) {
            std::cerr << "[Error] Undefined variable of "
                      << cur_token.getLiteral() << "\n";
            std::cerr << "[Line] " << cur_token.getLine() << "\n";
            exit(0);
        }

        cur_expr_type = ValueType::Type::MAX;

        // Build node on left-hand side of assignment statement 
        auto iden = parseExpression();

        assert(cur_token.isTokenEqual());
        advanceTokens();

        // Build node on right-hand side of assignment statement
        std::unique_ptr<Expression> expr;
        if (type == ValueType::Type::INT_ARRAY || type == ValueType::Type::FLOAT_ARRAY) {
            cur_expr_type = (type == ValueType::Type::INT_ARRAY)
                            ? type = ValueType::Type::INT
                            : type = ValueType::Type::FLOAT;
        }
        else {
            cur_expr_type = type;
        }

        expr = parseExpression();
        
        // Build node for assignment statment
        std::unique_ptr<Statement> statement = 
            std::make_unique<AssnStatement>(iden, expr);

        return statement;
    }
}

// Parse array expression 
std::unique_ptr<Expression> Parser::parseArrayExpr()
{
    advanceTokens();
    assert(cur_token.isTokenLBracket());

    advanceTokens();
    // num_ele, the number of elements, must be an integer
    auto swap = cur_expr_type;
    cur_expr_type = ValueType::Type::INT;
    auto num_ele = parseExpression();
    cur_expr_type = swap;
    if (!(num_ele->isExprLiteral())) {
        std::cerr << "[Error] Number of array elements "
                  << "must be a single integer. \n"
                  << "[Line] " << cur_token.getLine() << "\n";
        exit(0);
    }

    auto num_ele_lit = static_cast<LiteralExpression*>(num_ele.get());
    
    if (!(num_ele_lit->isLiteralInt())) {
        std::cerr << "[Error] Number of array elements "
                  << "must be a single integer. \n"
                  << "[Line] " << cur_token.getLine() << "\n";
        exit(0);
    }

    int num_eles_int = stoi(num_ele_lit->getLiteral());
    if (num_eles_int <= 1) {
        std::cerr << "[Error] Number of array elements "
                  << "must be larger than 1. \n"
                  << "[Line] " << cur_token.getLine() << "\n";
        exit(0);
    }

    assert(cur_token.isTokenRBracket());

    advanceTokens();
    assert(cur_token.isTokenEqual());

    advanceTokens();
    assert(cur_token.isTokenLBrace());

    std::vector<std::shared_ptr<Expression>> eles;
    if (!next_token.isTokenRBrace()) {
        advanceTokens();
        while (!cur_token.isTokenRBrace()) {
            eles.push_back(parseExpression());
            if (cur_token.isTokenComma())
                advanceTokens();
        }

        // We make sure consistent number of elements
        if (num_eles_int != eles.size()) {
            std::cerr << "[Error] Accpeted format: "
                      << "(1) pre-allocation style - array<int> x[10] = {} "
                      << "(2) #initials == #elements - "
                      << "array<int> x[2] = {1, 2} \n"
                      << "[Line] " << cur_token.getLine() << "\n";
            exit(0);
        }
    }
    else {
        advanceTokens();
    }

    advanceTokens();

    std::unique_ptr<Expression> ret = 
        std::make_unique<ArrayExpression>(num_ele, eles);

    return ret;
}

// Parse array index 
std::unique_ptr<Expression> Parser::parseIndex()
{
    std::unique_ptr<Identifier> iden(new Identifier(cur_token));

    advanceTokens();
    assert(cur_token.isTokenLBracket());

    advanceTokens();

    // Index must be an integer
    auto swap = cur_expr_type;
    cur_expr_type = ValueType::Type::INT;
    auto idx = parseExpression();
    cur_expr_type = swap;

    std::unique_ptr<Expression> ret = 
        std::make_unique<IndexExpression>(iden, idx);

    assert(cur_token.isTokenRBracket());

    return ret;
}

// Parse function-call statement
std::unique_ptr<Expression> Parser::parseCall()
{
    // Parse function name 
    std::unique_ptr<Identifier> def(new Identifier(cur_token));

    advanceTokens();
    assert(cur_token.isTokenLP());

    advanceTokens();

    // Parse arguments passed to function 
    std::vector<std::shared_ptr<Expression>> args;

    auto &arg_types = getFuncArgTypes(def->getLiteral());
    unsigned idx = 0;
    while (!cur_token.isTokenRP()) {
        if (cur_token.isTokenRP())
            break;

        auto swap = cur_expr_type;
        cur_expr_type = arg_types[idx++];
        args.push_back(parseExpression());
        cur_expr_type = swap;

        if (cur_token.isTokenRP())
            break;

        advanceTokens();
    }

    // Build node for the function call
    std::unique_ptr<Expression> ret = 
        std::make_unique<CallExpression>(def, args);

    return ret;
}

// Parse Boolean condition 
std::unique_ptr<Condition> Parser::parseCondition()
{
    // The type must be consistent
    auto swap = cur_expr_type;
    bool is_index = (next_token.isTokenLBracket()) ? 
                    true : false;
    cur_expr_type = getTokenType(cur_token, is_index);

    // Parse left-hand side 
    auto cond_left = parseExpression();

    // Get the relational operator 
    std::string comp_opr_str = cur_token.getLiteral();
    if (next_token.isTokenEqual()) {
        comp_opr_str += next_token.getLiteral();
        advanceTokens();
    }

    // Parse right-hand side 
    advanceTokens();
    auto cond_right = parseExpression();

    // Build condition object
    std::unique_ptr<Condition> cond = 
        std::make_unique<Condition>(cond_left,
                                    cond_right,
                                    comp_opr_str,
                                    cur_expr_type);
    cur_expr_type = swap;

    return cond;
}

// Parse if-else statement 
std::unique_ptr<Statement> Parser::parseIfStatement(std::string& 
                                                    parent_func_name)
{
    advanceTokens();
    assert(cur_token.isTokenLP());

    advanceTokens();

    // Parse the Boolean condition 
    auto cond = parseCondition();

    // Parse taken block
    advanceTokens();
    assert(cur_token.isTokenLBrace());

    std::vector<std::shared_ptr<Statement>> taken_block_codes;
    std::unordered_map<std::string,ValueType::Type> taken_block_local_vars;
    local_vars_tracker.push_back(&taken_block_local_vars);
    while (true) {
        advanceTokens();
        if (cur_token.isTokenRBrace())
            break;

        parseStatement(parent_func_name, taken_block_codes);

        // Check if we just finished an if/for statement
        if (taken_block_codes.back()->isStatementIf() ||
            taken_block_codes.back()->isStatementFor() ||
            taken_block_codes.back()->isStatementWhile()) {
            // This RBrace is from the statement,
            // should not terminate.
            assert(cur_token.isTokenRBrace());
        }
        else {
            if (cur_token.isTokenRBrace())
                break;
        }
    }
    
    assert(cur_token.isTokenRBrace());
    local_vars_tracker.pop_back();

    // Parse else block
    std::vector<std::shared_ptr<Statement>> not_taken_block_codes;
    std::unordered_map<std::string,
                       ValueType::Type> not_taken_block_local_vars;

    if (next_token.isTokenElse()) {
        advanceTokens();
        local_vars_tracker.push_back(&not_taken_block_local_vars);
        advanceTokens();
        while (true) {
            advanceTokens();
            if (cur_token.isTokenRBrace())
                break;

            parseStatement(parent_func_name, not_taken_block_codes);
            // We just finished an if/for statement
            if (not_taken_block_codes.back()->isStatementIf() ||
                not_taken_block_codes.back()->isStatementFor() ||
                not_taken_block_codes.back()->isStatementWhile()) {
                // This RBrace is from the statement,
                // should not terminate.
                assert(cur_token.isTokenRBrace());
            }
            else {
                if (cur_token.isTokenRBrace())
                    break;
            }
        }
        assert(cur_token.isTokenRBrace());
        local_vars_tracker.pop_back();
    }

    std::unique_ptr<Statement> if_statement = 
        std::make_unique<IfStatement>(cond, 
                                      taken_block_codes,
                                      not_taken_block_codes,
                                      taken_block_local_vars,
                                      not_taken_block_local_vars);
    
    assert(cur_token.isTokenRBrace());
    return if_statement;
}

std::unique_ptr<Statement> Parser::parseForStatement(std::string& 
                                                     parent_func_name)
{
    advanceTokens();
    assert(cur_token.isTokenLP());
    advanceTokens();

    std::unique_ptr<Statement>start_stmt = parseAssnStatement();
    assert(cur_token.isTokenSemicolon());
    advanceTokens();

    auto stop_cond = parseCondition();
    assert(cur_token.isTokenSemicolon());
    advanceTokens();

    std::unique_ptr<Statement>step_stmt = parseAssnStatement();
    assert(cur_token.isTokenRP());

    // Parse for block
    advanceTokens();
    assert(cur_token.isTokenLBrace());

    std::vector<std::shared_ptr<Statement>> for_block_codes;
    std::unordered_map<std::string,ValueType::Type> for_block_local_vars;
    local_vars_tracker.push_back(&for_block_local_vars);
    while (true) {
        advanceTokens();
        if (cur_token.isTokenRBrace())
            break;

        parseStatement(parent_func_name, for_block_codes);

        // Check if we just finished an if/for statement
        if (for_block_codes.back()->isStatementIf() ||
            for_block_codes.back()->isStatementFor() ||
            for_block_codes.back()->isStatementWhile()) {
            // This RBrace is from the statement,
            // should not terminate.
            assert(cur_token.isTokenRBrace());
        }
        else {
            if (cur_token.isTokenRBrace())
                break;
        }
    }

    std::unique_ptr<Statement> for_statement = 
        std::make_unique<ForStatement>(start_stmt, 
                                      stop_cond,
                                      step_stmt,
                                      for_block_codes,
                                      for_block_local_vars);
    return for_statement;
}

std::unique_ptr<Statement> Parser::parseWhileStatement(std::string& 
                                                     parent_func_name)
{
    advanceTokens();
    assert(cur_token.isTokenLP());
    advanceTokens();

    auto continue_cond = parseCondition();
    assert(cur_token.isTokenRP());

    // Parse for block
    advanceTokens();
    assert(cur_token.isTokenLBrace());

    std::vector<std::shared_ptr<Statement>> while_block_codes;
    std::unordered_map<std::string,ValueType::Type> while_block_local_vars;
    local_vars_tracker.push_back(&while_block_local_vars);
    while (true) {
        advanceTokens();
        if (cur_token.isTokenRBrace())
            break;

        parseStatement(parent_func_name, while_block_codes);

        // Check if we just finished an if/while statement
        if (while_block_codes.back()->isStatementIf() ||
            while_block_codes.back()->isStatementFor() ||
            while_block_codes.back()->isStatementWhile()) {
            // This RBrace is from the statement,
            // should not terminate.
            assert(cur_token.isTokenRBrace());
        }
        else {
            if (cur_token.isTokenRBrace())
                break;
        }
    }

    std::unique_ptr<Statement> while_statement = 
        std::make_unique<WhileStatement>(continue_cond,
                                        while_block_codes,
                                        while_block_local_vars);
    return while_statement;
}

// Parse expression 
std::unique_ptr<Expression> Parser::parseExpression()
{
    std::unique_ptr<Expression> left = parseTerm();     // Apply production rule for term to generate left node 

    while (true) {
        if (cur_token.isTokenPlus() || cur_token.isTokenMinus()) {
            Expression::ExpressionType expr_type;
            if (cur_token.isTokenPlus()) {
                expr_type = Expression::ExpressionType::PLUS;
            }
            else {
                expr_type = Expression::ExpressionType::MINUS;
            }

            advanceTokens();

            std::unique_ptr<Expression> right;

            // Priority one: handle sub-expression in paranthesis ()
            if (cur_token.isTokenLP()) {
                right = parseTerm();
                left = std::make_unique<ArithExpression>(left, 
                       right, 
                       expr_type);
                continue;
            }
            
            // Priority two: check if next token is a function call
            std::unique_ptr<Expression> pending_expr = nullptr;
            if (auto [is_def, is_built_in] = 
                    isFuncDef(cur_token.getLiteral());
                    is_def) {
                strictTypeCheck(cur_token);
                pending_expr = parseCall();
            }

            // check if next token is an array index
            // TODO - add deref in the future
            if (bool is_index = (next_token.isTokenLBracket()) ?
                                true : false;
                is_index) {
                assert(pending_expr == nullptr);
                strictTypeCheck(cur_token, is_index);
                pending_expr = parseIndex();
            }

            if (next_token.isTokenAsterisk() || next_token.isTokenSlash()) {
                if (pending_expr != nullptr) {
                    advanceTokens();
                    right = parseTerm(std::move(pending_expr));
                }
                else {
                    right = parseTerm();
                }
            }
            else {
                if (pending_expr != nullptr) {
                    right = std::move(pending_expr);
                    advanceTokens();
                }
                else {
                    if (strictTypeCheckReturn(cur_token)){
                        right = parseExpression();
                    } else {
                        right = std::make_unique<LiteralExpression>(cur_token);
                        advanceTokens();
                    }
                }
                
            }

            left = std::make_unique<ArithExpression>(left, 
                       right, 
                       expr_type);
        }
        else {
            return left;
        }
    }
}

// Parse term 
std::unique_ptr<Expression> Parser::parseTerm(std::unique_ptr<Expression> pending_left)
{   
    std::unique_ptr<Expression> left = 
        (pending_left != nullptr) ? std::move(pending_left) : parseFactor(); // Parse factor 

    while (true) {
        if (cur_token.isTokenAsterisk() || cur_token.isTokenSlash()) {
            Expression::ExpressionType expr_type;
            if (cur_token.isTokenAsterisk()) {
                expr_type = Expression::ExpressionType::ASTERISK;
            }
            else {
                expr_type = Expression::ExpressionType::SLASH;
            }

            advanceTokens();

            std::unique_ptr<Expression> right;

            if (cur_token.isTokenMinus() || cur_token.isTokenPlus()) {
                Expression::ExpressionType expr_type2 = cur_token.isTokenMinus() ?
                    Expression::ExpressionType::MINUS :
                    Expression::ExpressionType::PLUS;
                std::unique_ptr<Expression> left2 = parseFactor();
                advanceTokens();
                right = std::make_unique<LiteralExpression>(cur_token);
                right = std::make_unique<ArithExpression>(left2, 
                       right, 
                       expr_type2);

            } else {
                // We are trying to mul/div something with higher priority
                if (cur_token.isTokenLP()) {
                    right = parseTerm();
                }
                else {
                    // TODO - add deref in the future
                    bool is_index = (next_token.isTokenLBracket()) ?
                                    true : false;

                    strictTypeCheck(cur_token, is_index);
        
                    if (is_index)
                        right = parseIndex();
                    else if (auto [is_def, is_built_in] = 
                                isFuncDef(cur_token.getLiteral());
                                is_def)
                        right = parseCall();
                    else 
                        right = std::make_unique<LiteralExpression>(cur_token);

                    advanceTokens();
                }
            }

            left = std::make_unique<ArithExpression>(left, 
                       right, 
                       expr_type);

        }
        else {
            break;
        }

    }

    return left;
}

// Parse factor 
std::unique_ptr<Expression> Parser::parseFactor()
{
    std::unique_ptr<Expression> left;

    // Unary minus and plus operator 
    if (cur_token.isTokenMinus() || cur_token.isTokenPlus()) {
        Token zero_tok = Token();
        zero_tok.type = (cur_expr_type == ValueType::Type::INT) ? 
                                    Token::TokenType::TOKEN_INT : 
                                    Token::TokenType::TOKEN_FLOAT;
        zero_tok.literal = (cur_expr_type == ValueType::Type::INT) ? "0" : "0.0";

        left = std::make_unique<LiteralExpression>(zero_tok);

        Token result = Token(cur_token.type, cur_token.literal);

        while(next_token.isTokenMinus() || next_token.isTokenPlus()) {
            // if minus followed by minus => invert sign
            if (result.isTokenMinus() and next_token.isTokenMinus()){
                result.type = result.isTokenMinus() ? 
                    Token::TokenType::TOKEN_PLUS :
                    Token::TokenType::TOKEN_MINUS;
                result.literal = result.isTokenMinus() ? "+" : "-";
            } 
            // if plus is followed by minus => minus
            else if (result.isTokenPlus() and next_token.isTokenMinus()) {
                result.type = Token::TokenType::TOKEN_MINUS;
                result.literal = "-";
            }
            advanceTokens();
        }
        return left;
    }

    // Recursively parse sub-expression 
    if (cur_token.isTokenLP()) {
        advanceTokens();
        left = parseExpression();
        assert(cur_token.isTokenRP()); // Error checking
        advanceTokens();
        return left;
    }
    
    // TODO - add deref in the future
    bool is_index = (next_token.isTokenLBracket()) ?
                    true : false;

    // TODO: THIS CAUSE SEGFAUT ON A RETURN STATEMENT????
    // strictTypeCheck(cur_token, is_index);
    
    if (is_index)
        left = parseIndex();
    else if (auto [is_def, is_built_in] = 
                 isFuncDef(cur_token.getLiteral());
                 is_def)
        left = parseCall();
    else
        left = std::make_unique<LiteralExpression>(cur_token);

    advanceTokens();

    return left;
}

// Print functions for the various nodes in the AST
void RetStatement::printStatement()
{
    std::cout << "    {\n";
    std::cout << "      [Return]\n";
    if (ret->getType() == Expression::ExpressionType::LITERAL) 
        std::cout << "      " << ret->print(4);
    else 
        std::cout << ret->print(4);

    std::cout << "    }\n";
}

void AssnStatement::printStatement()
{
    std::cout << "    {\n";
    if (iden->getType() == Expression::ExpressionType::LITERAL) 
        std::cout << "      " << iden->print(4);
    else 
        std::cout << iden->print(4);

    std::cout << "      =\n";
    
    if (expr->getType() == Expression::ExpressionType::LITERAL) 
        std::cout << "      " << expr->print(4);
    else 
        std::cout << expr->print(4);
    
    std::cout << "    }\n";
}

void FuncStatement::printStatement()
{
    std::cout << "{\n";
    std::cout << "  Function Name: " << iden->print() << "\n";
    std::cout << "  Return Type: ";
    if (func_type == ValueType::Type::VOID) 
        std::cout << "void\n";
    else if (func_type == ValueType::Type::INT) 
        std::cout << "int\n";
    else if (func_type == ValueType::Type::FLOAT) 
        std::cout << "float\n";

    std::cout << "  Arguments\n";
    for (auto &arg : args) 
        std::cout << "    " << arg.print() << "\n";
    
    if (!args.size()) std::cout << "    NONE\n";

    std::cout << "  Codes\n";
    std::cout << "  {\n";
    for (auto &code : codes) 
        code->printStatement();
    
    std::cout << "  }\n";
    std::cout << "}\n";
}

void IfStatement::printStatement()
{
    std::cout << "  {\n";
    std::cout << "  [IF Statement] \n";
    std::cout << "  [Condition]\n";
    cond->printStatement();
    std::cout << "  [Taken Block]\n";
    std::cout << "  {\n";
    for (auto &code : taken_block) 
        code->printStatement();
    
    std::cout << "  }\n";
    
    if (not_taken_block.size() == 0) {
        std::cout << "  }\n";
        return;
    }
    
    std::cout << "  [Not Taken Block]\n";
    std::cout << "  {\n";
    for (auto &code : not_taken_block) 
        code->printStatement();
    
    std::cout << "  }\n";
    std::cout << "  }\n";
}

void ForStatement::printStatement()
{
    std::cout << "  {\n";
    std::cout << "  [For Statement] \n";
    std::cout << "  [Start]\n";
    start->printStatement();
    std::cout << "  [Stop Cond]\n";
    stop_cond->printStatement();
    std::cout << "  [Step]\n";
    step->printStatement();

    std::cout << "  [Block]\n";
    std::cout << "  {\n";
    
    for (auto &code : block) 
        code->printStatement();
    
    std::cout << "  }\n";
    std::cout << "  }\n";
}

void WhileStatement::printStatement()
{
    std::cout << "  {\n";
    std::cout << "  [While Statement] \n";
    std::cout << "  [Continue Cond]\n";
    continue_cond->printStatement();

    std::cout << "  [Block]\n";
    std::cout << "  {\n";
    
    for (auto &code : block) 
        code->printStatement();
    
    std::cout << "  }\n";
    std::cout << "  }\n";
}

void Condition::printStatement()
{
    std::cout << "  {\n";
    std::cout << "    [Left]\n";
    if (left->getType() == Expression::ExpressionType::LITERAL)
        std::cout << "      " << left->print(3) << "\n";
    else
        std::cout << left->print(3) << "\n";
    
    std::cout << "    [COMP] " << opr_type_str << "\n\n";
    std::cout << "    [Right]\n";
    if (right->getType() == Expression::ExpressionType::LITERAL) 
        std::cout << "      " << right->print(3) << "\n";
    else
        std::cout << right->print(3) << "\n";
    std::cout << "  }\n";
}
}
