#include "s3.hpp"

#include "../../common.hpp"
#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/aws/s3/session.hpp"

#include <boost/beast/http/fields_fwd.hpp>
#include <boost/url/url.hpp>
#include <nanobind/nanobind.h>
#include <string_view>
#include <unordered_map>

namespace nb = nanobind;

using namespace s3cpp::aws::s3;

namespace s3cpp::bindings::python::aws::s3 {

void _register(nanobind::module_ &_module) {
    // NOLINTNEXTLINE(readability-identifier-length)
    nb::module_ s3 = _module.def_submodule("s3");

    bind_awaitable<meta::crt<boost::asio::awaitable<std::expected<
        boost::beast::http::response<boost::beast::http::string_body>, boost::beast::error_code>>>>(s3);

    nb::class_<Session, s3cpp::aws::iam::Session>{s3, "Session"}
        .def(nb::init<s3cpp::aws::iam::Session>())
        .def(
            "get",
            [] [[clang::coro_wrapper]] (Session & session, std::string_view path, std::string_view query,
                                        const std::unordered_map<std::string, std::string> &headers,
                                        bool is_path_encoded) {
                boost::beast::http::fields boost_headers;
                for (const auto &[key, value] : headers) {
                    boost_headers.set(key, value);
                }
                return session.get(path, query, std::move(boost_headers), is_path_encoded);
            },
            nb::sig{"def get(path: str, query: str = '', headers: dict[str, str] = {}, is_path_encoded: bool "
                    "= False) -> collections.abc.Awaitable[s3cpp.HttpResponse]"})
        .def(
            "put",
            [] [[clang::coro_wrapper]] (Session & session, std::string_view path, std::string_view data,
                                        const std::unordered_map<std::string, std::string> &headers,
                                        bool is_encoded) {
                boost::beast::http::fields boost_headers;
                for (const auto &[key, value] : headers) {
                    boost_headers.set(key, value);
                }
                return session.put(path, data, std::move(boost_headers), is_encoded);
            },
            nb::sig{"def put(path: str, data: str, headers: dict[str, str] = {}, is_path_encoded: bool = "
                    "False) -> collections.abc.Awaitable[s3cpp.HttpResponse]"});
}

} // namespace s3cpp::bindings::python::aws::s3
