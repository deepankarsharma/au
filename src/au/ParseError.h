#pragma once

#include <sstream>
#include <stdexcept>
#include <string>

#define STR(EXPRS) (([&]() -> std::string { \
    std::ostringstream oss_; \
    oss_ << EXPRS; \
    return oss_.str(); \
    })())

#define THROW(stuff) \
  do { \
    std::ostringstream _message; \
    _message << stuff; \
    throw parse_error(_message.str()); \
  } while (0)

#define THROW_RT(stuff) throw std::runtime_error(STR(stuff))

struct parse_error : std::runtime_error {
  explicit parse_error(const std::string &what)
      : std::runtime_error(what) {}
};
