SHELL:=/usr/bin/bash
VENV_DIR:=../../venv

all:	py10

py10:	FORCE
	source $(VENV_DIR)/bin/activate && \
	c++ -O3 -Wall -shared -std=c++11 -fPIC $$(python3 -m pybind11 --includes) $@.cpp -o $@$$(python3 -m pybind11 --extension-suffix)

.PHONY:	FORCE
