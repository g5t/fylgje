from conan import ConanFile


class FylgjeRecipe(ConanFile):
    """Dependency provider for wheel builds, run by the scikit-build-core-conan
    backend (see pyproject.toml). Everything links statically into fylgje-cli,
    so the wheel needs no bundled shared libraries. Conda builds ignore this
    file and find the same packages with find_package."""

    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    default_options = {
        # ESS brokers use SASL_SSL with SCRAM-SHA-256. librdkafka's CMake build
        # only compiles SCRAM (and OAUTHBEARER) when both are on, and on
        # Linux/macOS "sasl" also pulls in cyrus-sasl. Without it SCRAM is
        # silently missing, though builtin.features still lists sasl_scram.
        "librdkafka/*:ssl": True,
        "librdkafka/*:sasl": True,
        # producers may compress with any codec; lz4 and snappy are built in
        "librdkafka/*:zlib": True,
        "librdkafka/*:zstd": True,
        "hdf5/*:shared": False,
        "hdf5/*:hl": False,
        "hdf5/*:with_zlib": False,
        "highfive/*:with_boost": False,
        "highfive/*:with_eigen": False,
        "highfive/*:with_xtensor": False,
        "highfive/*:with_opencv": False,
    }

    def requirements(self):
        self.requires("librdkafka/2.14.2")
        self.requires("highfive/3.1.1")
        # 1.14, matching the SWMR format cap in EventManager::open_file
        self.requires("hdf5/1.14.6", override=True)
        self.requires("fmt/12.2.0")
        self.requires("nlohmann_json/3.12.0")
        self.requires("flatbuffers/25.12.19")
