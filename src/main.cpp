#include "ast.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "passes/assign_homes.h"
#include "passes/cast_insert.h"
#include "passes/convert_assignments.h"
#include "passes/convert_to_closures.h"
#include "passes/emit.h"
#include "passes/explicate_control.h"
#include "passes/expose_allocation.h"
#include "passes/limit_functions.h"
#include "passes/patch_instructions.h"
#include "passes/prelude_conclusion.h"
#include "passes/rco.h"
#include "passes/reveal_casts.h"
#include "passes/reveal_functions.h"
#include "passes/select_instructions.h"
#include "passes/shrink.h"
#include "passes/uncover_get.h"
#include "passes/uniquify.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

using namespace mc;

namespace {

/// @brief Exit status used for a trapped dynamic type error
constexpr int kTrappedErrorStatus = 255;

/// @brief Format a diagnostic as `file:line:col: kind: message`
/// @ensures the position is omitted when unknown, which happens for nodes a
///          pass synthesised rather than parsed (notably under --dyn, where
///          type checking runs after cast_insert)
std::string diagnostic(const std::string &file, SourceLoc loc,
                       const char *kind, const std::string &msg) {
    std::string out = file;
    if (loc.known()) {
        out += ":" + std::to_string(loc.line) + ":" + std::to_string(loc.col);
    }
    return out + ": " + kind + ": " + msg;
}

/// @brief Open and parse .mc file into AST
/// @requires filename is a readable .mc file path
/// @ensures result has value on success, nullopt on file error;
///          `dyn` selects the annotation-free L_Dyn grammar
std::optional<Program> parse_file(const std::string &filename, bool dyn) {
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "error: cannot open " << filename << "\n";
        return std::nullopt;
    }
    Lexer lex(ifs);
    Parser parser(lex, dyn);
    try {
        auto p = parser.parse_program();
        return Program{std::move(p->defs), std::move(p->body)};
    } catch (const ParseError &e) {
        std::cerr << diagnostic(filename, e.loc, "error", e.what()) << "\n";
        return std::nullopt;
    }
}

/// @brief Shrink, uniquify, reveal functions, then insert casts for L_Dyn
/// @ensures in dyn mode every subexpression has type Any, and the program's
///          value is projected to Int so the exit status is a plain integer
std::unique_ptr<Program> to_l_any(const Program &prog, bool dyn) {
    auto p0 = shrink(prog);
    auto p1 = uniquify(*p0);
    auto p1a = reveal_functions(*p1);
    if (!dyn) return p1a;
    auto p1b = cast_insert(*p1a);
    p1b->body =
        std::make_unique<ProjectExpr>(std::move(p1b->body), int_type());
    return p1b;
}

/// @brief Front half of the pipeline, up to and including reveal_casts
/// @ensures result is a type-checked L_Any program with no casts left
/// @ensures throws TypeError if the program is ill-typed
std::unique_ptr<Program> front_end(const Program &prog, bool dyn) {
    if (!dyn) type_check(prog);
    auto any_prog = to_l_any(prog, dyn);
    if (dyn) type_check(*any_prog);
    return reveal_casts(*any_prog);
}

/// @brief Full compilation pipeline: parse -> type check -> passes -> emit x86
/// @requires src_file is a valid .mc file
/// @ensures out_file contains AT&T x86-64 assembly on success (returns 0)
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
int compile(const std::string &src_file, const std::string &out_file,
            bool dyn) {
    auto prog = parse_file(src_file, dyn);
    if (!prog) return 1;

    std::unique_ptr<Program> front;
    try { front = front_end(*prog, dyn); }
    catch (const TypeError &e) {
        std::cerr << diagnostic(src_file, e.loc, "type error", e.what()) << "\n";
        return 1;
    }

    auto p1ca = convert_assignments(*front);
    auto p1cc = convert_to_closures(*p1ca);
    auto p1lf = limit_functions(*p1cc); // packs >6 args into a tuple
    auto p1b = uncover_get(*p1lf);
    auto p1c = expose_allocation(*p1b);
    auto p2 = remove_complex_operands(*p1c);
    auto c_prog = explicate_control(*p2);
    auto x1 = select_instructions(c_prog);
    auto x2 = assign_homes(x1);
    auto x3 = patch_instructions(x2);
    auto x4 = generate_prelude_conclusion(x3);
    std::string asm_out = emit(x4);

    std::ofstream ofs(out_file);
    if (!ofs) {
        std::cerr << "error: cannot write " << out_file << "\n";
        return 1;
    }
    ofs << asm_out;
    return 0;
}

/// @brief Print an interpreted result
void print_value(const Value &result) {
    if (const auto *i = std::get_if<int64_t>(&result)) {
        std::cout << *i << "\n";
    } else if (const auto *b = std::get_if<bool>(&result)) {
        std::cout << (*b ? "true" : "false") << "\n";
    } else if (std::holds_alternative<Tuple>(result)) {
        std::cout << "(vector ...)" << "\n";
    } else if (std::holds_alternative<FunctionValue>(result) ||
               std::holds_alternative<ClosureRef>(result)) {
        std::cout << "(procedure ...)" << "\n";
    } else {
        std::cout << "(void)" << "\n";
    }
}

/// @brief Parse, type-check, and interpret program; print result to stdout
/// @requires src_file is a valid .mc file
/// @ensures prints result value to stdout, returns 0 on success; a trapped
///          dynamic type error returns 255, matching the compiled program
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
int run_interpret(const std::string &src_file, const std::string &input_file,
                  bool dyn) {
    auto prog = parse_file(src_file, dyn);
    if (!prog) return 1;

    // Dynamic programs are interpreted after cast insertion so the interpreter
    // and the compiled code agree on exactly where casts happen.
    std::unique_ptr<Program> dyn_prog;
    try {
        if (dyn) dyn_prog = to_l_any(*prog, true);
        type_check(dyn ? *dyn_prog : *prog);
    } catch (const TypeError &e) {
        std::cerr << diagnostic(src_file, e.loc, "type error", e.what()) << "\n";
        return 1;
    }
    const Program &target = dyn ? *dyn_prog : *prog;

    try {
        if (input_file.empty()) {
            print_value(interpret(target, std::cin));
            return 0;
        }
        std::ifstream ifs(input_file);
        if (!ifs) {
            std::cerr << "error: cannot open " << input_file << "\n";
            return 1;
        }
        print_value(interpret(target, ifs));
    } catch (const TrappedError &e) {
        std::cerr << "trapped error: " << e.what() << "\n";
        return kTrappedErrorStatus;
    }
    return 0;
}

/// @brief Strip a leading "--dyn" flag from argv
/// @modifies argc, argv
/// @ensures result is true iff "--dyn" was present and removed
bool take_dyn_flag(int &argc, char **argv) {
    // invariant: argv[1..i) has been scanned and does not hold --dyn
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dyn") != 0) continue;
        // decreases: argc - j
        for (int j = i; j + 1 < argc; ++j) argv[j] = argv[j + 1];
        --argc;
        return true;
    }
    return false;
}

void usage() {
    std::cerr << "usage: mc [--dyn] <input.mc> -o <output.s>\n";
    std::cerr << "       mc -i [--dyn] <input.mc> [-input <input.txt>]\n";
}

} // namespace

/// @brief CLI entry point: compile or interpret .mc files
/// @requires argc >= 2
int main(int argc, char *argv[]) {
    bool dyn = take_dyn_flag(argc, argv);
    if (argc < 2) {
        usage();
        return 1;
    }

    if (std::strcmp(argv[1], "-i") == 0) {
        if (argc < 3) {
            usage();
            return 1;
        }
        std::string input_file;
        if (argc >= 5 && std::strcmp(argv[3], "-input") == 0) {
            input_file = argv[4];
        }
        return run_interpret(argv[2], input_file, dyn);
    }

    if (argc < 4 || std::strcmp(argv[2], "-o") != 0) {
        usage();
        return 1;
    }
    return compile(argv[1], argv[3], dyn);
}
