#include "s3cpp/aws/s3/types.hpp"

#include <boost/describe/enum_from_string.hpp>
#include <charconv>
#include <cstring>
#include <format>
#include <pugixml.hpp>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace s3cpp::aws::s3 {

Object::Object(const pugi::xml_node &xml) {
    if (const char *parsed = xml.child_value("ChecksumAlgorithm");
        parsed != nullptr && std::strlen(parsed) > 0) {
        enum ChecksumAlgorithm_t chk {};
        if (!boost::describe::enum_from_string(parsed, chk)) {
            throw std::runtime_error{std::format("unknown checksum algorithm {}", parsed)};
        }
        ChecksumAlgorithm = chk;
    }

    if (const char *parsed = xml.child_value("ChecksumType"); parsed != nullptr && std::strlen(parsed) > 0) {
        enum ChecksumType_t chk {};
        if (!boost::describe::enum_from_string(parsed, chk)) {
            throw std::runtime_error{std::format("unknown checksum type {}", parsed)};
        }
        ChecksumType = chk;
    }

    if (const char *parsed = xml.child_value("ETag"); parsed != nullptr) {
        ETag = parsed;
    }

    if (const char *parsed = xml.child_value("Key"); parsed != nullptr) {
        Key = parsed;
    }

    if (const char *parsed = xml.child_value("Size"); parsed != nullptr) {
        const std::string_view size_str{parsed};
        std::size_t parsed_size{};
        if (const auto res = std::from_chars(size_str.begin(), size_str.end(), parsed_size);
            res.ec != std::errc{}) {
            throw std::runtime_error{"failed to parse object size"};
        }
        Size = parsed_size;
    }
}

} // namespace s3cpp::aws::s3
