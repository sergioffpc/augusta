#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "bindings.h"
#include "testing_server.h"

namespace augusta::agent::native {

namespace py = pybind11;

namespace {

// An object the script may close before Python lets it go, as a with block
// does: closing destroys it, joining its threads, after which its calls
// raise. Its threads never take the GIL, so it lets go of it meanwhile.
template <typename T>
class Closable {
 public:
  explicit Closable(std::unique_ptr<T> held) : held_(std::move(held)) {}

  T& Get() {
    if (!held_) {
      throw std::runtime_error("closed");
    }
    return *held_;
  }

  void Close() {
    std::unique_ptr<T> closing = std::move(held_);
    const py::gil_scoped_release release;
    closing.reset();
  }

 private:
  std::unique_ptr<T> held_;
};

// with closes it on the way out.
template <typename T, typename Class>
void DefContextManager(Class& bound) {
  bound.def("close", &Closable<T>::Close)
      .def(
          "__enter__", [](Closable<T>& self) -> Closable<T>& { return self; }, py::return_value_policy::reference)
      .def("__exit__", [](Closable<T>& self, const py::args&) { self.Close(); });
}

}  // namespace

void BindTesting(py::module_& module) {
  py::module_ testing = module.def_submodule("testing", "The in-process server the package's tests play against.");
  using Server = augusta::agent::testing::Server;
  using ServerHandle = Closable<Server>;
  py::class_<ServerHandle> server(testing, "Server");
  server
      .def(py::init([](const std::filesystem::path& pack, const std::filesystem::path& public_key,
                       std::uint8_t player_count) {
             return ServerHandle(std::make_unique<Server>(pack, public_key, player_count));
           }),
           py::arg("pack"), py::arg("public_key"), py::arg("player_count"))
      .def_property_readonly("address", [](ServerHandle& self) { return self.Get().Address(); });
  DefContextManager<Server>(server);
}

}  // namespace augusta::agent::native
