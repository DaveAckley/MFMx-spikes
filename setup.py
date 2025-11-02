import os
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext as _build_ext

# A custom build_ext command to ensure pybind11 is found
class build_ext(_build_ext):
    def run(self):
        import pybind11
        self.include_dirs.append(pybind11.get_include())
        super().run()

ext_modules = [
    Extension(
        'CMod',
        ['CMod.cpp'],
        include_dirs=[],
        language='c++',
        extra_compile_args=['-std=c++17', '-fvisibility=hidden'], # Or appropriate C++ standard
    ),
]

setup(
    name='CMod',
    version='0.0.1',
    author='Your Name',
    author_email='your.email@example.com',
    description='A pybind11 C++ module example',
    ext_modules=ext_modules,
    cmdclass={'build_ext': build_ext},
    zip_safe=False,
)
