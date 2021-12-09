#ifndef __CODEGEN_HH__
#define __CODEGEN_HH__

#include "parser/parser.hh"

// LLVM IR codegen libraries
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeWriter.h"

using namespace llvm;

namespace Frontend
{
class Codegen
{
  protected:
    std::unique_ptr<LLVMContext> context;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder<>> builder;

    std::string mod_name;
    std::string out_fn;

    Parser* parser;

  public:

    Codegen(const char* _mod_name,
            const char* _out_fn)
        : mod_name(_mod_name)
        , out_fn(_out_fn)
    {}

    void setParser(Parser *_parser)
    {
        parser = _parser;
    }

    void gen();

    void print();

  protected:
    std::vector<std::unordered_map<std::string,
                                   ValueType::Type>*> local_vars_ref;
    std::vector<std::unordered_map<std::string,Value*>> local_vars_tracker;

    void recordLocalVar(std::string& var_name, Value* reg)
    {
        auto &tracker = local_vars_tracker.back();
        tracker.insert({var_name, reg});
    }

    ValueType::Type getValType(std::string& _var_name)
    {
        for (int i = local_vars_ref.size() - 1;
                 i >= 0;
                 i--) {
            auto &ref = local_vars_ref[i];

            if (auto iter = ref->find(_var_name);
                    iter != ref->end()) {
                return iter->second;
            }
        }
    }
    
    std::pair<bool,Value*> getReg(std::string& _var_name)
    {
        for (int i = local_vars_tracker.size() - 1;
                 i >= 0;
                 i--) {
            auto &tracker = local_vars_tracker[i];

            if (auto iter = tracker.find(_var_name);
                    iter != tracker.end()) {
                return std::make_pair(true,iter->second);
            }
        }
        return std::make_pair(false,nullptr);
    }

    void statementGen(std::string&, Statement*);

    void funcGen(Statement *);
    void assnGen(Statement *);
    void builtinGen(Statement *);
    void callGen(Statement *);
    void retGen(std::string &,Statement *);

    Value* condGen(Condition*);
    void ifGen(std::string&,Statement *);
    void forGen(std::string&,Statement *);
    void whileGen(std::string&,Statement *);

    Value* allocaForIden(std::string&,
                         ValueType::Type&,
                         Expression*,
                         ArrayExpression*);
   
    Value* exprGen(ValueType::Type,Expression*);

    void arrayExprGen(ValueType::Type,
                      Value*,
                      ArrayExpression*);

    Value* arithExprGen(ValueType::Type,ArithExpression*);

    Value* literalExprGen(ValueType::Type, LiteralExpression*);

    Value* indexExprGen(ValueType::Type, IndexExpression*);

    Value* callExprGen(CallExpression*);
};
}

#endif
