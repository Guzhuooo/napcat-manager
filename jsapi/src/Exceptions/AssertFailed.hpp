// 到处用：条件不成立就抛
#pragma once
#include "Exception.hpp"

class AssertFailedException : public Exception
{
public:
    AssertFailedException(const char *file, int line, const std::string &message)
        : Exception(file, line, "Assertion failed: " + message) {}
};
#define THROW_ASSERT_FAILED(message) throw AssertFailedException(__FILE__, __LINE__, message)
#define ASSERT(condition)                                                  \
    if (!(condition)) THROW_ASSERT_FAILED("Assertion failed: " #condition)