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
                                                                         "/test/object", 11};
    request.set(boost::beast::http::field::host, "rgw.ceph.jgspace.org:7840");
    request.set("x-amz-date", "20240831T234309Z");
    request.set("x-amz-content-sha256", "810ff2fb242a5dee4220f2cb0e6a519891fb67f2f828a6cab4ef8894633b1f50");
    request.set("Content-MD5", "72VMQKtPF0f8aZkV1PcJAg==");

    const auto &[canonical, signed_headers] = s3cpp::aws::iam::canonicalize_request(request);

    constexpr auto canonical_chk =
        R"---(PUT
/test/object

content-md5:72VMQKtPF0f8aZkV1PcJAg==
host:rgw.ceph.jgspace.org:7840
x-amz-content-sha256:810ff2fb242a5dee4220f2cb0e6a519891fb67f2f828a6cab4ef8894633b1f50
x-amz-date:20240831T234309Z

content-md5;host;x-amz-content-sha256;x-amz-date
810ff2fb242a5dee4220f2cb0e6a519891fb67f2f828a6cab4ef8894633b1f50)---";

    if (canonical != canonical_chk) {
        std::cerr << "canonicalize_request failed, got \n" << canonical << "\n";
        return 1;
    }

    const s3cpp::aws::Scope scope{.timestamp = "20240831", .region = "default", .service = "s3"};

    const auto string_to_sign = s3cpp::aws::iam::string_to_sign(canonical, scope, "20240831T234309Z");

    constexpr auto string_to_sign_chk =
        R"---(AWS4-HMAC-SHA256
20240831T234309Z
20240831/default/s3/aws4_request
a57617be1f42084e6c3fe7c050fd45b5d834577342d2da43cc248bbacff5746b)---";

    if (string_to_sign != string_to_sign_chk) {
        std::cerr << "string_to_sign failed, got \n" << string_to_sign << "\n";
        return 1;
    }

    const auto auth =
        s3cpp::aws::iam::sign_request("MFPLGSQ8XT86RRZ7WGMI", "5GIcBiiLd4ZuXONNYHkMDDdx1zrAHaCODyVlA2TB",
                                      canonical, signed_headers, "20240831T234309Z", scope);

    constexpr auto auth_chk =
        R"---(AWS4-HMAC-SHA256 Credential=MFPLGSQ8XT86RRZ7WGMI/20240831/default/s3/aws4_request,SignedHeaders=content-md5;host;x-amz-content-sha256;x-amz-date,Signature=ed20d0d789c7565c0cce7dbb917ee5968d935fe109abbd824dcc617129e6a5a6)---";

    if (auth != auth_chk) {
        std::cerr << "sign_request failed, got \n" << auth << "\n";
        return 1;
    }
}
