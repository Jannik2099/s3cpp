#include "s3cpp/aws/iam/string_to_sign.hpp"

#include "s3cpp/aws/scope.hpp"

#include <botan/hash.h>
#include <botan/hex.h>
#include <cassert>
#include <format>
#include <string>
#include <string_view>

namespace s3cpp::aws::iam {

std::string string_to_sign(std::string_view canonical_request, const Scope &scope,
                           std::string_view timestamp) {

    auto hash = Botan::HashFunction::create_or_throw("SHA-256");
    assert(hash != nullptr);
    hash->update(canonical_request);

    const std::string digest = Botan::hex_encode(hash->final(), false);

    return std::format("AWS4-HMAC-SHA256\n{}\n{}\n{}", timestamp, scope, digest);
}

} // namespace s3cpp::aws::iam
