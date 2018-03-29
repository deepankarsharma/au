#include "main.h"
#include "AuOutputHandler.h"
#include "JsonOutputHandler.h"
#include "GrepHandler.h"
#include "TclapHelper.h"
#include "au/AuDecoder.h"

#include <chrono>
#include <cstdlib>
#include <optional>
#include <regex>

namespace {

bool setSignedPattern(Pattern &pattern, std::string &intPat) {
  const char *str = intPat.c_str();
  char *end;
  errno = 0;
  int64_t val = strtoll(str, &end, 10);
  if (errno == ERANGE) return false;
  if (end != str + intPat.size()) return false;
  pattern.intPattern = val;
  return true;
}

bool setUnsignedPattern(Pattern &pattern, std::string &intPat) {
  const char *str = intPat.c_str();
  char *end;
  errno = 0;
  uint64_t val = strtoull(str, &end, 10);
  if (errno == ERANGE) return false;
  if (!isdigit(*str)) return false; // don't allow negatives
  if (end != str + intPat.size()) return false;
  pattern.uintPattern = val;
  return true;
}

bool setIntPattern(Pattern &pattern, std::string &intPat) {
  return setSignedPattern(pattern, intPat) |
    setUnsignedPattern(pattern, intPat);
}

bool setDoublePattern(Pattern &pattern, std::string &intPat) {
  const char *str = intPat.c_str();
  char *end;
  errno = 0;
  double val = strtod(str, &end);
  if (errno == ERANGE) return false;
  if (!isdigit(*str)) return false; // don't allow negatives
  if (end != str + intPat.size()) return false;
  pattern.doublePattern = val;
  return true;
}

bool setAtomPattern(Pattern &pattern, std::string &atomPat) {
  if (atomPat == "true") {
    pattern.atomPattern = Pattern::Atom::True;
    return true;
  }
  if (atomPat == "false") {
    pattern.atomPattern = Pattern::Atom::False;
    return true;
  }
  if (atomPat == "null") {
    pattern.atomPattern = Pattern::Atom::Null;
    return true;
  }
  return false;
}

bool parsePrefix(std::string_view &str, size_t len, char delim, int &start,
                 int &end, int max, int min = 0, int base = 0) {
  if (str.empty()) {
    start = end = 0;
    return true;
  }

  auto result = 0;
  auto i = 0u;
  for (; i < len; i++) {
    if (i == str.size()) break;
    auto c = str[i];
    if (c == delim) return false;
    if (!isdigit(c)) return false;
    result = 10 * result + c - '0';
  }
  str.remove_prefix(i);
  start = end = result;
  if (str.empty()) {
    end += 1;
  } else {
    if (str[0] != delim) return false;
    str.remove_prefix(1);
    if (str.empty()) return false;
  }
  for (; i < len; i++) {
    start *= 10;
    end *= 10;
  }
  if (start < min || start > max) return false;
  start -= base;
  if (end < min || end > max + 1) return false;
  end -= base;
  return true;
}

bool setTimestampPattern(Pattern &pattern, const std::string &tsPat) {
  std::tm start;
  std::tm end;
  memset(&start, 0, sizeof(tm));
  memset(&end, 0, sizeof(tm));

  std::string_view sv(tsPat);
  if (!parsePrefix(sv, 4, '-', start.tm_year, end.tm_year, 9999, 1900, 1900))
    return false;
  if (!parsePrefix(sv, 2, '-', start.tm_mon, end.tm_mon, 12, 1, 1))
    return false;
  if (!parsePrefix(sv, 2, 'T', start.tm_mday, end.tm_mday, 31, 1)) return false;
  if (!parsePrefix(sv, 2, ':', start.tm_hour, end.tm_hour, 12)) return false;
  if (!parsePrefix(sv, 2, ':', start.tm_min, end.tm_min, 59)) return false;
  if (!parsePrefix(sv, 2, '.', start.tm_sec, end.tm_sec, 59)) return false;

  int startNanos;
  int endNanos;
  if (!parsePrefix(sv, 9, 0, startNanos, endNanos, 999999999)) return false;

  std::time_t ttstart = timegm(&start);
  std::time_t ttend = timegm(&end);
  if (ttstart == -1 || ttend == -1) return false;

  using namespace std::chrono;
  auto epoch = std::chrono::system_clock::time_point();
  auto startInt = epoch + duration_cast<nanoseconds>(
      seconds(ttstart) + nanoseconds(startNanos));
  auto endInt = epoch + duration_cast<nanoseconds>(
      seconds(ttend) + nanoseconds(endNanos));

  if (startInt == endInt) endInt += nanoseconds(1);
  pattern.timestampPattern = std::make_pair(startInt, endInt);
  return true;
}

void grepFile(Pattern &pattern,
              const std::string &fileName,
              bool encodeOutput) {
  if (encodeOutput) {
    AuOutputHandler handler(
        STR("Encoded by au: grep output from json file "
                << (fileName == "-" ? "<stdin>" : fileName)));
    doGrep(pattern, fileName, handler);
  } else {
    JsonOutputHandler handler;
    doGrep(pattern, fileName, handler);
  }
}

void usage() {
  std::cout
      << "usage: au grep [options] [--] <pattern> <path>...\n"
      << "\n"
      << "  -h --help           show usage and exit\n"
      << "  -e --encode         output au-encoded records rather than json\n"
      << "  -k --key <key>      match pattern only in object values with key <key>\n"
      << "  -o --ordered <key>  like -k, but values for <key> are assumed to to be\n"
      << "                      roughly ordered\n"
      << "  -i --integer        match <pattern> with integer values\n"
      << "  -d --double         match <pattern> with double-precision float values\n"
      << "  -t --timestamp      match <pattern> with timestamps: format is\n"
      << "                      2018-03-27T18:45:00.123456789 or any prefix thereof\n"
      << "                      2018-03-27T18:45:00.123, 2018-03-27T18:4, 2018-03, etc.\n"
      << "  -a --atom           match <pattern> only with atomic literals:\n"
      << "                      true, false, null\n"
      << "  -s --string         match <pattern> with string values\n"
      << "  -u --substring      match <pattern> as a substring of string values\n"
      << "                      implies -s, not compatible with -i/-d\n"
      << "  -m --matches <n>    show only the first <n> matching records\n"
      << "  -B --before <n>     show <n> records of context before each match\n"
      << "  -A --after <n>      show <n> records of context after each match\n"
      << "  -C --context <n>    equivalent to -A n -B n\n"
      << "  -c --count          print count of matching records per file\n";
}

}

int grep(int argc, const char * const *argv) {
  TclapHelper tclap(usage);

  TCLAP::ValueArg<std::string> key(
      "k", "key", "key", false, "", "string", tclap.cmd());
  TCLAP::ValueArg<std::string> ordered(
      "o", "ordered", "oredered", false, "", "string", tclap.cmd());
  TCLAP::ValueArg<uint32_t> context(
      "C", "context", "context", false, 0, "uint32_t", tclap.cmd());
  TCLAP::ValueArg<uint32_t> before(
      "B", "before", "before", false, 0, "uint32_t", tclap.cmd());
  TCLAP::ValueArg<uint32_t> after(
      "A", "after", "after", false, 0, "uint32_t", tclap.cmd());
  TCLAP::ValueArg<uint32_t> matches(
      "m", "matches", "matches", false, 0, "uint32_t", tclap.cmd());
  TCLAP::SwitchArg encode("e", "encode", "encode", tclap.cmd());
  TCLAP::SwitchArg count("c", "count", "count", tclap.cmd());
  TCLAP::SwitchArg matchAtom("a", "atom", "atom", tclap.cmd());
  TCLAP::SwitchArg matchInt("i", "integer", "integer", tclap.cmd());
  TCLAP::SwitchArg matchTimestamp("t", "timestamp", "timestamp", tclap.cmd());
  TCLAP::SwitchArg matchDouble("d", "double", "double", tclap.cmd());
  TCLAP::SwitchArg matchString("s", "string", "string", tclap.cmd());
  TCLAP::SwitchArg matchSubstring("u", "substring", "substring", tclap.cmd());
  TCLAP::UnlabeledValueArg<std::string> pat(
      "pattern", "", true, "", "pattern", tclap.cmd());
  TCLAP::UnlabeledMultiArg<std::string> fileNames(
      "path", "", false, "path", tclap.cmd());

  if (!tclap.parse(argc, argv)) return 1;

  if (key.isSet() && ordered.isSet()) {
    std::cerr << "only one of -k or -o may be specified." << std::endl;
    return 1;
  }

  Pattern pattern;
  if (key.isSet()) pattern.keyPattern = key.getValue();
  if (ordered.isSet()) {
    pattern.keyPattern = ordered.getValue();
    pattern.bisect = true;
  }

  if (matches.isSet()) pattern.numMatches = matches.getValue();

  bool explicitStringMatch = matchString.isSet() || matchSubstring.isSet();
  bool numericMatch = matchInt.isSet() || matchDouble.isSet()
                      || matchTimestamp.isSet() || matchAtom.isSet();
  bool defaultMatch = !(numericMatch || explicitStringMatch);

  if (matchSubstring.isSet() && numericMatch) {
    std::cerr << "-u (substring search) is not compatible with -i/-d/-t/-a."
              << std::endl;
    return 1;
  }

  // by default, we'll try to match anything, but won't be upset if the
  // pattern fails to parse as any particular thing...

  if (defaultMatch || explicitStringMatch) {
    pattern.strPattern = Pattern::StrPattern{
        pat.getValue(), !matchSubstring.isSet()};
  }

  if (defaultMatch || matchInt.isSet()) {
    bool success = setIntPattern(pattern, pat.getValue());
    if (!success && matchInt.isSet()) {
      std::cerr << "-i specified, but pattern '"
                << pat.getValue() << "' is not an integer." << std::endl;
      return 1;
    }
  }

  if (defaultMatch || matchDouble.isSet()) {
    bool success = setDoublePattern(pattern, pat.getValue());
    if (!success && matchDouble.isSet()) {
      std::cerr << "-d specified, but pattern '"
                << pat.getValue() << "' is not a double-precision number."
                << std::endl;
      return 1;
    }
  }

  if (defaultMatch || matchTimestamp.isSet()) {
    bool success = setTimestampPattern(pattern, pat.getValue());
    if (!success && matchTimestamp.isSet()) {
      std::cerr << "-t specified, but pattern '"
                << pat.getValue() << "' is not a date/time."
                << std::endl;
      return 1;
    }
  }

  if (defaultMatch || matchAtom.isSet()) {
    bool success = setAtomPattern(pattern, pat.getValue());
    if (!success && matchAtom.isSet()) {
      std::cerr << "-a specified, but pattern '"
                << pat.getValue() << "' is not true, false or null."
                << std::endl;
      return 1;
    }
  }

  if (context.isSet())
    pattern.beforeContext = pattern.afterContext = context.getValue();
  if (before.isSet())
    pattern.beforeContext = before.getValue();
  if (after.isSet())
    pattern.afterContext = after.getValue();

  pattern.count = count.isSet();

  if (fileNames.getValue().empty()) {
    grepFile(pattern, "-", encode.isSet());
  } else {
    for (auto &f : fileNames) {
      grepFile(pattern, f, encode.isSet());
    }
  }

  return 0;
}

