// Mini harnais de test de MOONLIGHT (sans dependance externe).
//
//   CHECK(cond)            verifie une condition
//   CHECK_OK(expr)         expr (Status ou Result<T>) doit etre un succes
//   CHECK_CODE(expr, code) expr (Status ou Result<T>) doit echouer avec ce code
//   SKIP_TEST(raison)      abandonne le test courant en le comptant comme "saute"
//   RUN_TEST(fonction)     execute une fonction de test
//   return finish("nom");  affiche le bilan ; 0 si aucun echec
//
// Un echec n'interrompt pas le test : tous les echecs sont rapportes.
#pragma once

#include <cstdio>

#include "core/error.hpp"
#include "core/result.hpp"

namespace moonlight::testing {

struct Counters {
    int checks = 0;
    int failures = 0;
    int skipped = 0;
};

inline Counters& counters() {
    static Counters instance;
    return instance;
}

inline const moonlight::core::Status& statusOf(const moonlight::core::Status& status) {
    return status;
}

template <typename T>
const moonlight::core::Status& statusOf(const moonlight::core::Result<T>& result) {
    return result.status();
}

inline void reportFailure(const char* file, int line, const char* expression,
                          const moonlight::core::Status* status = nullptr) {
    ++counters().failures;
    if (status != nullptr) {
        std::fprintf(stderr, "FAIL %s:%d: %s  [%s: %s]\n", file, line, expression,
                     moonlight::core::toString(status->code()), status->message().c_str());
    } else {
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    }
}

inline int finish(const char* suiteName) {
    const Counters& c = counters();
    std::printf("%s: %d check(s), %d failure(s), %d skipped\n", suiteName, c.checks, c.failures,
                c.skipped);
    std::printf("RESULT: %s\n", c.failures == 0 ? "PASS" : "FAIL");
    return c.failures == 0 ? 0 : 1;
}

}  // namespace moonlight::testing

#define CHECK(condition)                                                             \
    do {                                                                             \
        ++::moonlight::testing::counters().checks;                                   \
        if (!(condition)) {                                                          \
            ::moonlight::testing::reportFailure(__FILE__, __LINE__, #condition);     \
        }                                                                            \
    } while (false)

#define CHECK_OK(expression)                                                         \
    do {                                                                             \
        ++::moonlight::testing::counters().checks;                                   \
        auto&& checkOkValue_ = (expression); /* prolonge la vie d'un temporaire */   \
        const auto& checkOkStatus_ = ::moonlight::testing::statusOf(checkOkValue_);  \
        if (!checkOkStatus_.isOk()) {                                                \
            ::moonlight::testing::reportFailure(__FILE__, __LINE__, #expression,     \
                                                &checkOkStatus_);                    \
        }                                                                            \
    } while (false)

#define CHECK_CODE(expression, expectedCode)                                         \
    do {                                                                             \
        ++::moonlight::testing::counters().checks;                                   \
        auto&& checkCodeValue_ = (expression); /* prolonge la vie d'un temporaire */ \
        const auto& checkCodeStatus_ = ::moonlight::testing::statusOf(checkCodeValue_); \
        if (checkCodeStatus_.code() != (expectedCode)) {                             \
            ::moonlight::testing::reportFailure(__FILE__, __LINE__,                  \
                                                #expression " == " #expectedCode,    \
                                                &checkCodeStatus_);                  \
        }                                                                            \
    } while (false)

#define SKIP_TEST(reason)                                                            \
    do {                                                                             \
        ++::moonlight::testing::counters().skipped;                                  \
        std::fprintf(stderr, "SKIP %s:%d: %s\n", __FILE__, __LINE__, reason);        \
        return;                                                                      \
    } while (false)

#define RUN_TEST(function)                                                           \
    do {                                                                             \
        std::printf("  %s\n", #function);                                            \
        function();                                                                  \
    } while (false)
