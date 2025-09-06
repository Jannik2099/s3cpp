#include "iam.hpp"

#include "../../common.hpp"
#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/aws/iam/sign_request.hpp"
#include "s3cpp/aws/iam/string_to_sign.hpp"

#include <boost/url/url.hpp>
#include <nanobind/nanobind.h>
#include <string_view>

namespace nb = nanobind;

using namespace s3cpp::aws::iam;

namespace s3cpp::bindings::python::aws::iam {

void _register(nanobind::module_ &_module) {
    nb::module_ iam = _module.def_submodule("iam");

    nb::class_<Session>(iam, "Session")
        .def(nb::init<>())
        .def_rw("access_key", &Session::access_key)
        .def_rw("secret_access_key", &Session::secret_access_key)
        .def_rw("region", &Session::region)
        .def_prop_rw(
            "endpoint", [](const Session &session) { return session.endpoint.c_str(); },
            [](Session &session, std::string_view endpoint) {
                session.endpoint = boost::urls::url{endpoint};
            });

    iam.def("get_signing_key", &get_signing_key, nb::arg("secret_access_key"), nb::arg("scope"));
    iam.def("sign_request", &sign_request, nb::arg("access_key"), nb::arg("secret_access_key"),
            nb::arg("canonical_request"), nb::arg("signed_headers"), nb::arg("timestamp"), nb::arg("scope"));

    iam.def("string_to_sign", &string_to_sign, nb::arg("canonical_request"), nb::arg("scope"),
            nb::arg("timestamp"));
}

} // namespace s3cpp::bindings::python::aws::iam
