#include "s3cpp/aws/iam/canonicalize.hpp"
#include "s3cpp/aws/iam/sign_request.hpp"
#include "s3cpp/aws/iam/string_to_sign.hpp"
#include "s3cpp/aws/scope.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>     // IWYU pragma: keep
#include <boost/beast/http/string_body.hpp> // IWYU pragma: keep
#include <boost/beast/http/verb.hpp>
#include <cassert>
#include <iostream>

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    boost::beast::http::request<boost::beast::http::string_body> request{boost::beast::http::verb::put,
                                                                         "/-/vaults/examplevault", 11};
    request.set(boost::beast::http::field::host, "glacier.us-east-1.amazonaws.com");
    request.set("x-amz-glacier-version", "2012-06-01");
    request.set("x-amz-date", "20120525T002453Z");

    const auto &[canonical, signed_headers] = s3cpp::aws::iam::canonicalize_request(request);

    constexpr auto canonical_chk =
        R"---(PUT
/-/vaults/examplevault

host:glacier.us-east-1.amazonaws.com
x-amz-date:20120525T002453Z
x-amz-glacier-version:2012-06-01

host;x-amz-date;x-amz-glacier-version
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)---";

    if (canonical != canonical_chk) {
        std::cerr << "canonicalize_request failed, got \n" << canonical << "\n";
        return 1;
    }

    const s3cpp::aws::Scope scope{.timestamp = "20120525", .region = "us-east-1", .service = "glacier"};

    const auto string_to_sign = s3cpp::aws::iam::string_to_sign(canonical, scope, "20120525T002453Z");

    constexpr auto string_to_sign_chk =
        R"---(AWS4-HMAC-SHA256
20120525T002453Z
20120525/us-east-1/glacier/aws4_request
5f1da1a2d0feb614dd03d71e87928b8e449ac87614479332aced3a701f916743)---";

    if (string_to_sign != string_to_sign_chk) {
        std::cerr << "string_to_sign failed, got \n" << string_to_sign << "\n";
        return 1;
    }

    const auto auth =
        s3cpp::aws::iam::sign_request("AKIAIOSFODNN7EXAMPLE", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
                                      canonical, signed_headers, "20120525T002453Z", scope);

    constexpr auto auth_chk =
        R"---(AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20120525/us-east-1/glacier/aws4_request,SignedHeaders=host;x-amz-date;x-amz-glacier-version,Signature=3ce5b2f2fffac9262b4da9256f8d086b4aaf42eba5f111c21681a65a127b7c2a)---";

    if (auth != auth_chk) {
        std::cerr << "sign_request failed, got \n" << auth << "\n";
        return 1;
    }
}
