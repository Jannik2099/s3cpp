#include "aws.hpp"

#include "../common.hpp"
#include "iam/iam.hpp"
#include "s3/s3.hpp"
#include "s3cpp/aws/scope.hpp"

#include <nanobind/nanobind.h>

namespace nb = nanobind;

using namespace s3cpp::aws;

namespace s3cpp::bindings::python::aws {

void _register(nanobind::module_ &_module) {
    nb::module_ aws = _module.def_submodule("aws");

    nb::class_<Scope>(aws, "Scope")
        .def(nb::init<>())
        .def_rw("timestamp", &Scope::timestamp)
        .def_rw("region", &Scope::region)
        .def_rw("service", &Scope::service)
        .def_ro_static("footer", &Scope::footer);

    iam::_register(aws);
    s3::_register(aws);
}

} // namespace s3cpp::bindings::python::aws
