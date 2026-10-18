#ifndef AUGUSTA_PACK_FAILURE_H_
#define AUGUSTA_PACK_FAILURE_H_

#include <exception>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>

/// \file
/// How the cooker's binding of augusta_assets (encoder_binding.h,
/// reader_binding.h) turns an error augusta_assets returns as a type
/// (ADR-0033) into a Python exception that keeps it: raised with the
/// augusta_assets phrase as its message and the error itself as its reason,
/// so a tool can tell refusals apart without matching on text. The binding
/// decides the message and which exception each error is.
namespace augusta::pack {

/// Thrown by the binding for a refused call; raised in Python as the exception
/// RegisterFailure<Error> registered.
template <typename Error>
class Failure : public std::runtime_error {
 public:
  Failure(Error reason, const std::string& message) : std::runtime_error(message), reason_(reason) {}

  [[nodiscard]] Error Reason() const { return reason_; }

 private:
  Error reason_;
};

/// Registers module.name, a subclass of base, as what a Failure<Error> raises,
/// with its reason as the exception's reason attribute. Error must already be
/// bound (pybind11::enum_). Once per Error: the translator finds the exception
/// by the name the first call gave.
template <typename Error>
void RegisterFailure(pybind11::module_& module, const char* name, pybind11::handle base) {
  // Looked up by name when raised, not kept: a pybind11 object held in a
  // static would outlive the interpreter.
  static const std::string kExceptionName = name;
  pybind11::exception<Failure<Error>>(module, name, base);
  pybind11::register_exception_translator([](std::exception_ptr exception) {
    try {
      if (exception) {
        std::rethrow_exception(exception);
      }
    } catch (const Failure<Error>& failure) {
      const pybind11::object type = pybind11::module_::import("augusta_pack._native").attr(kExceptionName.c_str());
      pybind11::object error = type(failure.what());
      error.attr("reason") = failure.Reason();
      // Python.h's, which pybind11.h includes for its users.
      PyErr_SetObject(type.ptr(), error.ptr());  // NOLINT(misc-include-cleaner)
    }
  });
}

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_FAILURE_H_
