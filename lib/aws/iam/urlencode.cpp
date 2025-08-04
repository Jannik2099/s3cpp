#include "s3cpp/aws/iam/urlencode.hpp"

#include <boost/url/encode.hpp> // IWYU pragma: keep
#include <boost/url/grammar/charset.hpp>
#include <boost/url/grammar/lut_chars.hpp>
#include <string>
#include <string_view>

namespace s3cpp::aws::iam {

namespace {

constexpr boost::urls::grammar::lut_chars charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                    "abcdefghijklmnopqrstuvwxyz"
                                                    "1234567890"
                                                    "-._";

constexpr boost::urls::grammar::lut_chars path_charset = charset + "/";

constexpr boost::urls::grammar::lut_chars query_charset = charset + "?=";

} // namespace

std::string urlencode(std::string_view input) { return boost::urls::encode(input, charset); }
bool urlencode_required(std::string_view input) {
    return boost::urls::grammar::find_if(input.begin(), input.end(), charset) != input.end();
}

std::string urlencode_path(std::string_view input) { return boost::urls::encode(input, path_charset); }
bool urlencode_path_required(std::string_view input) {
    return boost::urls::grammar::find_if(input.begin(), input.end(), path_charset) != input.end();
}

std::string urlencode_query(std::string_view input) { return boost::urls::encode(input, query_charset); }
bool urlencode_query_required(std::string_view input) {
    return boost::urls::grammar::find_if(input.begin(), input.end(), query_charset) != input.end();
}

} // namespace s3cpp::aws::iam
