#pragma once

#include "internal.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <exception>
#include <memory>
#include <nanobind/nanobind.h>
#include <optional>

// we generally want all of these
// IWYU pragma: begin_keep
#include <nanobind/stl/array.h>
#include <nanobind/stl/bind_map.h>
#include <nanobind/stl/bind_vector.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/complex.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/list.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/wstring.h>
// IWYU pragma: end_keep

namespace nb = nanobind;

namespace s3cpp::bindings::python {

// Iterator class that wraps a boost::asio::awaitable and makes it Python-awaitable
template <typename T> class awaitable_iterator {
public:
    using value_type = typename T::value_type;

private:
    T awaitable_;
    std::unique_ptr<boost::asio::io_context> ctx_ = std::make_unique<boost::asio::io_context>();
    std::optional<value_type> result_;
    std::exception_ptr exception_;
    bool completed_ = false;
    bool started_ = false;

public:
    // Destructor
    ~awaitable_iterator() = default;

    explicit awaitable_iterator(T &&awaitable) : awaitable_(std::move(awaitable)) {}
    awaitable_iterator(awaitable_iterator &&other) noexcept = default;
    awaitable_iterator(const awaitable_iterator &) = delete;
    awaitable_iterator &operator=(awaitable_iterator &&other) noexcept = default;
    awaitable_iterator &operator=(const awaitable_iterator &) = delete;

    // Iterator protocol
    awaitable_iterator &iter() { return *this; }

    nb::object next() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        if (completed_) {
            // Manually raise StopIteration with the result as the value
            if (result_) {
                PyObject *exc_value = nb::cast(*result_).release().ptr();
                PyErr_SetObject(PyExc_StopIteration, exc_value);
                Py_DECREF(exc_value); // PyErr_SetObject increments ref count, so we decrement our ownership
            } else {
                PyErr_SetObject(PyExc_StopIteration, Py_None);
                // No need to manage Py_None reference count manually
            }
            throw nb::python_error{};
        }

        if (!started_) {
            started_ = true;

            // Start the coroutine
            boost::asio::co_spawn(*ctx_, std::move(awaitable_),
                                  [this](const std::exception_ptr &exception, value_type &&result) {
                                      if (exception) {
                                          exception_ = exception;
                                      } else {
                                          result_ = std::move(result);
                                      }
                                      completed_ = true;
                                  });
        }

        // Run the IO context until it would block or complete
        // This approach runs all immediately available work
        ctx_->poll();

        if (exception_) {
            std::rethrow_exception(exception_);
        }

        if (completed_) {
            // Manually raise StopIteration with the result as the value
            if (result_) {
                PyObject *exc_value = nb::cast(*result_).release().ptr();
                PyErr_SetObject(PyExc_StopIteration, exc_value);
                Py_DECREF(exc_value); // PyErr_SetObject increments ref count, so we decrement our ownership
            } else {
                PyErr_SetObject(PyExc_StopIteration, Py_None);
                // No need to manage Py_None reference count manually
            }
            throw nb::python_error{};
        }

        // Return None to indicate we're suspended but not done
        // This allows the Python event loop to do other work
        return nb::none();
    }
};

template <typename T, typename M> static inline void bind_awaitable(M &module_) {
    using iterator_type = awaitable_iterator<T>;
    using value_type = typename T::value_type;

    // hide these types for now
    // python typing is wonderfully broken as always. collections.abc.Awaitable's __await__ expects a
    // Generator, even though PEP492 specifies it as an Iterator
    // and even then, the actual Iterator signature is implementation-defined. What the fuck, Python?
    const std::string name = std::format("_Awaitable_{}", bound_type_name<value_type>::unqualified_str);
    const std::string iter_name =
        std::format("_AwaitableIterator_{}", bound_type_name<value_type>::unqualified_str);

    // Bind the iterator class
    nb::class_<iterator_type> iter_class(
        module_, iter_name.c_str(),
        nb::sig{std::format("class {}(collections.abc.Iterator[{}])", iter_name,
                            bound_type_name<value_type>::qualified_descr.text)
                    .c_str()});
    iter_class.def("__iter__", &iterator_type::iter);
    iter_class.def("__next__", &iterator_type::next);

    // Bind the awaitable class
    nb::class_<T> ret = nb::class_<T>(module_, name.c_str(),
                                      nb::sig{std::format("class {}(collections.abc.Awaitable[{}])", name,
                                                          bound_type_name<value_type>::qualified_descr.text)
                                                  .c_str()});

    ret.def("__await__", [](T &&self) { return iterator_type(std::move(self)); });
}

} // namespace s3cpp::bindings::python
