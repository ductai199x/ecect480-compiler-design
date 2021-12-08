/* LLVM IR code generation.
 * Author: Shihao Song 
 * Modified by: Naga Kandasamy
 * Date: October 21, 2021
 */

#include "parser/parser.hh"
#include "codegen/codegen.hh"

#include <iomanip>
#include <iostream>

using namespace Frontend;

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: ./codegen source-program name-of-executable\n");
        fprintf(stderr, "Example: ./codegen series_sum.txt series_sum.bc\n");
        exit(EXIT_SUCCESS);
    }

    char *source_program = argv[1];
    char *llvm_IR_code = argv[2];

    // Instantiate parser with source-program file
    Parser parser(source_program);

    // Generate LLVM IR code and store in specified output file 
    Codegen codegen(source_program, llvm_IR_code);
    codegen.setParser(&parser);
    codegen.gen();
    codegen.print();

    exit(EXIT_SUCCESS);
}
