// Tests de core : Status, ErrorCode et Result<T>.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "core/error.hpp"
#include "core/result.hpp"

using namespace moonlight::core;

namespace {

Result<int> parsePositive(int input) {
    if (input <= 0) {
        return Status::error(ErrorCode::InvalidArgument, "input must be positive");
    }
    return input * 2;
}

void testStatus() {
    const Status success = Status::ok();
    CHECK(success.isOk());
    CHECK(static_cast<bool>(success));
    CHECK(success.code() == ErrorCode::None);
    CHECK(success.message().empty());

    const Status failure = Status::error(ErrorCode::NotFound, "missing");
    CHECK(!failure.isOk());
    CHECK(!static_cast<bool>(failure));
    CHECK(failure.code() == ErrorCode::NotFound);
    CHECK(failure.message() == "missing");
}

void testErrorCodeNames() {
    CHECK(std::string(toString(ErrorCode::None)) == "None");
    CHECK(std::string(toString(ErrorCode::InvalidState)) == "InvalidState");
    CHECK(std::string(toString(ErrorCode::InvalidArgument)) == "InvalidArgument");
    CHECK(std::string(toString(ErrorCode::NotFound)) == "NotFound");
    CHECK(std::string(toString(ErrorCode::IoError)) == "IoError");
    CHECK(std::string(toString(ErrorCode::ParseError)) == "ParseError");
    CHECK(std::string(toString(ErrorCode::AccessDenied)) == "AccessDenied");
    CHECK(std::string(toString(ErrorCode::TooLarge)) == "TooLarge");
}

void testResultSuccess() {
    Result<int> result = parsePositive(21);
    CHECK_OK(result);
    CHECK(result.isOk());
    CHECK(static_cast<bool>(result));
    CHECK(result.value() == 42);
    CHECK(result.status().isOk());
    CHECK(result.valueOr(-1) == 42);
}

void testResultFailure() {
    Result<int> result = parsePositive(-3);
    CHECK(!result.isOk());
    CHECK_CODE(result, ErrorCode::InvalidArgument);
    CHECK(result.status().message() == "input must be positive");
    CHECK(result.valueOr(-1) == -1);
}

void testResultMoveOnlyAndContainers() {
    Result<std::unique_ptr<int>> owned = std::make_unique<int>(7);
    CHECK_OK(owned);
    std::unique_ptr<int> taken = std::move(owned).value();
    CHECK(taken != nullptr && *taken == 7);

    Result<std::vector<int>> list = std::vector<int>{1, 2, 3};
    CHECK_OK(list);
    CHECK(list.value().size() == 3);

    Result<std::string> text = Status::error(ErrorCode::IoError, "disk");
    CHECK_CODE(text, ErrorCode::IoError);
}

void testResultFromOkStatusIsAnError() {
    // Un Result ne peut pas etre "ok" sans valeur.
    Result<int> result = Status::ok();
    CHECK(!result.isOk());
    CHECK_CODE(result, ErrorCode::InvalidState);
}

}  // namespace

int main() {
    std::printf("moonlight_core_test\n");
    RUN_TEST(testStatus);
    RUN_TEST(testErrorCodeNames);
    RUN_TEST(testResultSuccess);
    RUN_TEST(testResultFailure);
    RUN_TEST(testResultMoveOnlyAndContainers);
    RUN_TEST(testResultFromOkStatusIsAnError);
    return moonlight::testing::finish("moonlight_core_test");
}
