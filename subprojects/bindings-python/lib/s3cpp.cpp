#include "aws/aws.hpp"
#include "common.hpp"

#include <boost/beast.hpp>
#include <expected>
#include <nanobind/nanobind.h>
#include <nanobind/nb_defs.h>
#include <optional>

namespace nb = nanobind;

using namespace s3cpp::bindings::python;

NB_MODULE(s3cpp, _module) {
    nb::class_<std::expected<boost::beast::http::response<boost::beast::http::string_body>,
                             boost::beast::error_code>>{_module, "HttpResponse"}
        .def("has_value",
             [](const std::expected<boost::beast::http::response<boost::beast::http::string_body>,
                                    boost::beast::error_code> &self) { return self.has_value(); })
        .def("error",
             [](const std::expected<boost::beast::http::response<boost::beast::http::string_body>,
                                    boost::beast::error_code> &self) -> std::optional<std::string> {
                 if (self) {
                     return {};
                 }
                 return self.error().to_string();
             })
        .def("status",
             [](const std::expected<boost::beast::http::response<boost::beast::http::string_body>,
                                    boost::beast::error_code> &self) { return self.value().result_int(); })
        .def("body", [](const std::expected<boost::beast::http::response<boost::beast::http::string_body>,
                                            boost::beast::error_code> &self) { return self.value().body(); });
    aws::_register(_module);
}
