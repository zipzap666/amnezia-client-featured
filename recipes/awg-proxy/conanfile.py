from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.layout import basic_layout
import os


class AwgProxy(ConanFile):
    name = "awg-proxy"
    version = "3.1.20260814"
    package_type = "application"
    settings = "os", "arch"

    def layout(self):
        basic_layout(self)

    def export_sources(self):
        copy(self, "*", src=os.path.join(self.recipe_folder, "../../tools/awg-proxy"),
             dst=self.export_sources_folder, excludes=("*.exe",))

    def validate(self):
        if str(self.settings.os) != "Windows" or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("AWG proxy supports Windows x64 only")

    def build_requirements(self):
        self.tool_requires("go/1.26.0")

    def build(self):
        self.run('go build -mod=readonly -buildvcs=false -trimpath -ldflags="-s -w" '
                 '-o amnezia-awg-proxy.exe .', cwd=self.source_folder, env="conanbuild")

    def package(self):
        copy(self, "amnezia-awg-proxy.exe", src=self.source_folder,
             dst=os.path.join(self.package_folder, "bin"))
        copy(self, "THIRD_PARTY*", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "amnezia::awg-proxy")
