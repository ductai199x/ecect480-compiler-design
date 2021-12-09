#include "codegen/codegen.hh"

namespace Frontend
{
    void Codegen::gen()
    {
        /* Create LLVM context. 
       A context is an execution state for the core LLVM IR system
    */
        context = std::make_unique<LLVMContext>();
        module = std::make_unique<Module>(mod_name, *context);

        /* Create a new builder for the module to fill basic blocks of functions with LLVM instructions. 
       IRBuilder internally maintains a current basic block and a pointer inside the block's list of instructions. 
       When a new instruction is added, it is inserted at that point, and then the pointer is advanced 
       after the new instruction.
    */
        builder = std::make_unique<IRBuilder<>>(*context);

        // Codegen begins
        auto &program = parser->getProgram(); // Run parser to generate the AST
        auto &statements = program.getStatements();

        // Iterate over each function and generate IR code from AST
        for (auto &statement : statements)
        {
            assert(statement->isStatementFunc());
            funcGen(statement.get());
        }
    }

    // Generate code for function
    void Codegen::funcGen(Statement *_statement)
    {
        FuncStatement *func_statement =
            static_cast<FuncStatement *>(_statement);

        // Extract the local variables reference
        local_vars_ref.push_back(func_statement->getLocalVars());
        local_vars_tracker.emplace_back();

        auto &func_name = func_statement->getFuncName();
        auto &func_args = func_statement->getFuncArgs();
        auto &func_codes = func_statement->getFuncCodes();

        // IR Generation: prepare argument types
        // LLVM types represent the type of a value
        std::vector<Type *> ir_gen_func_args;
        for (auto &arg : func_args)
        {
            if (arg.getArgType() == ValueType::Type::INT)
                ir_gen_func_args.push_back(Type::getInt32Ty(*context));
            else if (arg.getArgType() == ValueType::Type::FLOAT)
                ir_gen_func_args.push_back(Type::getFloatTy(*context));
            else
                assert(false &&
                       "[Error] funcGen: unsupported argument type. \n");
        }

        // Prepare return type
        Type *ir_gen_ret_type;
        if (func_statement->getRetType() == ValueType::Type::VOID)
            ir_gen_ret_type = Type::getVoidTy(*context);
        else if (func_statement->getRetType() == ValueType::Type::INT)
            ir_gen_ret_type = Type::getInt32Ty(*context);
        else if (func_statement->getRetType() == ValueType::Type::FLOAT)
            ir_gen_ret_type = Type::getFloatTy(*context);
        else
            assert(false &&
                   "[Error] funcGen: unsupported return type. \n");

        // Determine function type
        FunctionType *ir_gen_func_type =
            FunctionType::get(ir_gen_ret_type, ir_gen_func_args, false);

        // We specify external linkage for all functions so that they are accessible through the whole program.
        GlobalValue::LinkageTypes link_type = Function::ExternalLinkage;

        // Create function declaration
        Function *ir_gen_func = Function::Create(ir_gen_func_type,
                                                 link_type,
                                                 func_name,
                                                 module.get());

        // Create a new basic block to start insertion into.
        BasicBlock *BB = BasicBlock::Create(*context, "", ir_gen_func);
        builder->SetInsertPoint(BB);

        // Generate the code section
        // (1) Allocate space for arguments
        auto i = 0;
        std::vector<ValueType::Type> func_arg_types;
        if (ir_gen_func->arg_size())
            func_arg_types = parser->getFuncArgTypes(func_name);

        for (auto &arg : ir_gen_func->args())
        {
            Value *val = &arg;
            Value *reg;

            if (func_arg_types[i] == ValueType::Type::INT)
            {
                reg = builder->CreateAlloca(Type::getInt32Ty(*context));
                builder->CreateStore(val, reg);
            }
            else if (func_arg_types[i] == ValueType::Type::FLOAT)
            {
                reg = builder->CreateAlloca(Type::getFloatTy(*context));
                builder->CreateStore(val, reg);
            }

            recordLocalVar(func_args[i].getLiteral(), reg);
            i++;
        }

        // (2) Rest of the codes
        for (auto &statement : func_codes)
        {
            statementGen(func_name, statement.get());
        }

        if (func_statement->getRetType() == ValueType::Type::VOID)
        {
            Value *val = nullptr;
            builder->CreateRet(val);
        }

        // Verify function
        verifyFunction(*ir_gen_func);

        local_vars_tracker.pop_back();
        local_vars_ref.pop_back();
    }

    // Generate code for various statements within function body
    void Codegen::statementGen(std::string &func_name,
                               Statement *statement)
    {
        if (statement->isStatementAssn())
        {
            assnGen(statement);
        }
        else if (statement->isStatementBuiltinCall())
        {
            builtinGen(statement);
        }
        else if (statement->isStatementRet())
        {
            retGen(func_name, statement);
        }
        else if (statement->isStatementNormalCall())
        {
            callGen(statement);
        }
        else if (statement->isStatementIf())
        {
            ifGen(func_name, statement);
        }
        else if (statement->isStatementFor())
        {
            forGen(func_name, statement);
        }
        else if (statement->isStatementWhile())
        {
            whileGen(func_name, statement);
        }
    }

    // Generate code for assignment statement
    void Codegen::assnGen(Statement *_statement)
    {
        AssnStatement *assn_statement =
            static_cast<AssnStatement *>(_statement);

        auto iden = assn_statement->getIden();
        auto expr = assn_statement->getExpr();

        // Allocate for identifier
        // Value is the base class of all values computed by a program that may be used as operands to other values.
        // Values are maintained in SSA form
        std::string var_name;
        ValueType::Type var_type;
        Value *reg;

        ArrayExpression *array_info =
            (expr->isExprArray()) ? static_cast<ArrayExpression *>(expr) : nullptr;

        reg = allocaForIden(var_name, var_type,
                            iden, array_info);

        // Extract assigned value
        Value *val = nullptr;
        if (array_info != nullptr)
        {
            arrayExprGen(var_type, reg, array_info);
        }
        else
        {
            val = exprGen(var_type, expr);
            builder->CreateStore(val, reg);
        }
    }

    // Allocate storage for identifier
    Value *Codegen::allocaForIden(std::string &var_name,
                                  ValueType::Type &var_type,
                                  Expression *iden,
                                  ArrayExpression *array_info)
    {
        // Determine identifier type
        if (iden->isExprLiteral())
        {
            LiteralExpression *lit =
                static_cast<LiteralExpression *>(iden);

            var_name = lit->getLiteral();
            var_type = getValType(var_name);
        }
        else if (iden->isExprIndex())
        {
            IndexExpression *index = static_cast<IndexExpression *>(iden);

            var_name = index->getIden();
            var_type = getValType(var_name);
        }

        Value *reg;
        if (auto [is_allocated, reg_base] = getReg(var_name);
            !is_allocated)
        {
            // Allocating new variables, must be a literal iden
            assert(iden->isExprLiteral());
            LiteralExpression *lit =
                static_cast<LiteralExpression *>(iden);

            if (var_type == ValueType::Type::INT)
            {
                reg = builder->CreateAlloca(Type::getInt32Ty(*context));
            }
            else if (var_type == ValueType::Type::FLOAT)
            {
                reg = builder->CreateAlloca(Type::getFloatTy(*context));
            }
            else if (var_type == ValueType::Type::INT_ARRAY ||
                     var_type == ValueType::Type::FLOAT_ARRAY)
            {
                assert(array_info != nullptr);

                // Extract number of elements
                auto num_ele_expr = array_info->getNumElements();
                assert(num_ele_expr->isExprLiteral());

                auto num_ele_lit =
                    static_cast<LiteralExpression *>(num_ele_expr);
                assert(num_ele_lit->isLiteralInt());

                auto num_ele_int = stoi(num_ele_lit->getLiteral());

                // Get array type
                Type *ele_type = (var_type == ValueType::Type::INT_ARRAY) ? Type::getInt32Ty(*context) : Type::getFloatTy(*context);

                ArrayType *array_type = ArrayType::get(ele_type, num_ele_int);

                reg = builder->CreateAlloca(array_type);
            }
            else
            {
                std::cerr << "[Error] unsupported allocation type for "
                          << var_name << "\n";
                exit(0);
            }

            recordLocalVar(var_name, reg);
        }
        else
        {
            if (iden->isExprIndex())
            {
                IndexExpression *index = static_cast<IndexExpression *>(iden);
                Value *idx = exprGen(ValueType::Type::INT, index->getIndex());
                std::vector<Value *> idxs;
                idxs.push_back(ConstantInt::get(*context, APInt(32, 0)));
                idxs.push_back(idx);
                reg = builder->CreateInBoundsGEP(reg_base, idxs);
            }
            else if (iden->isExprLiteral())
            {
                reg = reg_base;
            }
        }

        return reg;
    }

    // built-ins are implemented in util/ and linked through llvm-link
    // please check bc_compile_and_run.bash and util for more info
    // This one is bit different from our callGen implementation
    // since we are defining printVarInt/printVarFloat at current
    // compilation unit.
    void Codegen::builtinGen(Statement *_statement)
    {
        static FunctionCallee printVarInt =
            module->getOrInsertFunction("printVarInt",
                                        Type::getVoidTy(*context),
                                        Type::getInt32Ty(*context));

        static FunctionCallee printVarFloat =
            module->getOrInsertFunction("printVarFloat",
                                        Type::getVoidTy(*context),
                                        Type::getFloatTy(*context));

        CallStatement *built_in_statement =
            static_cast<CallStatement *>(_statement);

        auto call_expr = built_in_statement->getCallExpr();
        assert(call_expr->isExprCall());

        auto &func_name = call_expr->getCallFunc();
        auto &func_args = call_expr->getArgs();
        assert(func_args.size() == 1);
        auto expr = func_args[0].get();

        ValueType::Type var_type = (func_name == "printVarInt") ? ValueType::Type::INT : ValueType::Type::FLOAT;

        Value *val = exprGen(var_type, expr);

        if (func_name == "printVarInt")
        {
            builder->CreateCall(printVarInt, val);
        }
        else if (func_name == "printVarFloat")
        {
            builder->CreateCall(printVarFloat, val);
        }
    }

    // Generate code for function call
    void Codegen::callGen(Statement *_statement)
    {
        CallStatement *built_in_statement =
            static_cast<CallStatement *>(_statement);

        auto call_expr = built_in_statement->getCallExpr();
        assert(call_expr->isExprCall());

        callExprGen(call_expr);
    }

    // Generate code for function return
    void Codegen::retGen(std::string &cur_func_name,
                         Statement *_statement)
    {
        RetStatement *ret = static_cast<RetStatement *>(_statement);

        auto expr = ret->getRetVal();

        ValueType::Type ret_type = parser->getFuncRetType(cur_func_name);

        Value *val = exprGen(ret_type, expr);
        builder->CreateRet(val);
    }

    // Create code to ealuate condtion
    Value *Codegen::condGen(Condition *cond)
    {
        auto var_type = cond->getType();

        Value *left = exprGen(var_type, cond->getLeft());
        Value *right = exprGen(var_type, cond->getRight());

        Value *eval = nullptr;
        auto opr = cond->getOpr();
        if (opr == "==")
        {
            if (var_type == ValueType::Type::INT)
            {
                eval = builder->CreateICmpEQ(left, right);
            }
            else if (var_type == ValueType::Type::FLOAT)
            {
                eval = builder->CreateFCmpOEQ(left, right);
            }
        }
        else if (opr == "!=")
        {
            if (var_type == ValueType::Type::INT)
            {
                eval = builder->CreateICmpNE(left, right);
            }
            else if (var_type == ValueType::Type::FLOAT)
            {
                eval = builder->CreateFCmpONE(left, right);
            }
        }
        else if (opr == ">")
        {
            if (var_type == ValueType::Type::INT)
            {
                eval = builder->CreateICmpSGT(left, right);
            }
            else if (var_type == ValueType::Type::FLOAT)
            {
                eval = builder->CreateFCmpOGT(left, right);
            }
        }
        else if (opr == ">=")
        {
            if (var_type == ValueType::Type::INT)
            {
                eval = builder->CreateICmpSGE(left, right);
            }
            else if (var_type == ValueType::Type::FLOAT)
            {
                eval = builder->CreateFCmpOGE(left, right);
            }
        }
        else if (opr == "<")
        {
            if (var_type == ValueType::Type::INT)
            {
                eval = builder->CreateICmpSLT(left, right);
            }
            else if (var_type == ValueType::Type::FLOAT)
            {
                eval = builder->CreateFCmpOLT(left, right);
            }
        }
        else if (opr == "<=")
        {
            if (var_type == ValueType::Type::INT)
            {
                eval = builder->CreateICmpSLE(left, right);
            }
            else if (var_type == ValueType::Type::FLOAT)
            {
                eval = builder->CreateFCmpOLE(left, right);
            }
        }

        assert(eval != nullptr);
        return eval;
    }

    // Create code for if statment
    void Codegen::ifGen(std::string &parent_func_name, Statement *_statement)
    {
        IfStatement *if_s =
            static_cast<IfStatement *>(_statement);

        auto cond = condGen(if_s->getCond()); // Create code to evaluate condition
        auto &taken_block = if_s->getTakenBlock();
        auto &not_taken_block = if_s->getNotTakenBlock();

        // Build basic blocks for paths
        Function *func = builder->GetInsertBlock()->getParent();
        BasicBlock *taken_BB =
            BasicBlock::Create(*context, "", func);

        BasicBlock *not_taken_BB = (not_taken_block.size()) ? BasicBlock::Create(*context, "", func) : nullptr;

        BasicBlock *merge_BB = BasicBlock::Create(*context, "", func);

        if (not_taken_BB != nullptr)
        {
            builder->CreateCondBr(cond, taken_BB, not_taken_BB);
        }
        else
        {
            builder->CreateCondBr(cond, taken_BB, merge_BB);
        }

        // Build taken path
        builder->SetInsertPoint(taken_BB);
        local_vars_ref.push_back(if_s->getTakenBlockVars());
        local_vars_tracker.emplace_back();
        for (auto &statement : taken_block)
        {
            statementGen(parent_func_name, statement.get());
        }
        builder->CreateBr(merge_BB);
        local_vars_ref.pop_back();
        local_vars_tracker.pop_back();

        // Build not taken path
        if (not_taken_BB != nullptr)
        {
            builder->SetInsertPoint(not_taken_BB);
            local_vars_ref.push_back(if_s->getNotTakenBlockVars());
            local_vars_tracker.emplace_back();
            for (auto &statement : not_taken_block)
            {
                statementGen(parent_func_name, statement.get());
            }
            builder->CreateBr(merge_BB);
            local_vars_ref.pop_back();
            local_vars_tracker.pop_back();
        }

        builder->SetInsertPoint(merge_BB);
    }

    // Generate for loop
    void Codegen::forGen(std::string &parent_func_name, Statement *_statement)
    {
        ForStatement *for_s =
            static_cast<ForStatement *>(_statement);

        local_vars_ref.push_back(for_s->getBlockVars());
        local_vars_tracker.emplace_back();

        // Generate code for initializer
        assnGen(for_s->getStart());

        // Build basic blocks for paths
        Function *func = builder->GetInsertBlock()->getParent();

        // Basic block for code to check loop condition
        BasicBlock *check_BB =
            BasicBlock::Create(*context, "", func);

        // Basic block to code within loop body
        BasicBlock *body_BB =
            BasicBlock::Create(*context, "", func);

        // Basic block the occurs after loop is done
        BasicBlock *merge_BB =
            BasicBlock::Create(*context, "", func);

        // Generate code to test for loop condition
        builder->CreateBr(check_BB);
        builder->SetInsertPoint(check_BB);

        auto end_cond = condGen(for_s->getStopCond());
        builder->CreateCondBr(end_cond, body_BB, merge_BB);

        // Generate code for the loop body
        builder->SetInsertPoint(body_BB);
        auto block = for_s->getBlock();
        for (auto code : block)
        {
            statementGen(parent_func_name, code.get());
        }

        // Generate code for loop step/increment
        assnGen(for_s->getStep());
        builder->CreateBr(check_BB);

        // Loop end
        builder->SetInsertPoint(merge_BB);

        local_vars_ref.pop_back();
        local_vars_tracker.pop_back();
    }

    // Generate while loop
    void Codegen::whileGen(std::string &parent_func_name, Statement *_statement)
    {
        WhileStatement *while_s =
            static_cast<WhileStatement *>(_statement);

        local_vars_ref.push_back(while_s->getBlockVars());
        local_vars_tracker.emplace_back();

        // Build basic blocks for paths
        Function *func = builder->GetInsertBlock()->getParent();

        // Basic block for code to check loop condition
        BasicBlock *check_BB =
            BasicBlock::Create(*context, "", func);

        // Basic block to code within loop body
        BasicBlock *body_BB =
            BasicBlock::Create(*context, "", func);

        // Basic block to occurs after loop is done
        BasicBlock *merge_BB =
            BasicBlock::Create(*context, "", func);

        // Generate code to test while loop condition
        builder->CreateBr(check_BB);
        builder->SetInsertPoint(check_BB);

        auto cont_cond = condGen(while_s->getContinueCond());
        builder->CreateCondBr(cont_cond, body_BB, merge_BB);

        // Generate code for the loop body
        builder->SetInsertPoint(body_BB);
        auto block = while_s->getBlock();
        for (auto code : block)
        {
            statementGen(parent_func_name, code.get());
        }

        builder->CreateBr(check_BB);

        // Loop end
        builder->SetInsertPoint(merge_BB);

        local_vars_ref.pop_back();
        local_vars_tracker.pop_back();
    }

    // Generate code for various expressions
    Value *Codegen::exprGen(ValueType::Type _var_type, Expression *expr)
    {
        ValueType::Type var_type = _var_type;
        if (_var_type == ValueType::Type::INT_ARRAY)
            var_type = ValueType::Type::INT;
        else if (_var_type == ValueType::Type::FLOAT_ARRAY)
            var_type = ValueType::Type::FLOAT;

        Value *val = nullptr;
        if (expr->isExprLiteral())
        {
            LiteralExpression *lit = static_cast<LiteralExpression *>(expr);
            val = literalExprGen(var_type, lit);
        }
        else if (expr->isExprArith())
        {
            ArithExpression *arith = static_cast<ArithExpression *>(expr);
            val = arithExprGen(var_type, arith);
        }
        else if (expr->isExprIndex())
        {
            IndexExpression *index = static_cast<IndexExpression *>(expr);
            val = indexExprGen(var_type, index);
        }
        else if (expr->isExprCall())
        {
            CallExpression *call = static_cast<CallExpression *>(expr);
            val = callExprGen(call);
        }

        assert(val != nullptr);
        return val;
    }

    // Generate code for literal expression
    Value *Codegen::literalExprGen(ValueType::Type type,
                                   LiteralExpression *lit)
    {
        Value *val;
        auto [is_allocated, reg_val] = getReg(lit->getLiteral());

        if (!is_allocated)
        {
            assert((lit->isLiteralInt() ||
                    lit->isLiteralFloat()));

            auto val_str = lit->getLiteral();
            if (lit->isLiteralInt())
            {
                val = ConstantInt::get(*context, APInt(32, stoi(val_str)));
            }
            else if (lit->isLiteralFloat())
            {
                val = ConstantFP::get(*context, APFloat(stof(val_str)));
            }
        }
        else
        {
            if (type == ValueType::Type::INT)
            {
                val = builder->CreateLoad(Type::getInt32Ty(*context),
                                          reg_val);
            }
            else if (type == ValueType::Type::FLOAT)
            {
                val = builder->CreateLoad(Type::getFloatTy(*context),
                                          reg_val);
            }
        }

        assert(val != nullptr);
        return val;
    }

    // Generate code for array expressions
    void Codegen::arrayExprGen(ValueType::Type array_type,
                               Value *reg,
                               ArrayExpression *array_info)
    {
        // Determine element type
        ValueType::Type type;
        if (array_type == ValueType::Type::INT_ARRAY)
            type = ValueType::Type::INT;
        else if (array_type == ValueType::Type::FLOAT_ARRAY)
            type = ValueType::Type::FLOAT;
        else
            assert(false);

        // Get address of the 0th element in array.
        // The first index will get you the pointer, then the second
        // index gets you to the first element.
        std::vector<Value *> index;
        index.push_back(ConstantInt::get(*context, APInt(32, 0)));
        index.push_back(ConstantInt::get(*context, APInt(32, 0)));
        auto base = builder->CreateInBoundsGEP(reg, index);

        auto cnt = 0;
        auto last_ele_idx = array_info->getElements().size() - 1;
        auto const_one = ConstantInt::get(*context, APInt(32, 1));
        for (auto ele : array_info->getElements())
        {
            Value *val = exprGen(type, ele.get());
            builder->CreateStore(val, base);
            if (++cnt <= last_ele_idx)
            {
                // increment one to the base
                base = builder->CreateInBoundsGEP(base, const_one);
            }
        }
    }

    // Generate code for arithmetic expression using a tree-walk code generator
    Value *Codegen::arithExprGen(ValueType::Type type,
                                 ArithExpression *arith)
    {
        Value *val_left = nullptr;
        Value *val_right = nullptr;

        // Recursively generate the left arith expr
        if (arith->getLeft() != nullptr)
        {
            Expression *next_expr = arith->getLeft();
            if (next_expr->isExprArith())
            {
                ArithExpression *next_arith =
                    static_cast<ArithExpression *>(next_expr);
                val_left = arithExprGen(type,
                                        next_arith);
            }
        }

        // Recursively generate the right arith expr
        if (arith->getRight() != nullptr)
        {
            Expression *next_expr = arith->getRight();
            if (next_expr->isExprArith())
            {
                ArithExpression *next_arith =
                    static_cast<ArithExpression *>(next_expr);
                val_right = arithExprGen(type,
                                         next_arith);
            }
        }

        if (val_left == nullptr)
        {
            Expression *left_expr = arith->getLeft();

            assert((left_expr->isExprLiteral() ||
                    left_expr->isExprCall() ||
                    left_expr->isExprIndex()));

            val_left = exprGen(type, left_expr);
        }

        if (val_right == nullptr)
        {
            Expression *right_expr = arith->getRight();

            // The right_expr must either be literal or call
            assert((right_expr->isExprLiteral() ||
                    right_expr->isExprCall() ||
                    right_expr->isExprIndex()));

            val_right = exprGen(type, right_expr);
        }

        assert(val_left != nullptr);
        assert(val_right != nullptr);

        // Generate operators
        auto opr = arith->getOperator();

        switch (opr)
        {
        case '+':
            if (type == ValueType::Type::INT)
                return builder->CreateAdd(val_left, val_right);
            else if (type == ValueType::Type::FLOAT)
                return builder->CreateFAdd(val_left, val_right);
        case '-':
            if (type == ValueType::Type::INT)
                return builder->CreateSub(val_left, val_right);
            else if (type == ValueType::Type::FLOAT)
                return builder->CreateFSub(val_left, val_right);
        case '*':
            if (type == ValueType::Type::INT)
                return builder->CreateMul(val_left, val_right);
            else if (type == ValueType::Type::FLOAT)
                return builder->CreateFMul(val_left, val_right);
        case '/':
            if (type == ValueType::Type::INT)
                return builder->CreateSDiv(val_left, val_right);
            else if (type == ValueType::Type::FLOAT)
                return builder->CreateFDiv(val_left, val_right);
        }
    }

    // Generate code to calculate array index
    Value *Codegen::indexExprGen(ValueType::Type type,
                                 IndexExpression *index)
    {
        auto [is_allocated, reg_val] = getReg(index->getIden());
        assert(is_allocated);

        Value *idx = exprGen(ValueType::Type::INT, index->getIndex());

        std::vector<Value *> idxs;
        idxs.push_back(ConstantInt::get(*context, APInt(32, 0)));
        idxs.push_back(idx);
        auto base = builder->CreateInBoundsGEP(reg_val, idxs);

        Value *val;
        if (type == ValueType::Type::INT)
            val = builder->CreateLoad(Type::getInt32Ty(*context), base);
        else if (type == ValueType::Type::FLOAT)
            val = builder->CreateLoad(Type::getFloatTy(*context), base);

        return val;
    }

    // Create code for function call
    Value *Codegen::callExprGen(CallExpression *call)
    {
        auto &def = call->getCallFunc();
        Function *call_func = module->getFunction(def);
        if (!call_func)
        {
            std::cerr << "[Error] Please define function before CALL\n";
            exit(0);
        }

        auto args = call->getArgs();
        auto arg_types = parser->getFuncArgTypes(def);
        assert(args.size() == call_func->arg_size());
        assert(arg_types.size() == call_func->arg_size());

        std::vector<Value *> call_func_args;
        for (auto i = 0; i < call_func->arg_size(); i++)
        {
            auto expr = args[i].get();

            Value *val = exprGen(arg_types[i], expr);
            call_func_args.push_back(val);
        }

        return builder->CreateCall(call_func, call_func_args);
    }

    // Print the LLVM IR to file
    void Codegen::print()
    {
        std::error_code EC;
        raw_fd_ostream out(out_fn, EC);
        WriteBitcodeToFile(*module, out);
    }
}
