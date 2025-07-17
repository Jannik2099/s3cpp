#include "s3cpp/aws/iam/sign_request.hpp"

#include "s3cpp/aws/iam/string_to_sign.hpp"
#include "s3cpp/aws/scope.hpp"

#include <botan/hex.h>
#include <botan/mac.h>
#include <cassert>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// see https://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
// for the signing scheme

namespace s3cpp::aws::iam {

std::vector<uint8_t> get_signing_key(std::string_view secret_access_key, const Scope &scope) {

    auto hmac = Botan::MessageAuthenticationCode::create_or_throw("HMAC(SHA-256)");
    assert(hmac != nullptr);

    std::vector<uint8_t> hmac_key;

    hmac_key.reserve(256);
    std::format_to(std::back_inserter(hmac_key), "AWS4{}", secret_access_key);

    // DateKey
    hmac->set_key(hmac_key);
    hmac->update(scope.timestamp);

    hmac_key.clear();

    hmac->final(hmac_key);
    hmac->clear();

    // DateRegionKey
    hmac->set_key(hmac_key);
    hmac->update(scope.region);

    hmac_key.clear();

    hmac->final(hmac_key);
    hmac->clear();

    // DateRegionServiceKey
    hmac->set_key(hmac_key);
    hmac->update(scope.service);

    hmac_key.clear();

    hmac->final(hmac_key);
    hmac->clear();

    // SigningKey
    hmac->set_key(hmac_key);
    hmac->update(Scope::footer);

    return hmac->final_stdvec();
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::string sign_request(std::string_view access_key, std::string_view secret_access_key,
                         std::string_view canonical_request, std::string_view signed_headers,
                         std::string_view timestamp, const Scope &scope) {

    auto hmac = Botan::MessageAuthenticationCode::create_or_throw("HMAC(SHA-256)");
    assert(hmac != nullptr);

    hmac->set_key(get_signing_key(secret_access_key, scope));
    const std::string string_to_sign_ = string_to_sign(canonical_request, scope, timestamp);
    hmac->update(string_to_sign_);

    const std::string signature = Botan::hex_encode(hmac->final(), false);

    return std::format("AWS4-HMAC-SHA256 Credential={}/{},SignedHeaders={},Signature={}", access_key, scope,
                       signed_headers, signature);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace s3cpp::aws::iam
